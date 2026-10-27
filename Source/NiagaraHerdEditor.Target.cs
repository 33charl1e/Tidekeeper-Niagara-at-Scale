using UnrealBuildTool;

public class NiagaraHerdEditorTarget : TargetRules
{
    public NiagaraHerdEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("NiagaraHerd");
        ExtraModuleNames.Add("NiagaraHerdEditor");
    }
}
