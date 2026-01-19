#include "Rendering/Voxel/VoxelRenderPass.h"

#include "CommonRenderResources.h"
#include "Rendering/Voxel/VoxelSceneProxy.h"
#include "Rendering/Voxel/VoxelRenderResources.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "PipelineStateCache.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "RHI.h"
#include "SceneView.h"
#include "RendererInterface.h"
#include "RenderGraphBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Math/IntVector.h"
#include "Logging/LogMacros.h"

static TAutoConsoleVariable<int32> CVarVoxelDebug(
    TEXT("r.Voxel.Debug"),
    0,
    TEXT("Enable voxel debug render pass (0=off, 1=on)"),
    ECVF_Default);

class FVoxelMeshVS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FVoxelMeshVS);
    SHADER_USE_PARAMETER_STRUCT(FVoxelMeshVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FMatrix44f, LocalToWorld)
        SHADER_PARAMETER(FMatrix44f, WorldToClip)
    END_SHADER_PARAMETER_STRUCT()
};

class FVoxelMeshPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FVoxelMeshPS);
    SHADER_USE_PARAMETER_STRUCT(FVoxelMeshPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FLinearColor, VoxelColor)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FVoxelMeshVS, "/Voxel/VoxelMesh.usf", "VoxelMeshVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FVoxelMeshPS, "/Voxel/VoxelMesh.usf", "VoxelMeshPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FVoxelMeshPassParameters, )
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// Raymarch pass parameters - includes all textures for proper RDG resource transitions
BEGIN_SHADER_PARAMETER_STRUCT(FVoxelRaymarchPassParameters, )
    SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, SDFTex)
    SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, DensityTex)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static FVertexDeclarationRHIRef GetVoxelPositionOnlyVertexDecl()
{
    static FVertexDeclarationRHIRef Decl;
    if (!Decl.IsValid())
    {
        FVertexDeclarationElementList Elements;
        Elements.Add(FVertexElement(
            /*StreamIndex*/0,
            /*Offset*/0,
            /*Type*/VET_Float3,
            /*AttributeIndex*/0,
            /*Stride*/sizeof(FVector3f)));
        Decl = RHICreateVertexDeclaration(Elements);
    }
    return Decl;
}

static TAutoConsoleVariable<int32> CVarVoxelRaymarch(
    TEXT("r.Voxel.Raymarch"),
    1,
    TEXT("Enable voxel raymarch render pass (0=off, 1=on)"),
    ECVF_Default);

static constexpr float GVoxelOverlapMultiplier = 2.0f;

class FSplatInstancesCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSplatInstancesCS);
    SHADER_USE_PARAMETER_STRUCT(FSplatInstancesCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, NumInstances)
        SHADER_PARAMETER(FVector3f, VolumeMinLS)
        SHADER_PARAMETER(float, VoxelSizeLS)
        SHADER_PARAMETER(FIntVector, VolumeDimensions)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InstanceCenters)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>,  InstanceScales)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, DensityUAV)
        SHADER_PARAMETER(float, BaseEdgeLengthLS)
        SHADER_PARAMETER(float, OverlapMultiplier)
    END_SHADER_PARAMETER_STRUCT()
};

class FSeedCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSeedCS);
    SHADER_USE_PARAMETER_STRUCT(FSeedCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntVector, VolumeDimensions)
        SHADER_PARAMETER(float, VoxelSizeLS)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, DensityTex)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, SeedUAV)
    END_SHADER_PARAMETER_STRUCT()
};

class FJFACS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FJFACS);
    SHADER_USE_PARAMETER_STRUCT(FJFACS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntVector, VolumeDimensions)
        SHADER_PARAMETER(int32, Step)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, InSeed)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutSeed)
    END_SHADER_PARAMETER_STRUCT()
};

class FDistanceToSdfCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDistanceToSdfCS);
    SHADER_USE_PARAMETER_STRUCT(FDistanceToSdfCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntVector, VolumeDimensions)
        SHADER_PARAMETER(FVector3f, VolumeMinLS)
        SHADER_PARAMETER(float, VoxelSizeLS)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SeedTex)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, DensityTex)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, SdfUAV)
    END_SHADER_PARAMETER_STRUCT()
};

