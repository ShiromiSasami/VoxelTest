#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "Rendering/Voxel/VoxelRenderResources.h"
#include "RHI.h"
#include "RHIResources.h"

class FMeshElementCollector;
class FSceneView;
class UVoxelRenderComponent;

struct FDebugMeshRHI
{
    FBufferRHIRef VertexBuffer;
    FBufferRHIRef IndexBuffer;
    uint32 NumVertices = 0;
    uint32 NumIndices  = 0;
    bool IsValid() const { return VertexBuffer.IsValid() && IndexBuffer.IsValid() && NumVertices > 0 && NumIndices > 0; }
    void Reset() { VertexBuffer.SafeRelease(); IndexBuffer.SafeRelease(); NumVertices = NumIndices = 0; }
};

class FVoxelSceneProxy : public FPrimitiveSceneProxy
{
public:
    explicit FVoxelSceneProxy(const UVoxelRenderComponent* InComponent);
    virtual ~FVoxelSceneProxy() override;

    // 今回は使わない
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                        const FSceneViewFamily& ViewFamily,
                                        uint32 VisibilityMap,
                                        FMeshElementCollector& Collector) const override;

    const TSharedPtr<FVoxelRenderResource>& GetRenderResources() const { return VolumeRenderResources; }

    const FDebugMeshRHI& GetDebugMesh() const { return DebugMesh; }
    FMatrix GetInstanceTransform() const { return GetLocalToWorld(); }
    void RebuildDebugMesh_RenderThread(FRHICommandListImmediate& RHICmdList, const FVoxelRenderResource& Resource);

    // Render-thread accessor to the shared proxy cache
    static const TArray<FVoxelSceneProxy*, TInlineAllocator<64>>& GetProxies_RenderThread();
    static void ClearProxies_RenderThread();
    static void RegisterProxy_RenderThread(FVoxelSceneProxy* Proxy);
    static void UnregisterProxy_RenderThread(FVoxelSceneProxy* Proxy);
    // Epoch-based management to separate editor vs PIE proxies
    // Epoch 0 = Editor mode, Epoch 1+ = PIE sessions
    static void BeginNewEpoch_RenderThread();  // Increments current epoch and activates it
    static void ClearEpochProxies_RenderThread(uint64 Epoch);
    static void ClearCurrentEpochProxies_RenderThread();
    static void SetActiveEpoch_RenderThread(uint64 Epoch);  // Manually set active epoch (use 0 for editor)
    static uint64 GetActiveEpoch_RenderThread();

    uint64 GetRegistrationEpoch_RenderThread() const { return RegistrationEpoch_RT; }
    virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }
    virtual SIZE_T GetTypeHash() const override { return 0; }

private:
    TSharedPtr<FVoxelRenderResource> VolumeRenderResources;
    
    FDebugMeshRHI DebugMesh;
    void BuildDebugMesh_RenderThread(
        FRHICommandListImmediate& RHICmdList,
        const TArray<FVector3f>& Centers,
        const TArray<float>& Scales);
    static TArray<FVoxelSceneProxy*, TInlineAllocator<64>> GProxies_RT;
    static uint64 GCurrentEpoch_RT;
    static uint64 GActiveEpoch_RT;
    uint64 RegistrationEpoch_RT = 0;
};

static_assert(TIsTriviallyCopyConstructible<FVoxelSceneProxy*>::Value, "Pointer storage is trivially copyable");

const TArray<FVoxelSceneProxy*, TInlineAllocator<64>>& GetVoxelProxies_RenderThread();
void ClearVoxelProxies_RenderThread();
bool IsVoxelProxyActive_RenderThread(const FVoxelSceneProxy* Proxy);
