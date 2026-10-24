// Velox - scripted input component (task 05).

#include "Movement/VeloxScriptedInputComponent.h"

#include "Character/VeloxCharacter.h"
#include "Containers/Ticker.h"
#include "Core/VeloxLog.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Movement/VeloxMoveDebug.h"
#include "Movement/VeloxMovementComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxScriptedInputComponent)

namespace VeloxScriptedInput
{
	/** A knockback larger than this many ground speeds is refused by the RPC validation (task 05 spec). */
	constexpr float MaxKnockbackInGroundSpeeds = 20.f;

	/** Seconds the locally controlled pawn must stand on the ground before a requested script starts (settle after spawn). */
	constexpr double SettleTime = 0.5;

	/** Seconds between repeated teleport requests while the pawn is not yet at the script's start location. */
	constexpr double RepositionRetryInterval = 1.0;

	struct FPendingRun
	{
		FString PathOrName;
		bool bQuit = false;
		bool bActive = false;
		double GroundSince = -1.0;
		double LastReposition = -100.0;
		FTSTicker::FDelegateHandle Handle;
	};
	FPendingRun GPending;

	AVeloxCharacter* FindLocalCharacter(UWorld*& OutWorld)
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE))
			{
				continue;
			}
			for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
			{
				if (It->IsLocallyControlled())
				{
					OutWorld = World;
					return *It;
				}
			}
		}
		return nullptr;
	}

	bool PollPending(float)
	{
		UWorld* World = nullptr;
		AVeloxCharacter* Character = FindLocalCharacter(World);
		UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
		UVeloxScriptedInputComponent* Scripted = Character ? Character->FindComponentByClass<UVeloxScriptedInputComponent>() : nullptr;
		if (!Movement || !Scripted)
		{
			GPending.GroundSince = -1.0;
			if (Character)
			{
				UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: %s has no scripted input component (shipping build?)"), *Character->GetName());
				GPending.bActive = false;
				return false;
			}
			return true;
		}
		if (!Scripted->LoadScriptFromFile(GPending.PathOrName))
		{
			GPending.bActive = false;
			if (GPending.bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return false;
		}
		if (Scripted->NeedsRepositioning() && World->GetTimeSeconds() - GPending.LastReposition > RepositionRetryInterval)
		{
			Scripted->RepositionToStart();
			GPending.LastReposition = World->GetTimeSeconds();
			GPending.GroundSince = -1.0;
			return true;
		}
		if (!Movement->IsOnGround() || Scripted->NeedsRepositioning())
		{
			GPending.GroundSince = -1.0;
			return true;
		}
		if (GPending.GroundSince < 0.0)
		{
			GPending.GroundSince = World->GetTimeSeconds();
		}
		if (World->GetTimeSeconds() - GPending.GroundSince < SettleTime)
		{
			return true;
		}

		GPending.bActive = false;
		Scripted->SetQuitOnFinish(GPending.bQuit);
		if (!Scripted->StartScript())
		{
			if (GPending.bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
		}
		return false;
	}
}

float FVeloxMoveScript::TotalDuration() const
{
	float Total = 0.f;
	for (const FVeloxScriptRow& Row : Rows)
	{
		Total += Row.Duration;
	}
	return Total;
}

UVeloxScriptedInputComponent::UVeloxScriptedInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(false);
}

void UVeloxScriptedInputComponent::BeginPlay()
{
	Super::BeginPlay();

#if !UE_BUILD_SHIPPING
	// -VeloxScript=<name>: start (and quit with the result as exit code) on the first locally controlled pawn.
	static bool bCommandLineHandled = false;
	FString ScriptName;
	if (!bCommandLineHandled && !IsRunningDedicatedServer() && FParse::Value(FCommandLine::Get(), TEXT("VeloxScript="), ScriptName))
	{
		bCommandLineHandled = true;
		RequestRun(ScriptName, true);
	}
#endif
}

