// Velox - primary game module (ADR 0001 section 1).

using UnrealBuildTool;

public class Velox : ModuleRules
{
	public Velox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;
		CppStandard = CppStandardVersion.Cpp20;

		// Includes are module-root relative, e.g. #include "Movement/VeloxMovementComponent.h".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"NetCore",
			"AIModule",
			"NavigationSystem",
			"UMG",
			"Slate",
			"SlateCore",
			"PhysicsCore",
			"DeveloperSettings"
		});

		// Empty in M0; add FunctionalTesting when the first functional test map lands.
		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
