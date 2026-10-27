#pragma once
#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "HerdDataInterface.generated.h"

// Niagara owns the lifetime and scheduling of both simulation backends.
// GPU work is inserted into Niagara's RDG pre-stage; the CPU backend ticks as a DI.
UCLASS(EditInlineNew, Category="Herd", meta=(DisplayName="Herd Simulation"))
class NIAGARAHERD_API UHerdDataInterface : public UNiagaraDataInterface
{
    GENERATED_BODY()
public:
    UHerdDataInterface(const FObjectInitializer& ObjectInitializer);
    virtual void PostInitProperties() override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
    virtual bool InitPerInstanceData(void* Data, FNiagaraSystemInstance* Instance) override;
    virtual void DestroyPerInstanceData(void* Data, FNiagaraSystemInstance* Instance) override;
    virtual int32 PerInstanceDataSize() const override;
    virtual bool PerInstanceTick(void* Data, FNiagaraSystemInstance* Instance, float Dt) override;
    virtual bool HasPreSimulateTick() const override { return true; }
    virtual bool PostSimulateCanOverlapFrames() const override { return false; }
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& Info, void* InstanceData, FVMExternalFunction& Out) override;
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;
    virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& Builder) const override;
    virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;
#if WITH_EDITORONLY_DATA
    virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& Functions) const override;
    virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* Visitor) const override;
    virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& Info, FString& Out) override;
    virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& Info, const FNiagaraDataInterfaceGeneratedFunction& Function, int Index, FString& Out) override;
#endif
protected:
    virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};