AVeloxCharacter* UVeloxScriptedInputComponent::GetVeloxCharacter() const
{
	return Cast<AVeloxCharacter>(GetOwner());
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading

FString UVeloxScriptedInputComponent::ResolveScriptPath(const FString& PathOrName)
{
	TArray<FString> Candidates;
	Candidates.Add(PathOrName);
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), PathOrName));
	FString Name = PathOrName;
	if (FPaths::GetExtension(Name).IsEmpty())
	{
		Name += TEXT(".json");
	}
	Candidates.Add(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/MoveScripts"), Name));
	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

bool UVeloxScriptedInputComponent::LoadScriptFromFile(const FString& PathOrName)
{
	const FString Path = ResolveScriptPath(PathOrName);
	FString Json;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: cannot read script '%s' (resolved '%s', project dir '%s')"), *PathOrName, *Path, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		return false;
	}
	return LoadScriptFromJson(Json, FPaths::GetBaseFilename(Path));
}

bool UVeloxScriptedInputComponent::LoadScriptFromJson(const FString& Json, const FString& DefaultName)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: '%s' is not valid JSON"), *DefaultName);
		return false;
	}

	FVeloxMoveScript Parsed;
	Parsed.Name = DefaultName;
	Root->TryGetStringField(TEXT("name"), Parsed.Name);
	double StartYaw = 0.0;
	Root->TryGetNumberField(TEXT("startYaw"), StartYaw);
	Parsed.StartYaw = static_cast<float>(StartYaw);

	const TArray<TSharedPtr<FJsonValue>>* Start = nullptr;
	if (Root->TryGetArrayField(TEXT("startLocation"), Start) && Start->Num() == 3)
	{
		Parsed.StartLocation = FVector((*Start)[0]->AsNumber(), (*Start)[1]->AsNumber(), (*Start)[2]->AsNumber());
	}

	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Root->TryGetArrayField(TEXT("rows"), Rows) || Rows->IsEmpty())
	{
		UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: '%s' has no rows"), *Parsed.Name);
		return false;
	}

	auto ReadNumber = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float Default)
	{
		double Value = Default;
		Obj->TryGetNumberField(Key, Value);
		return static_cast<float>(Value);
	};

	for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
	{
		const TSharedPtr<FJsonObject> Obj = RowValue.IsValid() ? RowValue->AsObject() : nullptr;
		if (!Obj.IsValid())
		{
			UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: '%s' row is not an object"), *Parsed.Name);
			return false;
		}
		FVeloxScriptRow Row;
		Row.Duration = ReadNumber(Obj, TEXT("duration"), 0.f);
		if (Row.Duration <= 0.f)
		{
			UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: '%s' row %d needs duration > 0"), *Parsed.Name, Parsed.Rows.Num());
			return false;
		}
		Row.Forward = ReadNumber(Obj, TEXT("forward"), 0.f);
		Row.Right = ReadNumber(Obj, TEXT("right"), 0.f);
		Row.YawRate = ReadNumber(Obj, TEXT("yawRate"), 0.f);
		Obj->TryGetBoolField(TEXT("jump"), Row.bJump);
		Obj->TryGetBoolField(TEXT("override"), Row.bKnockbackOverride);
		Obj->TryGetStringField(TEXT("label"), Row.Label);
		double YawValue;
		if (Obj->TryGetNumberField(TEXT("yaw"), YawValue))
		{
			Row.Yaw = static_cast<float>(YawValue);
		}
		FString Until;
		if (Obj->TryGetStringField(TEXT("until"), Until))
		{
			if (Until.Equals(TEXT("airborne"), ESearchCase::IgnoreCase))
			{
				Row.Until = FVeloxScriptRow::EUntil::Airborne;
			}
			else if (Until.Equals(TEXT("ground"), ESearchCase::IgnoreCase))
			{
				Row.Until = FVeloxScriptRow::EUntil::Ground;
			}
			else
			{
				UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: '%s' row %d: unknown until '%s'"), *Parsed.Name, Parsed.Rows.Num(), *Until);
				return false;
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Knock = nullptr;
		if (Obj->TryGetArrayField(TEXT("serverKnockback"), Knock) && Knock->Num() == 3)
		{
			Row.ServerKnockback = FVector((*Knock)[0]->AsNumber(), (*Knock)[1]->AsNumber(), (*Knock)[2]->AsNumber());
		}
		const TSharedPtr<FJsonObject>* CheckObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("check"), CheckObj))
		{
			FVeloxScriptCheck Check;
			(*CheckObj)->TryGetStringField(TEXT("name"), Check.Name);
			const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
			if ((*CheckObj)->TryGetArrayField(TEXT("box"), Box) && Box->Num() == 4)
			{
				Check.Box = FBox2D(FVector2D((*Box)[0]->AsNumber(), (*Box)[1]->AsNumber()), FVector2D((*Box)[2]->AsNumber(), (*Box)[3]->AsNumber()));
				Check.bHasBox = true;
			}
			Check.bCheckOnGround = (*CheckObj)->TryGetBoolField(TEXT("onGround"), Check.bOnGround);
			Row.Check = Check;
		}
		Parsed.Rows.Add(MoveTemp(Row));
	}

	Script = MoveTemp(Parsed);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Running

