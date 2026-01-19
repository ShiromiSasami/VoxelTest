#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelActor.generated.h"

class UVoxelRenderComponent;

UCLASS()
class AVoxelActor : public AActor
{
    GENERATED_BODY()
public:
    AVoxelActor();

protected:
    UPROPERTY(VisibleAnywhere, Category="Voxel")
    UVoxelRenderComponent* VoxelComponent;
};
