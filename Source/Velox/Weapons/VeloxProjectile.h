// Velox - rockets and plasma (ADR 0003 section 7, task 05).
//
// Three roles of the same class:
//   Server (real)   spawned by UVeloxGameplayAbility_FireProjectile on the authority, forward-simulated by
//                   min(ping / 2, ProjectileForwardPredictionMax), moved by FVeloxProjectileSim::Step against the world
//                   (Weapon channel sphere sweep) and the registered pawns' current hit shapes (no rewind, Q3). Replicated
//                   once (COND_InitialOnly launch data, DORM_DormantAll), destroyed on explosion (destruction replicates).
//                   The explosion applies the direct hit and Q3 radius damage through GE_Damage, executes the impact cue
//                   on the shooter's ASC (outside any prediction window: every client gets it) and destroys itself.
//   Fake            spawned (never replicated) by the owning client at fire time from the quantized shot; flies with
//                   the same sim against the world and the rendered proxy capsules; purely visual unless it hits a
//                   rendered pawn (then it predicts the owner's self-knockback from there at once).
//   Proxy           the replicated real projectile on other clients: dead-reckoned from StartLocation, LaunchVelocity
//                   and SpawnServerTime, stopped visually at the world; hidden on the owning client.
//
// Owner self splash (rocket jump): evaluated in the owner's move timeline on both ends, see ScheduleOwnerSelfSplash.

#pragma once

#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "Weapons/VeloxProjectileSim.h"

#include "VeloxProjectile.generated.h"

class APawn;
class UAbilitySystemComponent;
class USphereComponent;
class UVeloxMovementComponent;
class UVeloxWeaponDefinition;
struct FGameplayCueParameters;

/** One launch: the server's real projectile or the owning client's fake. */
struct FVeloxProjectileLaunch
{
	const UVeloxWeaponDefinition* Definition = nullptr;
	APawn* Shooter = nullptr;
	/** The shooter's ASC (instigator of every damage spec and owner of the impact cue). */
	UAbilitySystemComponent* SourceASC = nullptr;
	/** The eye, quantized as the server received it (NetQuantize100). */
	FVector Origin = FVector::ZeroVector;
	/** Unit direction (quantized normal, renormalised). */
	FVector Dir = FVector::ForwardVector;
	uint8 ShotSeq = 0;
	/**
	 * ADR 0004 section 3: the shooter had Quad Damage at fire (Q3 stores the factor in the missile). Server: the shooter
	 * ASC's tag; owning client's fake: its local state (see UVeloxGameplayAbility_FireProjectile).
	 */
	bool bQuad = false;
	/** Server: forward simulation at spawn (s); 0 for the fake and for a shooter on the server's machine. */
	float ForwardSimSeconds = 0.f;
	/** The owner's move-timed self splash of this shot (0 = none) on OwnerMovement, and its explosion origin. */
	int32 SelfSplashEventId = 0;
	UVeloxMovementComponent* OwnerMovement = nullptr;
	FVector SelfSplashOrigin = FVector::ZeroVector;
};

#if !UE_BUILD_SHIPPING
/** Last server explosion of this process (automation tests and logs). */
struct FVeloxProjectileExplosionInfo
{
	uint8 ShotSeq = 0;
	FVector Location = FVector::ZeroVector;
	TWeakObjectPtr<AActor> DirectVictim;
	/** Splash victims and their Q3 points (the owner only when the explosion resolved its self splash). */
	TArray<TPair<TWeakObjectPtr<AActor>, int32>> Splash;
	bool bOwnerSelfSplashDeferred = false;
	/** B26 hit confirm: non-owner victims of this explosion, health + armor they lost (clamped 255) and whether one died. */
	int32 ConfirmedVictims = 0;
	int32 ConfirmedDamage = 0;
	bool bKill = false;
	/** ClientConfirmHit was sent (the owner has an AVeloxPlayerController and ConfirmedVictims > 0). */
	bool bConfirmSent = false;
	int32 Count = 0;
};
#endif

