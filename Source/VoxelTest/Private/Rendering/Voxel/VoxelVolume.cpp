#include "Rendering/Voxel/VoxelVolume.h"
#include "Rendering/Voxel/VoxelSceneProxy.h"
#include "RHI.h"
#include "RHICommandList.h"

static void AddVoxelInstance(const FVector3f& Center, float DefaultScale,
                             TArray<FVector3f>& OutCenters,
                             TArray<float>& OutScales)
{
    OutCenters.Add(Center);
    OutScales.Add(DefaultScale);
}

static void InitInstances_RenderThread(
    FVoxelRenderResource& Out,
    const TArray<FVector3f>& Centers,
    const TArray<float>& Scales,
    FRHICommandListImmediate& RHICmdList)
{
    Out.ReleaseAll();
    Out.InitializeInstances(Centers, Scales);
}

void UVoxelVolume::BuildVoxelGrid(const FVector& RegionSize, float BlockSize)
{
    if (BlockSize <= 0.f)
    {
        return;
    }
    if (!RenderResources.IsValid())
    {
        RenderResources = MakeShared<FVoxelRenderResource>();
    }

    TArray<FVector3f>  CentersArr;
    TArray<float>      ScalesArr;

    const FVector3f Region = (FVector3f)RegionSize;

    const float Half = BlockSize * 0.5f;
    const int32 NX = FMath::Max(1, FMath::FloorToInt(Region.X / BlockSize));
    const int32 NY = FMath::Max(1, FMath::FloorToInt(Region.Y / BlockSize));
    const int32 NZ = FMath::Max(1, FMath::FloorToInt(Region.Z / BlockSize));
    const float PackedLenX = NX * BlockSize;
    const float PackedLenY = NY * BlockSize;
    const float PackedLenZ = NZ * BlockSize;
    const FVector3f PackedHalf(PackedLenX * 0.5f, PackedLenY * 0.5f, PackedLenZ * 0.5f);

    const FVector3f VolumeMinLS = -PackedHalf;
    const FVector3f VolumeMaxLS =  PackedHalf;

    const FVector3f Start(
        -PackedLenX * 0.5f + Half,
        -PackedLenY * 0.5f + Half,
        -PackedLenZ * 0.5f + Half);

    CentersArr.Reserve(NX * NY * NZ);
    ScalesArr.Reserve(NX * NY * NZ);

    for (int32 ix = 0; ix < NX; ++ix){
        for (int32 iy = 0; iy < NY; ++iy){
            for (int32 iz = 0; iz < NZ; ++iz){
                const FVector3f Center = Start + FVector3f(ix * BlockSize, iy * BlockSize, iz * BlockSize);
                // スケールは正規化（0..1）。初期値は全て1に設定
                AddVoxelInstance(Center, 1.0f, CentersArr, ScalesArr);
            }
        }
    }

    RenderResources->VolumeMinLS = VolumeMinLS;
    RenderResources->VolumeMaxLS = VolumeMaxLS;
    RenderResources->VoxelSizeLS = BlockSize;

    // Cache base arrays on GT for runtime animation
    BaseCenters_GT = CentersArr;
    BaseScales_GT  = ScalesArr;

    ENQUEUE_RENDER_COMMAND(InitVoxelVolumeGridBuffersCmd)(
        [Shared = RenderResources, CentersCopy = MoveTemp(CentersArr), ScalesCopy = MoveTemp(ScalesArr)](FRHICommandListImmediate& RHICmdList)
        {
            if (!Shared.IsValid()) return;
            InitInstances_RenderThread(*Shared.Get(), CentersCopy, ScalesCopy, RHICmdList);

            const auto& Proxies = GetVoxelProxies_RenderThread();
            for (FVoxelSceneProxy* Proxy : Proxies)
            {
                if (!Proxy) continue;
                const TSharedPtr<FVoxelRenderResource>& ProxyResources = Proxy->GetRenderResources();
                if (ProxyResources.IsValid() && ProxyResources == Shared)
                {
                    Proxy->RebuildDebugMesh_RenderThread(RHICmdList, *Shared.Get());
                }
            }
        });
}

void UVoxelVolume::BeginDestroy()
{
    Super::BeginDestroy();
    if (RenderResources.IsValid())
    {
        TSharedPtr<FVoxelRenderResource> Local = RenderResources;
        ENQUEUE_RENDER_COMMAND(ReleaseVoxelVolumeBuffersCmd)(
            [Local](FRHICommandListImmediate&)
            {
                if (Local.IsValid())
                {
                    Local->ReleaseAll();
                }
            });
        RenderResources.Reset();
    }
    BaseCenters_GT.Reset();
    BaseScales_GT.Reset();
}

void UVoxelVolume::AnimateScales(float TimeSeconds, float Amplitude, float Frequency)
{
    if (!RenderResources.IsValid()) return;
    if (BaseScales_GT.Num() == 0) return;
    const int32 N = BaseScales_GT.Num();
    TArray<float> NewScales; NewScales.SetNumUninitialized(N);
    const float TwoPiF = 6.28318530718f * Frequency;
    for (int32 i = 0; i < N; ++i)
    {
        const float s0 = BaseScales_GT[i];
        const float phase = (float)i * 0.13f; // simple per-index phase
        // 0..1 の正規化スケール（中心0.5、振幅0.5）
        const float t01 = 0.5f + 0.5f * FMath::Sin(TwoPiF * TimeSeconds + phase);
        NewScales[i] = s0 * t01;
    }
    // Render threadに安全に反映
    TSharedPtr<FVoxelRenderResource> Shared = RenderResources;
    ENQUEUE_RENDER_COMMAND(UpdateVoxelScalesCmd)(
        [Shared, NewScales = MoveTemp(NewScales)](FRHICommandListImmediate&)
        {
            if (Shared.IsValid())
            {
                Shared->Scales = NewScales;
            }
        });
}

void UVoxelVolume::AnimateCenters(float TimeSeconds, float Amplitude, float Frequency)
{
    if (!RenderResources.IsValid()) return;
    if (BaseCenters_GT.Num() == 0) return;
    const int32 N = BaseCenters_GT.Num();
    TArray<FVector3f> NewCenters; NewCenters.SetNumUninitialized(N);
    const float TwoPiF = 6.28318530718f * Frequency;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector3f c0 = BaseCenters_GT[i];
        const float phase = (float)i * 0.19f;
        const float w = TwoPiF * TimeSeconds + phase;
        // small Lissajous offset per cell (kept modest to avoid exiting volume)
        const FVector3f offset(
            Amplitude * FMath::Sin(w),
            Amplitude * FMath::Sin(1.37f * w + 0.5f),
            Amplitude * FMath::Sin(1.91f * w + 1.0f));
        NewCenters[i] = c0 + offset;
    }
    // Render threadに安全に反映
    TSharedPtr<FVoxelRenderResource> Shared = RenderResources;
    ENQUEUE_RENDER_COMMAND(UpdateVoxelCentersCmd)(
        [Shared, NewCenters = MoveTemp(NewCenters)](FRHICommandListImmediate&)
        {
            if (Shared.IsValid())
            {
                Shared->Centers = NewCenters;
            }
        });
}
