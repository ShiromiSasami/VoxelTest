#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;

void AddVoxelDebugRenderPass(
    FRDGBuilder& GraphBuilder,
    FRDGTextureRef SceneColor,
    FRDGTextureRef SceneDepth,
    const void* OpaqueView);

// Experimental SDF raymarch pass (seed -> JFA -> SDF -> raymarch)
void AddVoxelRaymarchPass(
    FRDGBuilder& GraphBuilder,
    FRDGTextureRef SceneColor,
    FRDGTextureRef SceneDepth,
    const void* OpaqueView);

