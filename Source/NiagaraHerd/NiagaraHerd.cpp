#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FNiagaraHerdModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        const FString ShaderDirectory=FPaths::Combine(FPaths::ProjectDir(),TEXT("Shaders"));
        // Cooked builds load the compiled global shader library, not source files.
        if(FPaths::DirectoryExists(ShaderDirectory)) AddShaderSourceDirectoryMapping(TEXT("/NiagaraHerd"),ShaderDirectory);
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FNiagaraHerdModule, NiagaraHerd, "NiagaraHerd");
