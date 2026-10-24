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

		// ADR 0001: stay on the classic replication system (no Iris) until M6.
		// bUseIris requires a unique build environment. Installed engines (Launcher on the Mac,
		// prebuilt Linux editor) only support the shared environment for Game/Editor targets, so
		// overriding it there would fail the build. In shared environments Iris stays compiled in
		// but is disabled at runtime via net.Iris.UseIrisReplication=0 in DefaultEngine.ini.
		if (Target.BuildEnvironment == TargetBuildEnvironment.Unique)
		{
			Target.bUseIris = false;
		}
	}
}