bool UVeloxScriptedInputComponent::StartScript()
{
	AVeloxCharacter* Character = GetVeloxCharacter();
	UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Movement || Script.Rows.IsEmpty())
	{
		UE_LOG(LogVeloxNet, Error, TEXT("VeloxScript: cannot start (no character/movement or no script loaded)"));
		return false;
	}

	Result = FVeloxScriptResult();
	ScriptTime = 0.f;
	Yaw = Script.StartYaw;
	CurrentRow = INDEX_NONE;
	bJumpHeldByScript = false;
	LastSeenCorrections = Movement->GetNumCorrections();
	TimeStarted = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bRunning = true;
	bHavePrev = false;
	PostCorrectionSampleAt = -1.0;
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetSoak event=info script=%s ground_speed=%.1f jump_velocity=%.1f gravity_z=%.1f knockback_grace_max=%.3f ground_friction=%.3f stop_speed=%.1f"), *Script.Name,
		Movement->GetSimParams().MaxGroundSpeed, Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), Movement->GetSimParams().KnockbackGraceMax,
		Movement->GetSimParams().GroundFriction, Movement->GetSimParams().StopSpeed);

	// Tick order: component ticks run after their owner's tick, which waits for the movement component, so the input of
	// frame N is consumed by the move of frame N + 1 (a constant one-frame latency, the same as hardware input). A
	// prerequisite of the movement component on this component would be a tick cycle and stall both. Tests call
	// StepScript themselves before ticking the world (SetDrivenExternally) and so have no latency.

	UE_LOG(LogVeloxNet, Log, TEXT("VeloxScript: start script=%s rows=%d duration=%.2fs pos=%s"), *Script.Name, Script.Rows.Num(), Script.TotalDuration(), *Character->GetActorLocation().ToCompactString());
	return true;
}

void UVeloxScriptedInputComponent::StopScript()
{
	if (bRunning)
	{
		Finish(TEXT("stopped"));
	}
}

void UVeloxScriptedInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bDrivenExternally)
	{
		StepScript(DeltaTime);
	}
}

