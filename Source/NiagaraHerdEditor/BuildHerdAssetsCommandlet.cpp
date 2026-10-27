#include "BuildHerdAssetsCommandlet.h"
#include "HerdDataInterface.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraConstants.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeAssignment.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, NiagaraHerdEditor);
namespace HerdAssets
{
static void Save(UObject* Asset)
{
    UPackage* Package=Asset->GetOutermost(); Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Asset);
    FSavePackageArgs A; A.TopLevelFlags=RF_Public|RF_Standalone;
    const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    check(UPackage::SavePackage(Package,Asset,*File,A));
}
template<typename T> static T* NewAsset(const FString& Name)
{
    UPackage* P=CreatePackage(*(TEXT("/Game/Herd/")+Name));
    P->FullyLoad();
    return NewObject<T>(P,*Name,RF_Public|RF_Standalone|RF_Transactional);
}
static UEdGraphPin* First(UEdGraphNode* N,EEdGraphPinDirection D)
{
    for(auto* P:N->Pins) if(P->Direction==D && P->PinType.PinCategory!=TEXT("misc")) return P;
    return nullptr;
}
static UEdGraphPin& Override(UNiagaraNodeFunctionCall* N,FString Name,FNiagaraTypeDefinition Type)
{
    return FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*N,FNiagaraParameterHandle(*(N->GetFunctionName()+TEXT(".")+Name)),Type,FGuid(),FGuid());
}
static void Link(UNiagaraNodeFunctionCall* N,FString Input,FNiagaraVariable Var)
{
    auto& Pin=Override(N,Input,Var.GetType());
    TSet<FNiagaraVariableBase> Known; Known.Add(Var);
    FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin,Var,Known,ENiagaraDefaultMode::FailIfPreviouslyNotSet);
}
static UMaterial* Material(FString Name,bool bParticles)
{
    auto* M=NewAsset<UMaterial>(Name); M->SetShadingModel(MSM_Unlit); M->TwoSided=true;
    M->bUsedWithNiagaraSprites=bParticles; M->bUsedWithNiagaraMeshParticles=bParticles;
    if(bParticles)
    {
        auto* Color=NewObject<UMaterialExpressionParticleColor>(M);
        M->GetExpressionCollection().AddExpression(Color);
        M->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
    }
    else
    {
        auto* Color=NewObject<UMaterialExpressionVectorParameter>(M); Color->ParameterName=TEXT("Tint"); Color->DefaultValue=FLinearColor(.1,.3,.4);
        M->GetExpressionCollection().AddExpression(Color);
        M->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
    }
    M->PostEditChange(); Save(M); return M;
}
static UNiagaraScript* Getter(FString Function,FNiagaraTypeDefinition Type)
{
    auto* S=NewAsset<UNiagaraScript>(TEXT("DI_")+Function);
    S->CheckVersionDataAvailable();
    S->SetUsage(ENiagaraScriptUsage::DynamicInput);
    S->GetLatestScriptData()->ModuleUsageBitmask=-1;
    auto* Source=NewObject<UNiagaraScriptSource>(S);
    auto* G=NewObject<UNiagaraGraph>(Source); G->Schema=UEdGraphSchema_Niagara::StaticClass(); Source->NodeGraph=G;
    S->SetLatestSource(Source);
    auto* Out=NewObject<UNiagaraNodeOutput>(G); Out->ScriptType=ENiagaraScriptUsage::DynamicInput;
    Out->Outputs.Add(FNiagaraVariable(Type,TEXT("Value"))); G->AddNode(Out); Out->CreateNewGuid(); Out->AllocateDefaultPins();
    auto* Call=NewObject<UNiagaraNodeFunctionCall>(G);
    Call->Signature.Name=*Function; Call->Signature.bMemberFunction=true; Call->Signature.bRequiresContext=false; Call->Signature.bSupportsGPU=true;
    Call->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(UHerdDataInterface::StaticClass()),TEXT("Simulation")));
    Call->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),TEXT("Index")));
    Call->Signature.Outputs.Add(FNiagaraVariable(Type,TEXT("Value")));
    G->AddNode(Call); Call->CreateNewGuid(); Call->AllocateDefaultPins();
    auto* DI=NewObject<UNiagaraNodeInput>(G);
    DI->Input=FNiagaraVariable(FNiagaraTypeDefinition(UHerdDataInterface::StaticClass()),TEXT("User.Flock"));
    DI->Usage=ENiagaraInputNodeUsage::Parameter; DI->ExposureOptions.bRequired=false; DI->ExposureOptions.bExposed=false;
    auto* Object=NewObject<UHerdDataInterface>(DI);
    FindFProperty<FObjectProperty>(DI->GetClass(),TEXT("DataInterface"))->SetObjectPropertyValue_InContainer(DI,Object);
    G->AddNode(DI); DI->CreateNewGuid(); DI->AllocateDefaultPins();
    auto* Index=NewObject<UNiagaraNodeInput>(G);
    Index->Input=SYS_PARAM_ENGINE_EXEC_INDEX; Index->Usage=ENiagaraInputNodeUsage::SystemConstant;
    G->AddNode(Index); Index->CreateNewGuid(); Index->AllocateDefaultPins();
    DI->Pins[0]->MakeLinkTo(Call->FindPinChecked(TEXT("Simulation"),EGPD_Input));
    Index->Pins[0]->MakeLinkTo(Call->FindPinChecked(TEXT("Index"),EGPD_Input));
    Call->FindPinChecked(TEXT("Value"),EGPD_Output)->MakeLinkTo(Out->Pins[0]);
    G->NotifyGraphChanged(); Save(S); return S;
}
static UNiagaraNodeFunctionCall* Add(UNiagaraNodeOutput* Out,const TCHAR* Path)
{
    auto* S=LoadObject<UNiagaraScript>(nullptr,Path); check(S);
    return FNiagaraStackGraphUtilities::AddScriptModuleToStack(S,*Out);
}
static void SetFromGetter(UNiagaraNodeAssignment* N,FString Variable,UNiagaraScript* Getter)
{
    FNiagaraTypeDefinition Type=Getter->GetUsage()==ENiagaraScriptUsage::DynamicInput ? CastChecked<UNiagaraScriptSource>(Getter->GetLatestSource())->NodeGraph->FindEquivalentOutputNode(ENiagaraScriptUsage::DynamicInput)->Outputs[0].GetType() : FNiagaraTypeDefinition::GetVec3Def();
    UNiagaraNodeFunctionCall* DI=nullptr;
    auto& Pin=Override(N,Variable,Type);
    FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(Pin,Getter,DI);
}
static void System(bool GPU,UMaterial* ParticleMaterial,UNiagaraScript* Position,UNiagaraScript* Velocity,UNiagaraScript* Color)
{
    FString Name=GPU?TEXT("NS_HerdGPU"):TEXT("NS_HerdCPU");
    auto* S=NewAsset<UNiagaraSystem>(Name); UNiagaraSystemFactoryNew::InitializeSystem(S,true);
    S->bFixedBounds=true; S->SetFixedBounds(FBox(FVector(-1600,-1000,-100),FVector(1600,1000,300)));
    FNiagaraVariable CountVar(FNiagaraTypeDefinition::GetIntDef(),TEXT("User.Count")); CountVar.SetValue<int32>(GPU?100000:500);
    S->GetExposedParameters().AddParameter(CountVar,true);
    S->GetExposedParameters().SetParameterValue<int32>(GPU?100000:500,CountVar);
    FNiagaraVariable DIVar(FNiagaraTypeDefinition(UHerdDataInterface::StaticClass()),TEXT("User.Flock"));
    S->GetExposedParameters().AddParameter(DIVar); S->GetExposedParameters().SetDataInterface(NewObject<UHerdDataInterface>(S),DIVar);
    auto* E=NewObject<UNiagaraEmitter>(S,FName(GPU?TEXT("SimpleMany"):TEXT("ComplexFew")),RF_Transactional);
    UNiagaraEmitterFactoryNew::InitializeEmitter(E,false);
    auto* D=E->GetLatestEmitterData();
    D->SimTarget=GPU?ENiagaraSimTarget::GPUComputeSim:ENiagaraSimTarget::CPUSim;
    D->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
    D->FixedBounds=S->GetFixedBounds(); D->MaxGPUParticlesSpawnPerFrame=4000000;
    D->bGpuAlwaysRunParticleUpdateScript=true;
    auto* G=CastChecked<UNiagaraScriptSource>(D->GraphSource)->NodeGraph.Get();
    auto* EU=G->FindEquivalentOutputNode(ENiagaraScriptUsage::EmitterUpdateScript);
    auto* State=Add(EU,TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
    Override(State,TEXT("Loop Duration"),FNiagaraTypeDefinition::GetFloatDef()).DefaultValue=TEXT("1000000.0");
    auto* Burst=Add(EU,TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
    Link(Burst,TEXT("Spawn Count"),CountVar);
    Override(Burst,TEXT("Loop Count Limit"),FNiagaraTypeDefinition::GetIntDef()).DefaultValue=TEXT("1");
    auto* PS=G->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript);
    auto* PU=G->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript);
    for(auto* Output:{PS,PU})
    {
        const TArray<FNiagaraVariable> Vars={FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(),TEXT("Particles.Position")),FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Particles.Velocity")),FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("Particles.Color"))};
        auto* N=FNiagaraStackGraphUtilities::AddParameterModuleToStack(Vars,*Output,INDEX_NONE,{TEXT("0,0,35"),TEXT("0,0,0"),TEXT("1,1,1,1")});
        SetFromGetter(N,TEXT("Particles.Position"),Position);
        SetFromGetter(N,TEXT("Particles.Velocity"),Velocity);
        SetFromGetter(N,TEXT("Particles.Color"),Color);
    }
    FNiagaraStackGraphUtilities::AddParameterModuleToStack({FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(),TEXT("Particles.SpriteSize")),FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Particles.Scale")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Particles.SpriteRotation"))},*PS,INDEX_NONE,{GPU?TEXT("(X=5,Y=9)"):TEXT("(X=10,Y=16)"),TEXT("(X=0.28,Y=0.085,Z=0.055)"),TEXT("0")});
    if(GPU)
    {
        auto* R=NewObject<UNiagaraSpriteRendererProperties>(E);
        R->Material=ParticleMaterial;
        R->Alignment=ENiagaraSpriteAlignment::VelocityAligned; R->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
        R->SortMode=ENiagaraSortMode::None;
        E->AddRenderer(R,D->Version.VersionGuid);
    }
    else
    {
        auto* R=NewObject<UNiagaraMeshRendererProperties>(E); R->Meshes.SetNum(1);
        R->Meshes[0].Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        R->FacingMode=ENiagaraMeshFacingMode::Velocity; R->bOverrideMaterials=true;
        FNiagaraMeshMaterialOverride Mat; Mat.ExplicitMat=ParticleMaterial; R->OverrideMaterials.Add(Mat);
        E->AddRenderer(R,D->Version.VersionGuid);
    }
    FNiagaraEditorUtilities::AddEmitterToSystem(*S,*E,D->Version.VersionGuid);
    S->RequestCompile(true); S->WaitForCompilationComplete(true,false);
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Diagnostics")),true);
    const auto* Compiled=S->GetEmitterHandles()[0].GetInstance().GetEmitterData();
    const auto& Executable=(GPU?Compiled->GetGPUComputeScript():Compiled->UpdateScriptProps.Script.Get())->GetVMExecutableData();
    FFileHelper::SaveStringToFile(GPU?Executable.LastHlslTranslationGPU:Executable.LastHlslTranslation,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics")/(Name+TEXT(".hlsl"))));
    UE_LOG(LogTemp,Display,TEXT("HERD_ASSET %s valid=%d ready=%d"),*Name,S->IsValid(),S->IsReadyToRun());
    check(S->IsValid() && S->IsReadyToRun()); Save(S);
}
}
UBuildHerdAssetsCommandlet::UBuildHerdAssetsCommandlet() { IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }
int32 UBuildHerdAssetsCommandlet::Main(const FString&)
{
    auto* Arena=HerdAssets::Material(TEXT("M_Arena"),false);
    auto* Particles=HerdAssets::Material(TEXT("M_Fish"),true);
    auto* Position=HerdAssets::Getter(TEXT("ReadPosition"),FNiagaraTypeDefinition::GetPositionDef());
    auto* Velocity=HerdAssets::Getter(TEXT("ReadVelocity"),FNiagaraTypeDefinition::GetVec3Def());
    auto* Color=HerdAssets::Getter(TEXT("ReadColor"),FNiagaraTypeDefinition::GetColorDef());
    HerdAssets::System(false,Particles,Position,Velocity,Color);
    HerdAssets::System(true,Particles,Position,Velocity,Color);
    UE_LOG(LogTemp,Display,TEXT("HERD_ASSETS_COMPLETE")); return 0;
}