// ========= Raymarch pixel shader =========

class FRaymarchFullscreenVS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FRaymarchFullscreenVS);
    SHADER_USE_PARAMETER_STRUCT(FRaymarchFullscreenVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    END_SHADER_PARAMETER_STRUCT()
};

class FRaymarchPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FRaymarchPS);
    SHADER_USE_PARAMETER_STRUCT(FRaymarchPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector3f, VolumeMinLS)
        SHADER_PARAMETER(FVector3f, VolumeMaxLS)
        SHADER_PARAMETER(float, VoxelSizeLS)
        SHADER_PARAMETER(FMatrix44f, LocalToWorld)
        SHADER_PARAMETER(FMatrix44f, WorldToLocal)
        SHADER_PARAMETER(FMatrix44f, InvViewProj)
        SHADER_PARAMETER(FMatrix44f, ViewProj)
        SHADER_PARAMETER(FVector3f, CameraWorldPos)
        SHADER_PARAMETER(FVector2f, ViewportInvSize)
        SHADER_PARAMETER(FVector2f, ViewportMin)
        SHADER_PARAMETER(FIntVector, VolumeDims)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, SDFTex)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, DensityTex)
        SHADER_PARAMETER_SAMPLER(SamplerState, SDFSampler)
    END_SHADER_PARAMETER_STRUCT()
};

// ComputeShaders
IMPLEMENT_GLOBAL_SHADER(FSplatInstancesCS, "/Voxel/VoxelDensity.usf",       "SplatInstancesCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSeedCS,           "/Voxel/VoxelDistanceField.usf", "SeedCS",           SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FJFACS,            "/Voxel/VoxelDistanceField.usf", "JfaCS",            SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDistanceToSdfCS,  "/Voxel/VoxelDistanceField.usf", "DistanceToSdfCS",  SF_Compute);

// RaymarchShaders
IMPLEMENT_GLOBAL_SHADER(FRaymarchFullscreenVS,  "/Voxel/VoxelRaymarch.usf", "FullscreenVS",     SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FRaymarchPS,            "/Voxel/VoxelRaymarch.usf", "RaymarchPS",       SF_Pixel);

static FIntVector DivideCeil3D(const FIntVector& VolumeDimensions, int32 GroupSize)
{
    auto CeilDiv = [GroupSize](int32 v){ return (v + GroupSize - 1) / GroupSize; };
    return FIntVector(CeilDiv(VolumeDimensions.X), CeilDiv(VolumeDimensions.Y), CeilDiv(VolumeDimensions.Z));
}

static void AddSplatInstancesPass(
    FRDGBuilder& GraphBuilder,
    const FVoxelRenderResource& Resource,
    FRDGTextureRef DensityTex,
    const FIntVector& VolumeDimensions,
    const FVector3f& VolumeMinLS,
    float VoxelSizeLS)
{
    const uint32 NumInstances = Resource.Centers.Num();
    TArray<FVector4f> PackedCenters; PackedCenters.Reserve(NumInstances);
    for (uint32 i = 0; i < NumInstances; ++i)
    {
        const FVector3f C = Resource.Centers.IsValidIndex(i) ? Resource.Centers[i] : FVector3f::ZeroVector;
        PackedCenters.Add(FVector4f(C, 1.0f));
    }
    FRDGBufferRef InstanceBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Voxel.InstanceCenters"), sizeof(FVector4f), PackedCenters.Num(), PackedCenters.GetData(), PackedCenters.Num() * sizeof(FVector4f));
    FRDGBufferSRVRef InstanceSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InstanceBuffer));

    TArray<float> PackedScales; PackedScales.Reserve(NumInstances);
    for (uint32 i = 0; i < NumInstances; ++i)
    {
        const float S = Resource.Scales.IsValidIndex(i) ? Resource.Scales[i] : 1.0f;
        PackedScales.Add(S);
    }
    
    FRDGBufferRef ScalesBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Voxel.InstanceScales"), sizeof(float), PackedScales.Num(), PackedScales.GetData(), PackedScales.Num() * sizeof(float));
    FRDGBufferSRVRef ScalesSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ScalesBuffer));

    TShaderMapRef<FSplatInstancesCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    auto* Params = GraphBuilder.AllocParameters<FSplatInstancesCS::FParameters>();
    Params->NumInstances     = NumInstances;
    Params->VolumeMinLS      = VolumeMinLS;
    Params->VoxelSizeLS      = VoxelSizeLS;
    Params->VolumeDimensions = VolumeDimensions;
    Params->InstanceCenters  = InstanceSRV;
    Params->InstanceScales   = ScalesSRV;
    Params->DensityUAV       = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DensityTex, 0));
    Params->BaseEdgeLengthLS = VoxelSizeLS;
    Params->OverlapMultiplier = GVoxelOverlapMultiplier;

    const uint32 GroupSize = 64u;
    const uint32 GroupsX   = (NumInstances + GroupSize - 1u) / GroupSize;
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Voxel.SplatInstances"), ERDGPassFlags::Compute, CS, Params, FIntVector(GroupsX, 1, 1));
}