void UVeloxScriptedInputComponent::StepScript(float DeltaTime)
{
	if (!bRunning)
	{
		return;
	}

	AVeloxCharacter* Character = GetVeloxCharacter();
	UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Movement)
	{
		Finish(TEXT("no movement component"));
		return;
	}

	// Corrections since the last tick (owning client; always zero on an authority).
	const int32 NumCorrections = Movement->GetNumCorrections();
	if (NumCorrections > LastSeenCorrections)
	{
		const float Cm = Movement->GetLastCorrectionCm();
		Result.NumCorrections += NumCorrections - LastSeenCorrections;
		Result.SumCorrectionCm += Cm;
		Result.MaxCorrectionCm = FMath::Max(Result.MaxCorrectionCm, Cm);
		LastSeenCorrections = NumCorrections;
		UE_LOG(LogVeloxNet, Log, TEXT("VeloxCorrection t=%.3f cm=%.2f st=%.3f air=%d grace=%.3f latch=%d speed2d=%.1f vz=%.1f"), Movement->GetLastCorrectionTime(), Cm, ScriptTime,
			!Movement->IsOnGround(), Movement->GetKnockbackGrace(), Movement->IsJumpLatched(), Movement->GetSpeed2D(), Movement->Velocity.Z);
		if (PostCorrectionSampleAt < 0.0) // keep the first correction's sample; a burst of corrections must not push it out
		{
			PostCorrectionSampleAt = Movement->GetLastCorrectionTime() + 0.15;
		}
		bHavePrev = false; // the replay rewrote the state, so this tick is no landing observation
	}

	// Soak instrumentation: peak speed, landings (jumped or not), samples after a correction.
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool bOnGround = Movement->IsOnGround();
		const float Speed2D = Movement->GetSpeed2D();
		const float Vz = Movement->Velocity.Z;
		if (Speed2D > Result.PeakSpeed2D)
		{
			Result.PeakSpeed2D = Speed2D;
			Result.PeakTime = Now;
		}
		if (PostCorrectionSampleAt >= 0.0 && Now >= PostCorrectionSampleAt)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetSoak event=post_correction_sample t=%.3f air=%d grace=%.3f speed2d=%.1f vz=%.1f"), Now, !bOnGround, Movement->GetKnockbackGrace(), Speed2D, Vz);
			PostCorrectionSampleAt = -1.0;
		}
		if (bHavePrev && !bPrevOnGround && PrevVz < 0.f)
		{
			const float JumpVelocity = Movement->GetSimParams().JumpVelocity;
			const bool bLandedNoJump = bOnGround;
			const bool bLandedJumped = !bOnGround && Vz > 0.5f * JumpVelocity;
			if (bLandedNoJump || bLandedJumped)
			{
				UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetSoak event=land t=%.3f st=%.3f jumped=%d vz=%.1f speed_before=%.1f speed_after=%.1f latch=%d"), Now, ScriptTime, bLandedJumped, Vz, PrevSpeed2D, Speed2D, Movement->IsJumpLatched());
			}
		}
		bHavePrev = true;
		bPrevOnGround = bOnGround;
		PrevVz = Vz;
		PrevSpeed2D = Speed2D;
	}

	// Row selection and row-enter events (a long frame may skip several rows). An "until" row whose condition holds
	// jumps the script time to the row's end, so the next row starts in this very tick.
	int32 RowIndex = INDEX_NONE;
	for (;;)
	{
		float RowStart = 0.f;
		RowIndex = INDEX_NONE;
		for (int32 I = 0; I < Script.Rows.Num(); ++I)
		{
			if (ScriptTime < RowStart + Script.Rows[I].Duration)
			{
				RowIndex = I;
				break;
			}
			RowStart += Script.Rows[I].Duration;
		}
		if (RowIndex == INDEX_NONE)
		{
			break;
		}
		const FVeloxScriptRow& Candidate = Script.Rows[RowIndex];
		const bool bOnGround = Movement->IsOnGround();
		const bool bConditionMet = (Candidate.Until == FVeloxScriptRow::EUntil::Airborne && !bOnGround) || (Candidate.Until == FVeloxScriptRow::EUntil::Ground && bOnGround);
		// Not in the row's first tick: the condition must be observed after the row's input had a chance to act.
		if (!bConditionMet || RowIndex != CurrentRow || ScriptTime <= RowStart)
		{
			break;
		}
		ScriptTime = RowStart + Candidate.Duration;
	}
	const int32 TargetRow = RowIndex == INDEX_NONE ? Script.Rows.Num() - 1 : RowIndex;
	for (int32 I = CurrentRow + 1; I <= TargetRow; ++I)
	{
		// Entering row I (and leaving the previous one).
		if (Script.Rows.IsValidIndex(CurrentRow))
		{
			EvaluateCheck(Script.Rows[CurrentRow]);
		}
		CurrentRow = I;
		const FVeloxScriptRow& Entered = Script.Rows[I];
		if (Entered.Yaw.IsSet())
		{
			Yaw = Entered.Yaw.GetValue();
		}
		if (!Entered.Label.IsEmpty())
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxScript: st=%.3f row=%d '%s' pos=%s vel=%s"), ScriptTime, I, *Entered.Label, *Character->GetActorLocation().ToCompactString(), *Movement->Velocity.ToCompactString());
		}
		if (Entered.ServerKnockback.IsSet())
		{
			SendTestKnockback(Entered.ServerKnockback.GetValue(), Entered.bKnockbackOverride);
		}
	}

	if (RowIndex == INDEX_NONE)
	{
		if (Script.Rows.IsValidIndex(CurrentRow))
		{
			EvaluateCheck(Script.Rows[CurrentRow]);
		}
		Finish(nullptr);
		return;
	}

	ApplyRow(Script.Rows[RowIndex], DeltaTime);
	ScriptTime += DeltaTime;
	++Result.Ticks;
}

