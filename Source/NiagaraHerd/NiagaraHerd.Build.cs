using UnrealBuildTool;

public class NiagaraHerd : ModuleRules
{
    public NiagaraHerd(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Niagara", "RenderCore", "RHI"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { "MovieSceneCapture", "ImageWriteQueue", "Slate", "SlateCore" });
    }
}
