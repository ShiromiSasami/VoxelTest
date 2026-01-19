#include "Rendering/Voxel/VoxelSceneProxy.h"
#include "Rendering/Voxel/VoxelRenderComponent.h"
#include "MeshElementCollector.h"
#include "SceneView.h"
#include "Materials/Material.h"
#include "SceneManagement.h"
#include "RHI.h"
#include "RHIResources.h"

// Static member definition (single global cache)
TArray<FVoxelSceneProxy*, TInlineAllocator<64>> FVoxelSceneProxy::GProxies_RT;
uint64 FVoxelSceneProxy::GCurrentEpoch_RT = 0;
uint64 FVoxelSceneProxy::GActiveEpoch_RT = 0;

void FVoxelSceneProxy::RegisterProxy_RenderThread(FVoxelSceneProxy* Proxy)
{
    check(IsInRenderingThread());
    Proxy->RegistrationEpoch_RT = GCurrentEpoch_RT;
    GProxies_RT.Add(Proxy);
}

void FVoxelSceneProxy::UnregisterProxy_RenderThread(FVoxelSceneProxy* Proxy)
{
    check(IsInRenderingThread());
    GProxies_RT.RemoveSwap(Proxy);
}

const TArray<FVoxelSceneProxy*, TInlineAllocator<64>>& FVoxelSceneProxy::GetProxies_RenderThread()
{
    check(IsInRenderingThread());
    return GProxies_RT;
}

void FVoxelSceneProxy::ClearProxies_RenderThread()
{
    check(IsInRenderingThread());
    GProxies_RT.Reset();
}

void FVoxelSceneProxy::BeginNewEpoch_RenderThread()
{
    check(IsInRenderingThread());
    ++GCurrentEpoch_RT;
    GActiveEpoch_RT = GCurrentEpoch_RT;
}

void FVoxelSceneProxy::ClearEpochProxies_RenderThread(uint64 Epoch)
{
    check(IsInRenderingThread());
    for (int32 i = GProxies_RT.Num() - 1; i >= 0; --i)
    {
        FVoxelSceneProxy* Proxy = GProxies_RT[i];
        if (Proxy && Proxy->RegistrationEpoch_RT == Epoch)
        {
            GProxies_RT.RemoveAtSwap(i);
        }
    }
}

void FVoxelSceneProxy::ClearCurrentEpochProxies_RenderThread()
{
    check(IsInRenderingThread());
    ClearEpochProxies_RenderThread(GCurrentEpoch_RT);
}

void FVoxelSceneProxy::SetActiveEpoch_RenderThread(uint64 Epoch)
{
    check(IsInRenderingThread());
    GActiveEpoch_RT = Epoch;
}

uint64 FVoxelSceneProxy::GetActiveEpoch_RenderThread()
{
    check(IsInRenderingThread());
    return GActiveEpoch_RT;
}

const TArray<FVoxelSceneProxy*, TInlineAllocator<64>>& GetVoxelProxies_RenderThread()
{
    return FVoxelSceneProxy::GetProxies_RenderThread();
}

void ClearVoxelProxies_RenderThread()
{
    check(IsInRenderingThread());
    const auto& Proxies = FVoxelSceneProxy::GetProxies_RenderThread();
    for (const FVoxelSceneProxy* Proxy : Proxies)
    {
        if (!Proxy) continue;
    }
    FVoxelSceneProxy::ClearProxies_RenderThread();
}

bool IsVoxelProxyActive_RenderThread(const FVoxelSceneProxy* Proxy)
{
    check(IsInRenderingThread());
    if (!Proxy) return false;
    return Proxy->GetRegistrationEpoch_RenderThread() == FVoxelSceneProxy::GetActiveEpoch_RenderThread();
}

FVoxelSceneProxy::FVoxelSceneProxy(const UVoxelRenderComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent)
{
    VolumeRenderResources = InComponent->GetSharedRenderResources();

    TArray<FVector3f> CentersCopy;
    TArray<float>     ScalesCopy;
    if (VolumeRenderResources.IsValid())
    {
        CentersCopy = VolumeRenderResources->Centers;
        ScalesCopy  = VolumeRenderResources->Scales;
    }

    ENQUEUE_RENDER_COMMAND(RegisterVoxelProxyCmd)(
        [This = this, CentersCopy = MoveTemp(CentersCopy), ScalesCopy = MoveTemp(ScalesCopy)](FRHICommandListImmediate& RHICmdList)
        {
            RegisterProxy_RenderThread(This);
            if (CentersCopy.Num() > 0)
            {
                This->BuildDebugMesh_RenderThread(RHICmdList, CentersCopy, ScalesCopy);
            }
        });
}

FVoxelSceneProxy::~FVoxelSceneProxy()
{
    if (IsInRenderingThread())
    {
        UnregisterProxy_RenderThread(this);
        DebugMesh.Reset();
        VolumeRenderResources.Reset();
    }
    else
    {
        FDebugMeshRHI DebugMeshCopy = DebugMesh;
        DebugMesh.Reset();
        TSharedPtr<FVoxelRenderResource> ResourcesCopy = MoveTemp(VolumeRenderResources);
        VolumeRenderResources.Reset();

        FVoxelSceneProxy* ProxyPtr = this;
        ENQUEUE_RENDER_COMMAND(UnregisterVoxelProxyCmd)(
            [ProxyPtr, DebugMeshCopy = MoveTemp(DebugMeshCopy), ResourcesCopy](FRHICommandListImmediate&) mutable
            {
                if (ProxyPtr)
                {
                    UnregisterProxy_RenderThread(ProxyPtr);
                }
                DebugMeshCopy.Reset();
            });
    }
}

