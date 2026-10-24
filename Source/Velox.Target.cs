// Velox - Game target (standalone, listen server, PIE; also `-server` on installed engines).

using UnrealBuildTool;

public class VeloxTarget : TargetRules
{
	public VeloxTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		ApplySharedVeloxTargetSettings(this);
	}

	/// <summary>
	/// Settings shared by all Velox targets (ADR 0001 section 1).
	/// </summary>
	internal static void ApplySharedVeloxTargetSettings(TargetRules Target)
	{
		Target.DefaultBuildSettings = BuildSettingsVersion.Latest;
		Target.IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		Target.ExtraModuleNames.Add("Velox");

		// ADR 0001: classic replication (no Iris) until M6, push model on. UE 5.8 has no bUseIris target
		// setting (Iris is always compiled in), so Iris is disabled at runtime via
		// net.Iris.UseIrisReplication=0 in DefaultEngine.ini. bWithPushModel requires a unique build
		// environment (always on in Editor targets); Game targets on installed engines keep the default.
		if (Target.BuildEnvironment == TargetBuildEnvironment.Unique)
		{
			Target.bWithPushModel = true;
		}
	}
}
