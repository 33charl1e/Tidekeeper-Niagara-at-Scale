#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BuildHerdAssetsCommandlet.generated.h"

UCLASS()
class UBuildHerdAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UBuildHerdAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
