// Velox - Dedicated server target. Requires a source build of the engine (ADR 0001 section 5).

using UnrealBuildTool;

public class VeloxServerTarget : TargetRules
{
	public VeloxServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		// Unique environment so target-specific engine settings (bWithPushModel, bUseLoggingInShipping) are allowed.
		BuildEnvironment = TargetBuildEnvironment.Unique;
		VeloxTarget.ApplySharedVeloxTargetSettings(this);
		bUseLoggingInShipping = true;
	}
}