static void AddSeedPass(FRDGBuilder& GraphBuilder, FRDGTextureRef DensityTex, FRDGTextureRef OutSeedTex, const FIntVector& VolumeDimensions, float VoxelSizeLS)
{
    TShaderMapRef<FSeedCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    auto* Params = GraphBuilder.AllocParameters<FSeedCS::FParameters>();
    Params->VolumeDimensions        = VolumeDimensions;
    Params->VoxelSizeLS            = VoxelSizeLS;
    Params->DensityTex = DensityTex;
    Params->SeedUAV    = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutSeedTex, 0));
    const FIntVector Groups = DivideCeil3D(VolumeDimensions, 8);
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Voxel.Seed"), ERDGPassFlags::Compute, CS, Params, Groups);
}

static FRDGTextureRef AddJFAPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef SeedPing, FRDGTextureRef SeedPong, const FIntVector& VolumeDimensions)
{
    int32 MaxDim = FMath::Max3(VolumeDimensions.X, VolumeDimensions.Y, VolumeDimensions.Z);
    int32 Step = 1 << (31 - FMath::CountLeadingZeros(MaxDim));
    bool bPingToPong = true;
    while (Step >= 1)
    {
        TShaderMapRef<FJFACS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        auto* Params = GraphBuilder.AllocParameters<FJFACS::FParameters>();
        Params->VolumeDimensions  = VolumeDimensions;
        Params->Step = Step;
        Params->InSeed  = bPingToPong ? SeedPing : SeedPong;
        Params->OutSeed = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(bPingToPong ? SeedPong : SeedPing, 0));
        const FIntVector Groups = DivideCeil3D(VolumeDimensions, 8);
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Voxel.JFA step=%d", Step), ERDGPassFlags::Compute, CS, Params, Groups);
        bPingToPong = !bPingToPong;
        Step >>= 1;
    }
    return bPingToPong ? SeedPing : SeedPong;
}

static void AddDistanceToSdfPass(
    FRDGBuilder& GraphBuilder,
    FRDGTextureRef InSeed,
    FRDGTextureRef OutSdf,
    const FIntVector& VolumeDimensions,
    const FVector3f& VolumeMinLS,
    float VoxelSizeLS,
    FRDGTextureRef DensityTex)
{
    TShaderMapRef<FDistanceToSdfCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    auto* Params = GraphBuilder.AllocParameters<FDistanceToSdfCS::FParameters>();
    Params->VolumeDimensions         = VolumeDimensions;
    Params->VolumeMinLS             = VolumeMinLS;
    Params->VoxelSizeLS             = VoxelSizeLS;
    Params->SeedTex                 = InSeed;
    Params->DensityTex              = DensityTex;
    Params->SdfUAV                  = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutSdf, 0));
    const FIntVector Groups = DivideCeil3D(VolumeDimensions, 8);
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Voxel.DistanceToSDF"), ERDGPassFlags::Compute, CS, Params, Groups);
}