void UVeloxScriptedInputComponent::ApplyRow(const FVeloxScriptRow& Row, float DeltaTime)
{
	AVeloxCharacter* Character = GetVeloxCharacter();
	UVeloxMovementComponent* Movement = Character->GetVeloxMovementComponent();

	Yaw = FRotator::NormalizeAxis(Yaw + Row.YawRate * DeltaTime);
	const FRotator YawRotation(0.0, Yaw, 0.0);
	if (AController* Controller = Character->GetController())
	{
		Controller->SetControlRotation(YawRotation);
	}

	// Same sequence as AVeloxCharacter::Input_Move.
	Movement->SetLocalMoveInput(FVector2D(Row.Forward, Row.Right));
	if (Row.Right != 0.f)
	{
		Character->AddMovementInput(YawRotation.RotateVector(FVector::RightVector), Row.Right);
	}
	if (Row.Forward != 0.f)
	{
		Character->AddMovementInput(YawRotation.RotateVector(FVector::ForwardVector), Row.Forward);
	}

	// Same sequence as Input_JumpStarted / Input_JumpCompleted.
	if (Row.bJump && !bJumpHeldByScript)
	{
		Movement->SetJumpHeld(true);
		Character->Jump();
	}
	else if (!Row.bJump && bJumpHeldByScript)
	{
		Movement->SetJumpHeld(false);
		Character->StopJumping();
	}
	bJumpHeldByScript = Row.bJump;
}

void UVeloxScriptedInputComponent::EvaluateCheck(const FVeloxScriptRow& Row)
{
	if (!Row.Check.IsSet())
	{
		return;
	}
	const FVeloxScriptCheck& Check = Row.Check.GetValue();
	const AVeloxCharacter* Character = GetVeloxCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Movement)
	{
		return;
	}

	const FVector Location = Character->GetActorLocation();
	bool bOk = true;
	if (Check.bHasBox && !Check.Box.IsInside(FVector2D(Location.X, Location.Y)))
	{
		bOk = false;
	}
	if (Check.bCheckOnGround && Movement->IsOnGround() != Check.bOnGround)
	{
		bOk = false;
	}
	++Result.ChecksRun;
	if (!bOk)
	{
		++Result.ChecksFailed;
	}
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxScript: check '%s' %s st=%.3f pos=%s onGround=%d"), *Check.Name, bOk ? TEXT("PASS") : TEXT("FAIL"), ScriptTime, *Location.ToCompactString(), Movement->IsOnGround());
}

void UVeloxScriptedInputComponent::ReleaseInput()
{
	if (AVeloxCharacter* Character = GetVeloxCharacter())
	{
		if (UVeloxMovementComponent* Movement = Character->GetVeloxMovementComponent())
		{
			Movement->SetLocalMoveInput(FVector2D::ZeroVector);
			if (bJumpHeldByScript)
			{
				Movement->SetJumpHeld(false);
				Character->StopJumping();
			}
		}
	}
	bJumpHeldByScript = false;
}

void UVeloxScriptedInputComponent::Finish(const TCHAR* Reason)
{
	ReleaseInput();
	bRunning = false;

	Result.bFinished = true;
	Result.Reason = Reason ? Reason : TEXT("");
	if (Result.ChecksFailed > 0)
	{
		Result.Reason = FString::Printf(TEXT("%d of %d checkpoints failed"), Result.ChecksFailed, Result.ChecksRun);
	}
	Result.bPassed = Result.Reason.IsEmpty();

	const float Mean = Result.NumCorrections > 0 ? Result.SumCorrectionCm / Result.NumCorrections : 0.f;
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetSoak: script=%s moves=%d corrections=%d max=%.2f mean=%.2f checks=%d/%d peak2d=%.1f peak_t=%.3f result=%s reason=%s"),
		*Script.Name, Result.Ticks, Result.NumCorrections, Result.MaxCorrectionCm, Mean, Result.ChecksRun - Result.ChecksFailed, Result.ChecksRun, Result.PeakSpeed2D, Result.PeakTime,
		Result.bPassed ? TEXT("PASS") : TEXT("FAIL"), Result.Reason.IsEmpty() ? TEXT("-") : *Result.Reason);

