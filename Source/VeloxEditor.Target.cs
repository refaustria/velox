// Velox - Editor target.

using UnrealBuildTool;

public class VeloxEditorTarget : TargetRules
{
	public VeloxEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		VeloxTarget.ApplySharedVeloxTargetSettings(this);
	}
}