struct FVoxelRenderTextureResult
{
    FRDGTextureRef SdfTex = nullptr;
    FRDGTextureRef DensityTex = nullptr;
    FIntVector VolumeDimensions = FIntVector::ZeroValue;
};

static FVoxelRenderTextureResult BuildVoxelRenderTextureResult(FRDGBuilder& GraphBuilder, const FVoxelRenderResource& Resource)
{
    if (!Resource.IsValid()) return FVoxelRenderTextureResult{};

    const FVector3f VolumeMinLS = Resource.VolumeMinLS;
    const FVector3f VolumeMaxLS = Resource.VolumeMaxLS;
    const float VoxelSizeLS = Resource.VoxelSizeLS;
    const FVector3f ExtentLS = (VolumeMaxLS - VolumeMinLS);
    const int32 NX = FMath::Max(1, FMath::RoundToInt(ExtentLS.X / VoxelSizeLS));
    const int32 NY = FMath::Max(1, FMath::RoundToInt(ExtentLS.Y / VoxelSizeLS));
    const int32 NZ = FMath::Max(1, FMath::RoundToInt(ExtentLS.Z / VoxelSizeLS));
    const FIntVector VolumeDimensions(NX, NY, NZ);

    FRDGTextureDesc DensityDesc = FRDGTextureDesc::Create3D(VolumeDimensions, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
    FRDGTextureDesc SeedDesc    = FRDGTextureDesc::Create3D(VolumeDimensions, PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
    FRDGTextureDesc SdfDesc     = FRDGTextureDesc::Create3D(VolumeDimensions, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);

    FRDGTextureRef DensityTex = GraphBuilder.CreateTexture(DensityDesc, TEXT("Voxel.Density"));
    FRDGTextureRef SeedPing   = GraphBuilder.CreateTexture(SeedDesc,    TEXT("Voxel.SeedPing"));
    FRDGTextureRef SeedPong   = GraphBuilder.CreateTexture(SeedDesc,    TEXT("Voxel.SeedPong"));
    FRDGTextureRef SdfTex     = GraphBuilder.CreateTexture(SdfDesc,     TEXT("Voxel.SDF"));

    FRDGTextureUAVRef DensityUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DensityTex, 0));
    AddClearUAVPass(GraphBuilder, DensityUAV, 0u);
    FRDGTextureUAVRef SdfUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SdfTex, 0));
    AddClearUAVPass(GraphBuilder, SdfUAV, 0.0f);
    
    AddSplatInstancesPass(GraphBuilder, Resource, DensityTex, VolumeDimensions, VolumeMinLS, VoxelSizeLS);
    AddSeedPass(GraphBuilder, DensityTex, SeedPing, VolumeDimensions, VoxelSizeLS);
    FRDGTextureRef SeedAll = AddJFAPasses(GraphBuilder, SeedPing, SeedPong, VolumeDimensions);
    AddDistanceToSdfPass(GraphBuilder, SeedAll, SdfTex, VolumeDimensions, VolumeMinLS, VoxelSizeLS, DensityTex);

    FVoxelRenderTextureResult Outputs;
    Outputs.SdfTex = SdfTex;
    Outputs.DensityTex = DensityTex;
    Outputs.VolumeDimensions = VolumeDimensions;
    return Outputs;
}

