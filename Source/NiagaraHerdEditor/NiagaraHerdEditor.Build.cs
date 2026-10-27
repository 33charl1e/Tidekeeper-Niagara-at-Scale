using UnrealBuildTool;
public class NiagaraHerdEditor : ModuleRules
{
    public NiagaraHerdEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","UnrealEd","NiagaraHerd","Niagara","NiagaraCore","NiagaraEditor","AssetRegistry","AssetTools","RenderCore","RHI","Projects"});
    }
}