#if !UE_BUILD_SHIPPING
	VeloxMoveDebug::StopRecording();
#endif

	OnScriptFinished.Broadcast();
	if (bQuitOnFinish)
	{
		FPlatformMisc::RequestExitWithStatus(false, Result.bPassed ? 0 : 1);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Test knockback

void UVeloxScriptedInputComponent::SendTestKnockback(const FVector& Impulse, bool bOverride)
{
	const AVeloxCharacter* Character = GetVeloxCharacter();
	UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Movement)
	{
		return;
	}
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetSoak event=knockback t=%.3f st=%.3f impulse=%s override=%d"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, ScriptTime, *Impulse.ToCompactString(), bOverride);
	if (GetOwner()->HasAuthority())
	{
		Movement->AddKnockback(Impulse, bOverride);
	}
	else
	{
		ServerApplyTestKnockback(Impulse, bOverride);
	}
}

bool UVeloxScriptedInputComponent::ServerApplyTestKnockback_Validate(FVector Impulse, bool bOverride)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const AVeloxCharacter* Character = GetVeloxCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	return Movement && Impulse.Size() <= VeloxScriptedInput::MaxKnockbackInGroundSpeeds * Movement->GetSimParams().MaxGroundSpeed;
#endif
}

void UVeloxScriptedInputComponent::ServerApplyTestKnockback_Implementation(FVector Impulse, bool bOverride)
{
#if !UE_BUILD_SHIPPING
	if (const AVeloxCharacter* Character = GetVeloxCharacter())
	{
		if (UVeloxMovementComponent* Movement = Character->GetVeloxMovementComponent())
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetSoak event=knockback_server t=%.3f impulse=%s override=%d"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, *Impulse.ToCompactString(), bOverride);
			Movement->AddKnockback(Impulse, bOverride);
		}
	}
#endif
}

bool UVeloxScriptedInputComponent::NeedsRepositioning() const
{
	const AActor* Owner = GetOwner();
	return Owner && Script.StartLocation.IsSet() && FVector::Dist2D(Owner->GetActorLocation(), Script.StartLocation.GetValue()) > 5.0;
}

void UVeloxScriptedInputComponent::RepositionToStart()
{
	if (!Script.StartLocation.IsSet())
	{
		return;
	}
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxScript: reposition %s -> %s"), *GetOwner()->GetActorLocation().ToCompactString(), *Script.StartLocation.GetValue().ToCompactString());
	if (GetOwner()->HasAuthority())
	{
		ServerTeleportTestPawn_Implementation(Script.StartLocation.GetValue(), Script.StartYaw);
	}
	else
	{
		ServerTeleportTestPawn(Script.StartLocation.GetValue(), Script.StartYaw);
	}
}

bool UVeloxScriptedInputComponent::ServerTeleportTestPawn_Validate(FVector Location, float InYaw)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

void UVeloxScriptedInputComponent::ServerTeleportTestPawn_Implementation(FVector Location, float InYaw)
{
#if !UE_BUILD_SHIPPING
	if (AVeloxCharacter* Character = GetVeloxCharacter())
	{
		Character->TeleportTo(Location, FRotator(0.0, InYaw, 0.0));
		if (AController* Controller = Character->GetController())
		{
			Controller->SetControlRotation(FRotator(0.0, InYaw, 0.0));
		}
		if (UVeloxMovementComponent* Movement = Character->GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Console/command-line request

void UVeloxScriptedInputComponent::RequestRun(const FString& PathOrName, bool bQuitOnFinish)
{
	using namespace VeloxScriptedInput;
	GPending.PathOrName = PathOrName;
	GPending.bQuit = bQuitOnFinish;
	GPending.GroundSince = -1.0;
	GPending.LastReposition = -100.0;
	if (!GPending.bActive)
	{
		GPending.bActive = true;
		GPending.Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&PollPending), 0.1f);
	}
}