void AddVoxelRaymarchPass(
    FRDGBuilder& GraphBuilder,
    FRDGTextureRef SceneColor,
    FRDGTextureRef SceneDepth,
    const void* OpaqueView)
{
    if (CVarVoxelRaymarch.GetValueOnAnyThread() == 0) return;
    
    const FSceneView* View = static_cast<const FSceneView*>(OpaqueView);
    if (!View) return;

    const auto& Proxies = GetVoxelProxies_RenderThread();
    for (const FVoxelSceneProxy* Proxy : Proxies)
    {
        if (!IsVoxelProxyActive_RenderThread(Proxy)) continue;
        if (!Proxy->IsShown(View)) continue;
        
        const FBoxSphereBounds Bounds = Proxy->GetBounds();
        if (!View->ViewFrustum.IntersectBox(Bounds.Origin, Bounds.BoxExtent)) continue;

        const TSharedPtr<FVoxelRenderResource>& Resource = Proxy->GetRenderResources();
        if (!Resource.IsValid()) continue;

        const FVoxelRenderTextureResult RenderResult = BuildVoxelRenderTextureResult(GraphBuilder, *Resource.Get());
        if (!RenderResult.SdfTex) continue;

        auto* PassParameters = GraphBuilder.AllocParameters<FVoxelRaymarchPassParameters>();
        PassParameters->SDFTex = RenderResult.SdfTex;
        PassParameters->DensityTex = RenderResult.DensityTex;
        PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
        PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
            SceneDepth,
            ERenderTargetLoadAction::ELoad,
            ERenderTargetLoadAction::ELoad,
            FExclusiveDepthStencil::DepthWrite_StencilNop);

        const FIntPoint SceneExtent = SceneColor->Desc.Extent;
        GraphBuilder.AddPass(
            RDG_EVENT_NAME("Voxel.RaymarchRendering"),
            PassParameters,
            ERDGPassFlags::Raster,
            [PassParameters, SceneExtent, View, Proxy, RenderResult, Resource, SceneDepth](FRHICommandListImmediate& RHICmdList)
            {
                FGraphicsPipelineStateInitializer GraphicsPSO;
                RHICmdList.ApplyCachedRenderTargets(GraphicsPSO);

                GraphicsPSO.BlendState        = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
                GraphicsPSO.RasterizerState   = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
                GraphicsPSO.DepthStencilState = TStaticDepthStencilState<true, CF_GreaterEqual>::GetRHI();
                GraphicsPSO.PrimitiveType     = PT_TriangleList;

                ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
                TShaderMapRef<FRaymarchFullscreenVS>  VS(GetGlobalShaderMap(FeatureLevel));
                TShaderMapRef<FRaymarchPS>  PS(GetGlobalShaderMap(FeatureLevel));

                GraphicsPSO.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
                GraphicsPSO.BoundShaderState.VertexShaderRHI = VS.GetVertexShader();
                GraphicsPSO.BoundShaderState.PixelShaderRHI  = PS.GetPixelShader();
                SetGraphicsPipelineState(RHICmdList, GraphicsPSO, 0);

                RHICmdList.SetViewport(0, 0, 0.0f, SceneExtent.X, SceneExtent.Y, 1.0f);
                FRaymarchFullscreenVS::FParameters VSParams;
                SetShaderParameters(RHICmdList, VS, VS.GetVertexShader(), VSParams);

                FRaymarchPS::FParameters PSParams;
                PSParams.VolumeMinLS = Resource->VolumeMinLS;
                PSParams.VolumeMaxLS = Resource->VolumeMaxLS;
                PSParams.VoxelSizeLS = Resource->VoxelSizeLS;
                const FMatrix LocalToWorld = Proxy->GetLocalToWorld();
                const FMatrix WorldToLocal = LocalToWorld.InverseFast();
                PSParams.LocalToWorld = FMatrix44f(LocalToWorld);
                PSParams.WorldToLocal = FMatrix44f(WorldToLocal);
                PSParams.InvViewProj = FMatrix44f(View->ViewMatrices.GetInvViewProjectionMatrix());
                PSParams.ViewProj    = FMatrix44f(View->ViewMatrices.GetViewProjectionMatrix());
                PSParams.CameraWorldPos = static_cast<FVector3f>(View->ViewMatrices.GetViewOrigin());
                PSParams.ViewportInvSize = FVector2f(1.0f / static_cast<float>(SceneExtent.X), 1.0f / static_cast<float>(SceneExtent.Y));
                PSParams.ViewportMin = FVector2f(0.0f, 0.0f);
                PSParams.VolumeDims = RenderResult.VolumeDimensions;
                PSParams.SDFTex = RenderResult.SdfTex;
                PSParams.DensityTex = RenderResult.DensityTex;
                PSParams.SDFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
                SetShaderParameters(RHICmdList, PS, PS.GetPixelShader(), PSParams);

                RHICmdList.DrawPrimitive(0, 1, 1);
            });
    }
}

