using UnrealBuildTool;

public class NiagaraHerdTarget : TargetRules
{
    public NiagaraHerdTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("NiagaraHerd");
    }
}