FPrimitiveViewRelevance FVoxelSceneProxy::GetViewRelevance(const FSceneView* View) const
{
    FPrimitiveViewRelevance Result;
    Result.bDrawRelevance    = IsShown(View);
    Result.bDynamicRelevance = false;
    Result.bRenderInMainPass = false;
    Result.bOpaque           = true;
    return Result;
}

void FVoxelSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                              const FSceneViewFamily& ViewFamily,
                                              uint32 VisibilityMap,
                                              FMeshElementCollector& Collector) const
{
}

void FVoxelSceneProxy::RebuildDebugMesh_RenderThread(FRHICommandListImmediate& RHICmdList, const FVoxelRenderResource& Resource)
{
    check(IsInRenderingThread());
    if (Resource.Centers.Num() == 0)
    {
        DebugMesh.Reset();
        return;
    }
    BuildDebugMesh_RenderThread(RHICmdList, Resource.Centers, Resource.Scales);
}

void FVoxelSceneProxy::BuildDebugMesh_RenderThread(
    FRHICommandListImmediate& RHICmdList,
    const TArray<FVector3f>& Centers,
    const TArray<float>& Scales)
{
    check(IsInRenderingThread());

    if (!VolumeRenderResources.IsValid())
    {
        DebugMesh.Reset();
        return;
    }

    TArray<FVector3f> Positions;
    TArray<uint32>    Indices;
    Positions.Reserve(Centers.Num() * 8);
    Indices.Reserve(Centers.Num() * 36);

    auto AppendCube = [](float Half, const FVector3f& C, TArray<FVector3f>& P, TArray<uint32>& I)
    {
        const FVector3f V[] = {
            C + FVector3f(-Half,-Half,-Half), C + FVector3f(+Half,-Half,-Half), C + FVector3f(+Half,+Half,-Half), C + FVector3f(-Half,+Half,-Half),
            C + FVector3f(-Half,-Half,+Half), C + FVector3f(+Half,-Half,+Half), C + FVector3f(+Half,+Half,+Half), C + FVector3f(-Half,+Half,+Half)
        };
        const uint32 Base = static_cast<uint32>(P.Num());
        P.Append(V, UE_ARRAY_COUNT(V));
        const uint32 Idx[] = {
            0,1,2,  0,2,3,
            4,6,5,  4,7,6,
            4,5,1,  4,1,0,
            7,3,2,  7,2,6,
            4,0,3,  4,3,7,
            5,6,2,  5,2,1
        };
        for (uint32 k = 0; k < UE_ARRAY_COUNT(Idx); ++k) { I.Add(Base + Idx[k]); }
    };

    for (int32 i = 0; i < Centers.Num(); ++i)
    {
        const float Edge = Scales.IsValidIndex(i) ? Scales[i] * VolumeRenderResources->VoxelSizeLS: VolumeRenderResources->VoxelSizeLS;
        const float Half = Edge * 0.5f;
        AppendCube(Half, Centers[i], Positions, Indices);
    }

    DebugMesh.Reset();
    const uint32 VBSize = Positions.Num() * sizeof(FVector3f);
    const uint32 IBSize = Indices.Num() * sizeof(uint32);
    if (VBSize == 0 || IBSize == 0)
    {
        return;
    }

    FRHIBufferCreateDesc VDesc = FRHIBufferCreateDesc::CreateVertex(TEXT("VoxelProxyPosVB"), VBSize).DetermineInitialState();
    DebugMesh.VertexBuffer = RHICmdList.CreateBuffer(VDesc);
    if (DebugMesh.VertexBuffer.IsValid())
    {
        void* VData = RHICmdList.LockBuffer(DebugMesh.VertexBuffer, 0, VBSize, RLM_WriteOnly);
        FMemory::Memcpy(VData, Positions.GetData(), VBSize);
        RHICmdList.UnlockBuffer(DebugMesh.VertexBuffer);
    }

    FRHIBufferCreateDesc IDesc = FRHIBufferCreateDesc::CreateIndex(TEXT("VoxelProxyIB"), IBSize, sizeof(uint32)).DetermineInitialState();
    DebugMesh.IndexBuffer = RHICmdList.CreateBuffer(IDesc);
    if (DebugMesh.IndexBuffer.IsValid())
    {
        void* IData = RHICmdList.LockBuffer(DebugMesh.IndexBuffer, 0, IBSize, RLM_WriteOnly);
        FMemory::Memcpy(IData, Indices.GetData(), IBSize);
        RHICmdList.UnlockBuffer(DebugMesh.IndexBuffer);
    }

    DebugMesh.NumVertices = Positions.Num();
    DebugMesh.NumIndices  = Indices.Num();
}
