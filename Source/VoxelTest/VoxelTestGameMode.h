// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VoxelTestGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AVoxelTestGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    
    /** Constructor */
    AVoxelTestGameMode();


protected:
    virtual void BeginPlay() override;
};



