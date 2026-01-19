// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelTest.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "RendererInterface.h"
#include "SceneView.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif
#include "Rendering/Voxel/VoxelSceneProxy.h"
// Voxel pass public header
#include "Rendering/Voxel/VoxelRenderPass.h"

class FVoxelTestGameModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        // Map project shaders directory to "/Voxel"
        const FString ShaderDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Shaders"));
        AddShaderSourceDirectoryMapping(TEXT("/Voxel"), ShaderDir / TEXT("Voxel"));

        // Register PostOpaque render delegate
        IRendererModule& RendererModule = FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"));
        PostOpaqueHandle = RendererModule.RegisterPostOpaqueRenderDelegate(
            FPostOpaqueRenderDelegate::CreateRaw(this, &FVoxelTestGameModule::OnPostOpaqueRender));

#if WITH_EDITOR
    FEditorDelegates::BeginPIE.AddRaw(this, &FVoxelTestGameModule::OnBeginPIE);
    FEditorDelegates::EndPIE.AddRaw(this, &FVoxelTestGameModule::OnEndPIE);
#endif

    }

    virtual void ShutdownModule() override
    {
        if (PostOpaqueHandle.IsValid())
        {
            if (IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer")))
            {
                RendererModule->RemovePostOpaqueRenderDelegate(PostOpaqueHandle);
            }
            PostOpaqueHandle.Reset();
        }

#if WITH_EDITOR
    FEditorDelegates::BeginPIE.RemoveAll(this);
    FEditorDelegates::EndPIE.RemoveAll(this);
#endif

        ENQUEUE_RENDER_COMMAND(ClearVoxelProxiesCmd)(
            [](FRHICommandListImmediate&)
            {
                ClearVoxelProxies_RenderThread();
            });
    }

private:
    void OnPostOpaqueRender(FPostOpaqueRenderParameters& Parameters)
    {
        if (!Parameters.GraphBuilder)
        {
            return;
        }
        const FSceneView* SceneView = reinterpret_cast<const FSceneView*>(Parameters.View);
        if (!SceneView)
        {
            return;
        }
        AddVoxelRaymarchPass(*Parameters.GraphBuilder, Parameters.ColorTexture, Parameters.DepthTexture, SceneView);
        AddVoxelDebugRenderPass(*Parameters.GraphBuilder, Parameters.ColorTexture, Parameters.DepthTexture, SceneView);
    }

#if WITH_EDITOR
    void OnBeginPIE(const bool bIsSimulating)
    {
        // Start a new epoch for PIE proxies (epoch 1+)
        ENQUEUE_RENDER_COMMAND(BeginVoxelPIEEpochCmd)(
            [](FRHICommandListImmediate&)
            {
                FVoxelSceneProxy::BeginNewEpoch_RenderThread();  // Increments and activates new epoch
            });
    }

    void OnEndPIE(const bool bIsSimulating)
    {
        // Clear PIE epoch proxies and return to editor mode (epoch 0)
        ENQUEUE_RENDER_COMMAND(ClearVoxelPIEEpochCmd)(
            [](FRHICommandListImmediate&)
            {
                FVoxelSceneProxy::ClearCurrentEpochProxies_RenderThread();
                FVoxelSceneProxy::SetActiveEpoch_RenderThread(0);  // Return to editor epoch
            });
    }
#endif


    FDelegateHandle PostOpaqueHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FVoxelTestGameModule, VoxelTest, "VoxelTest");

DEFINE_LOG_CATEGORY(LogVoxelTest)