void AddVoxelDebugRenderPass(
    FRDGBuilder& GraphBuilder,
    FRDGTextureRef SceneColor,
    FRDGTextureRef SceneDepth,
    const void* OpaqueView)
{
    if (CVarVoxelDebug.GetValueOnAnyThread() == 0) return;
    
    auto* PassParameters = GraphBuilder.AllocParameters<FVoxelMeshPassParameters>();
    PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
    PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
        SceneDepth,
        ERenderTargetLoadAction::ELoad,
        ERenderTargetLoadAction::ELoad,
        FExclusiveDepthStencil::DepthRead_StencilNop);

    const FIntPoint SceneExtent = SceneColor->Desc.Extent;
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("VoxelDebugRenderPass"),
        PassParameters,
        ERDGPassFlags::Raster,
        [PassParameters, SceneExtent, OpaqueView](FRHICommandListImmediate& RHICmdList)
        {
            FGraphicsPipelineStateInitializer GraphicsPSO;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSO);

            GraphicsPSO.BlendState        = TStaticBlendState<>::GetRHI();
            GraphicsPSO.RasterizerState   = TStaticRasterizerState<FM_Wireframe, CM_None>::GetRHI();
            GraphicsPSO.DepthStencilState = TStaticDepthStencilState<false, CF_GreaterEqual>::GetRHI();
            GraphicsPSO.PrimitiveType     = PT_TriangleList;

            ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
            TShaderMapRef<FVoxelMeshVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
            TShaderMapRef<FVoxelMeshPS> PixelShader(GetGlobalShaderMap(FeatureLevel));

            GraphicsPSO.BoundShaderState.VertexDeclarationRHI = GetVoxelPositionOnlyVertexDecl();
            GraphicsPSO.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSO.BoundShaderState.PixelShaderRHI  = PixelShader.GetPixelShader();

            SetGraphicsPipelineState(RHICmdList, GraphicsPSO, 0);

            RHICmdList.SetViewport(0, 0, 0.0f, SceneExtent.X, SceneExtent.Y, 1.0f);

            const FSceneView* View = static_cast<const FSceneView*>(OpaqueView);
            if (!View)
            {
                return;
            }
            const auto& Proxies = GetVoxelProxies_RenderThread();
            for (const FVoxelSceneProxy* Proxy : Proxies)
            {
                if (!IsVoxelProxyActive_RenderThread(Proxy)) continue;
                if (!Proxy->IsShown(View)) continue;
                
                const FBoxSphereBounds Bounds = Proxy->GetBounds();
                if (!View->ViewFrustum.IntersectBox(Bounds.Origin, Bounds.BoxExtent)) continue;
                
                const FDebugMeshRHI& Mesh = Proxy->GetDebugMesh();
                if (!Mesh.IsValid()) continue;

                RHICmdList.SetStreamSource(0, Mesh.VertexBuffer.GetReference(), 0);
                FVoxelMeshVS::FParameters VSParams;
                VSParams.LocalToWorld = FMatrix44f(Proxy->GetInstanceTransform());
                VSParams.WorldToClip  = FMatrix44f(View->ViewMatrices.GetViewProjectionMatrix());
                SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParams);

                FVoxelMeshPS::FParameters PSParams;
                PSParams.VoxelColor = FLinearColor::Red;
                SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParams);

                RHICmdList.DrawIndexedPrimitive(
                    Mesh.IndexBuffer,
                    /*BaseVertexIndex=*/0,
                    /*FirstInstance=*/0,
                    Mesh.NumVertices,
                    /*StartIndex=*/0,
                    Mesh.NumIndices / 3,
                    /*NumInstances=*/1);
            }
        });
}