UCLASS()
class VELOX_API AVeloxProjectile : public AActor
{
	GENERATED_BODY()

public:
	AVeloxProjectile(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Server: spawns, forward-simulates (may explode inside the window: then it never replicates) and returns it. */
	static AVeloxProjectile* SpawnServerProjectile(UWorld& World, TSubclassOf<AVeloxProjectile> ProjectileClass, const FVeloxProjectileLaunch& Launch);

	/** Owning client: spawns the local fake (not replicated). */
	static AVeloxProjectile* SpawnFakeProjectile(UWorld& World, TSubclassOf<AVeloxProjectile> ProjectileClass, const FVeloxProjectileLaunch& Launch);

	/**
	 * World-only flight of a launch (FVeloxProjectileSim::WorldFlight on the Weapon channel, owner ignored): seconds to
	 * the world impact or the lifetime end, and where. Identical on client and server for the same quantized shot.
	 */
	static double ComputeWorldFlight(const UWorld& World, const UVeloxWeaponDefinition& Definition, const FVector& Origin, const FVector& Dir, const AActor* IgnoreActor, FVector& OutLocation);

	/**
	 * The owner's self splash, in the owner's move timeline (task 05 decision, see the report): runs at the end of the
	 * first owner move with time stamp >= MoveTime (UVeloxMovementComponent::AddMoveTimedEvent). bPredicted (owning
	 * client): FVeloxProjectileSim::SelfKnockback -> AddKnockback(Predicted, ShotSeq). Server (remote owner): GE_Damage
	 * with bSelf/bRadius (half damage, full knockback through AddKnockback(Server)), then ServerResolveSelfKnockbackShot.
	 * The server also registers the shot (ServerBeginSelfKnockbackShot). Returns the event id. bQuad: the shot's Quad
	 * state (the splash damage is scaled before the falloff, both ends alike).
	 */
	static int32 ScheduleOwnerSelfSplash(UVeloxMovementComponent& OwnerMovement, const UVeloxWeaponDefinition& Definition, UAbilitySystemComponent* SourceASC, float MoveTime,
		const FVector& Origin, uint8 ShotSeq, bool bPredicted, bool bQuad = false);

	/** Evaluates the owner's self splash now (the events above, a fake's pawn hit, the server deadline). */
	static void RunOwnerSelfSplash(UVeloxMovementComponent& OwnerMovement, const UVeloxWeaponDefinition& Definition, UAbilitySystemComponent* SourceASC, const FVector& Origin,
		uint8 ShotSeq, bool bPredicted, const TCHAR* Reason, bool bQuad = false);

	/**
	 * The shot's direct or splash base damage with its Quad state applied (Q3 multiplies the missile's damage and
	 * splashDamage at fire, so the falloff truncates the scaled value): FVeloxDamageMath::ScaleDamage(Base, QuadDamageFactor).
	 */
	static int32 ScaledShotDamage(int32 BaseDamage, bool bQuad, const UAbilitySystemComponent* SettingsASC);

	/**
	 * UVeloxCombatFeedbackComponent hook: an executed impact cue reached CueAvatar. On the owning client it ends the fake
	 * of that shot; when the authoritative explosion is elsewhere than the fake's self-splash origin, the predicted self
	 * splash is cancelled (the server resolves it from its explosion and corrects).
	 */
	static void OnImpactCue(const AActor* CueAvatar, const FGameplayCueParameters& Parameters);

	bool IsFake() const { return bFake; }
	bool HasExploded() const { return bExploded; }
	uint8 GetShotSeq() const { return ShotSeq; }
	bool IsQuad() const { return bQuad; }
	const UVeloxWeaponDefinition* GetDefinition() const { return Definition; }
	FVector GetStartLocation() const { return StartLocation; }
	FVector GetLaunchVelocity() const { return LaunchVelocity; }
	float GetSpawnServerTime() const { return SpawnServerTime; }

#if !UE_BUILD_SHIPPING
	static const FVeloxProjectileExplosionInfo& GetLastServerExplosion();
#endif

	//~AActor interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** ResetLevel (match restart, ADR 0004 section 5): a projectile in flight is destroyed, its owner's pending self splash dropped. */
	virtual void Reset() override;
	//~End of AActor interface

protected:
	void TickServer(float DeltaSeconds);
	void TickFake(float DeltaSeconds);
	void TickProxy(float DeltaSeconds);

	void ServerExplode(const FVeloxProjectileImpact& Impact);
	void FakeDetonate(const FVeloxProjectileImpact& Impact);

	/** Sphere sweep on the Weapon channel (world only: pawns and projectiles ignore it), owner and self ignored. */
	bool SweepWorld(const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal) const;

	float GetRadius() const;

	UPROPERTY(VisibleAnywhere, Category = "Velox|Projectile")
	TObjectPtr<USphereComponent> CollisionComponent;

	/** Launch point (the validated eye). Replicated once. */
	UPROPERTY(Replicated)
	FVector_NetQuantize100 StartLocation;

	UPROPERTY(Replicated)
	FVector_NetQuantize10 LaunchVelocity;

	/** Server world time at which the projectile was at StartLocation (now - forward simulation at spawn). */
	UPROPERTY(Replicated)
	float SpawnServerTime = 0.f;

	UPROPERTY(Replicated)
	uint8 ShotSeq = 0;

	UPROPERTY(Replicated)
	TObjectPtr<const UVeloxWeaponDefinition> Definition;

	/** ADR 0004 section 3: fired under Quad Damage (direct and splash damage x QuadDamageFactor at the explosion). */
	UPROPERTY(Replicated)
	bool bQuad = false;

	FVeloxProjectileState State;
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	TWeakObjectPtr<UVeloxMovementComponent> SelfSplashMovement;
	int32 SelfSplashEventId = 0;
	FVector SelfSplashOrigin = FVector::ZeroVector;
	float ForwardSimSeconds = 0.f;
	bool bFake = false;
	bool bExploded = false;
	bool bProxyStopped = false;
};
