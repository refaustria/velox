// Velox - Client target (strips server code). Requires a source build of the engine (ADR 0001 section 5).

using UnrealBuildTool;

public class VeloxClientTarget : TargetRules
{
	public VeloxClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		// Unique environment so target-specific engine settings (bWithPushModel) are allowed.
		BuildEnvironment = TargetBuildEnvironment.Unique;
		VeloxTarget.ApplySharedVeloxTargetSettings(this);
	}
}
