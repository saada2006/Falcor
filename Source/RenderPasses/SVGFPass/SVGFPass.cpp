/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "SVGFPass.h"

/*
TODO:
- clean up shaders
- clean up UI: tooltips, etc.
- handle skybox pixels
- enum for fbo channel indices
*/

namespace
{
    // Shader source files
    const char kPackLinearZAndNormalShader[] = "RenderPasses/SVGFPass/SVGFPackLinearZAndNormal.ps.slang";
    const char kReprojectShader[]            = "RenderPasses/SVGFPass/SVGFReproject.ps.slang";
    const char kAtrousShader[]               = "RenderPasses/SVGFPass/SVGFAtrous.ps.slang";
    const char kFilterMomentShader[]         = "RenderPasses/SVGFPass/SVGFFilterMoments.ps.slang";

    const char kFinalModulateShaderS[]        = "RenderPasses/SVGFPass/SVGFFinalModulateS.ps.slang";
    const char kFinalModulateShaderD[]        = "RenderPasses/SVGFPass/SVGFFinalModulateD.ps.slang";

    // Names of valid entries in the parameter dictionary.
    const char kEnabled[] = "Enabled";
    const char kIterations[] = "Iterations";
    const char kFeedbackTap[] = "FeedbackTap";
    const char kVarianceEpsilon[] = "VarianceEpsilon";
    const char kPhiColor[] = "PhiColor";
    const char kPhiNormal[] = "PhiNormal";
    const char kAlpha[] = "Alpha";
    const char kMomentsAlpha[] = "MomentsAlpha";

    // Input buffer names
    const char kInputBufferAlbedo[] = "Albedo";
    const char kInputBufferColor[] = "Color";
    const char kInputBufferEmission[] = "Emission";
    const char kInputBufferWorldPosition[] = "WorldPosition";
    const char kInputBufferWorldNormal[] = "WorldNormal";
    const char kInputBufferPosNormalFwidth[] = "PositionNormalFwidth";
    const char kInputBufferLinearZ[] = "LinearZ";
    const char kInputBufferMotionVector[] = "MotionVec";

    // Internal buffer names
    const char kInternalBufferPreviousLinearZAndNormal[] = "Previous Linear Z and Packed Normal";
    const char kInternalBufferPreviousLighting[] = "Previous Lighting";
    const char kInternalBufferPreviousMoments[] = "Previous Moments";

    // Output buffer name
    const char kOutputBufferFilteredImage[] = "Filtered image";
    const char kOutputDebugBuffer[] = "DebugBuf";
    }

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SVGFPass>();
}

SVGFPass::SVGFPass(ref<Device> pDevice, const Dictionary& dict)
    : RenderPass(pDevice)
{
    for (const auto& [key, value] : dict)
    {
        if (key == kEnabled) mFilterEnabled = value;
        else if (key == kIterations) mFilterIterations = value;
        else if (key == kFeedbackTap) mFeedbackTap = value;
        else if (key == kVarianceEpsilon) mVarainceEpsilon = value;
        else if (key == kPhiColor) ;//dvSigmaL = value;
        else if (key == kPhiNormal) ;//dvSigmaN = value;
        else if (key == kAlpha) ;//dvAlpha = value;
        else if (key == kMomentsAlpha) ;//dvMomentsAlpha = value;
        else logWarning("Unknown field '{}' in SVGFPass dictionary.", key);
    }

    mpPackLinearZAndNormal = FullScreenPass::create(mpDevice, kPackLinearZAndNormalShader);
    mpReprojection = FullScreenPass::create(mpDevice, kReprojectShader);
    mpAtrous = FullScreenPass::create(mpDevice, kAtrousShader);
    mpFilterMoments = FullScreenPass::create(mpDevice, kFilterMomentShader);
    mpFinalModulate = FullScreenPass::create(mpDevice, kFinalModulateShaderS);


    mpFinalModulateD = FullScreenPass::create(mpDevice, kFinalModulateShaderD);

    mpTempDiffColor = Buffer::create(pDevice, sizeof(int32_t) * 4 * 1920 * 1080);
    mpTempDiffAlbedo = Buffer::create(pDevice, sizeof(int32_t) * 4 * 1920 * 1080);
    mpTempDiffEmission = Buffer::create(pDevice, sizeof(int32_t) * 4 * 1920 * 1080);

    FALCOR_ASSERT(mpPackLinearZAndNormal && mpReprojection && mpAtrous && mpFilterMoments && mpFinalModulate && mpTempDiffColor && mpTempDiffAlbedo && mpTempDiffEmission);

    // set common stuff first
    float3 dvLuminanceParams = float3(0.3333);

    float   dvSigmaL              = 10.0f;
    float   dvSigmaZ              = 1.0;
    float   dvSigmaN              = 128.0f;
    float   dvAlpha               = 0.05f;
    float   dvMomentsAlpha        = 0.2f;

    float dvWeightFunctionParams[3] {1.0, 1.0, 1.0};

    // set pack linear z and normal params


    // set reproj params
    mReprojectState.dvLuminanceParams = dvLuminanceParams;
    mReprojectState.dvAlpha = dvAlpha;
    mReprojectState.dvMomentsAlpha = dvMomentsAlpha;

    mReprojectState.dvParams[0] = 32.0;
    mReprojectState.dvParams[1] = 1.0;
    mReprojectState.dvParams[2] = 10.0;
    mReprojectState.dvParams[3] = 16.0;

    mReprojectState.dvKernel[0] = 1.0;
    mReprojectState.dvKernel[1] = 1.0;
    mReprojectState.dvKernel[2] = 1.0;

    // set filter moments params
    mFilterMomentsState.dvSigmaL = dvSigmaL;
    mFilterMomentsState.dvSigmaZ = dvSigmaZ;
    mFilterMomentsState.dvSigmaN = dvSigmaN;

    mFilterMomentsState.dvLuminanceParams = dvLuminanceParams;

    for (int i = 0; i < 3; i++) {
        mFilterMomentsState.dvWeightFunctionParams[i] = dvWeightFunctionParams[i];
    }

    mFilterMomentsState.dvVarianceBoostFactor = 4.0;


    // Set atrous state vars
    mAtrousState.dvSigmaL = dvSigmaL;
    mAtrousState.dvSigmaZ = dvSigmaZ;
    mAtrousState.dvSigmaN = dvSigmaN;

    for (int i = 0; i < 3; i++) {
        mAtrousState.dvWeightFunctionParams[i] = dvWeightFunctionParams[i];
    }

    mAtrousState.dvLuminanceParams = dvLuminanceParams;

    mAtrousState.dvKernel[0] = 1.0;
    mAtrousState.dvKernel[1] = 2.0f / 3.0f;
    mAtrousState.dvKernel[2] = 1.0f / 6.0f;

    mAtrousState.dvVarianceKernel[0][0] = 1.0 / 4.0;
    mAtrousState.dvVarianceKernel[0][1] = 1.0 / 8.0;
    mAtrousState.dvVarianceKernel[1][0] = 1.0 / 8.0;
    mAtrousState.dvVarianceKernel[1][1] = 1.0 / 16.0;


}

Dictionary SVGFPass::getScriptingDictionary()
{
    Dictionary dict;
    dict[kEnabled] = mFilterEnabled;
    dict[kIterations] = mFilterIterations;
    dict[kFeedbackTap] = mFeedbackTap;
    dict[kVarianceEpsilon] = mVarainceEpsilon;
    // doesn't really make sense for our use case
    dict[kPhiColor] = -1.0;
    dict[kPhiNormal] = -1.0;
    dict[kAlpha] = -1.0;
    dict[kMomentsAlpha] = -1.0;
    return dict;
}

/*
Reproject:
  - takes: motion, color, prevLighting, prevMoments, linearZ, prevLinearZ, historyLen
    returns: illumination, moments, historyLength
Variance/filter moments:
  - takes: illumination, moments, history length, normal+depth
  - returns: filtered illumination+variance (to ping pong fbo)
a-trous:
  - takes: albedo, filtered illumination+variance, normal+depth, history length
  - returns: final color
*/

RenderPassReflection SVGFPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    reflector.addInput(kInputBufferAlbedo, "Albedo");
    reflector.addInput(kInputBufferColor, "Color");
    reflector.addInput(kInputBufferEmission, "Emission");
    reflector.addInput(kInputBufferWorldPosition, "World Position");
    reflector.addInput(kInputBufferWorldNormal, "World Normal");
    reflector.addInput(kInputBufferPosNormalFwidth, "PositionNormalFwidth");
    reflector.addInput(kInputBufferLinearZ, "LinearZ");
    reflector.addInput(kInputBufferMotionVector, "Motion vectors");

    reflector.addInternal(kInternalBufferPreviousLinearZAndNormal, "Previous Linear Z and Packed Normal")
        .format(ResourceFormat::RGBA32Float)
        .bindFlags(Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource)
        ;
    reflector.addInternal(kInternalBufferPreviousLighting, "Previous Filtered Lighting")
        .format(ResourceFormat::RGBA32Float)
        .bindFlags(Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource)
        ;
    reflector.addInternal(kInternalBufferPreviousMoments, "Previous Moments")
        .format(ResourceFormat::RG32Float)
        .bindFlags(Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource)
        ;

    reflector.addOutput(kOutputBufferFilteredImage, "Filtered image").format(ResourceFormat::RGBA16Float);
    reflector.addOutput(kOutputDebugBuffer, "DebugBuf").format(ResourceFormat::RGBA32Float);

    return reflector;
}

void SVGFPass::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    allocateFbos(compileData.defaultTexDims, pRenderContext);
    mBuffersNeedClear = true;
}

void SVGFPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    ref<Texture> pAlbedoTexture = renderData.getTexture(kInputBufferAlbedo);
    ref<Texture> pColorTexture = renderData.getTexture(kInputBufferColor);
    ref<Texture> pEmissionTexture = renderData.getTexture(kInputBufferEmission);
    ref<Texture> pWorldPositionTexture = renderData.getTexture(kInputBufferWorldPosition);
    ref<Texture> pWorldNormalTexture = renderData.getTexture(kInputBufferWorldNormal);
    ref<Texture> pPosNormalFwidthTexture = renderData.getTexture(kInputBufferPosNormalFwidth);
    ref<Texture> pLinearZTexture = renderData.getTexture(kInputBufferLinearZ);
    ref<Texture> pMotionVectorTexture = renderData.getTexture(kInputBufferMotionVector);

    ref<Texture> pOutputTexture = renderData.getTexture(kOutputBufferFilteredImage);
    ref<Texture> pDebugTexture = renderData.getTexture(kOutputDebugBuffer);

    FALCOR_ASSERT(mpFilteredIlluminationFbo &&
           mpFilteredIlluminationFbo->getWidth() == pAlbedoTexture->getWidth() &&
           mpFilteredIlluminationFbo->getHeight() == pAlbedoTexture->getHeight());

    if (mBuffersNeedClear)
    {
        clearBuffers(pRenderContext, renderData);
        mBuffersNeedClear = false;
    }

    if (mFilterEnabled)
    {
        // Grab linear z and its derivative and also pack the normal into
        // the last two channels of the mpLinearZAndNormalFbo.
        computeLinearZAndNormal(pRenderContext, pLinearZTexture, pWorldNormalTexture);

        // Demodulate input color & albedo to get illumination and lerp in
        // reprojected filtered illumination from the previous frame.
        // Stores the result as well as initial moments and an updated
        // per-pixel history length in mpCurReprojFbo.
        ref<Texture> pPrevLinearZAndNormalTexture =
            renderData.getTexture(kInternalBufferPreviousLinearZAndNormal);
        computeReprojection(pRenderContext, pAlbedoTexture, pColorTexture, pEmissionTexture,
                            pMotionVectorTexture, pPosNormalFwidthTexture,
                            pPrevLinearZAndNormalTexture, pDebugTexture);

        // Do a first cross-bilateral filtering of the illumination and
        // estimate its variance, storing the result into a float4 in
        // mpPingPongFbo[0].  Takes mpCurReprojFbo as input.
        computeFilteredMoments(pRenderContext);

        pRenderContext->blit(mpPingPongFbo[0]->getColorTexture(1)->getSRV(), pDebugTexture->getRTV());

        // Filter illumination from mpCurReprojFbo[0], storing the result
        // in mpPingPongFbo[0].  Along the way (or at the end, depending on
        // the value of mFeedbackTap), save the filtered illumination for
        // next time into mpFilteredPastFbo.
        computeAtrousDecomposition(pRenderContext, pAlbedoTexture);

        // Compute albedo * filtered illumination and add emission back in.
        auto perImageCB = mpFinalModulate->getRootVar()["PerImageCB"];
        perImageCB["gAlbedo"] = pAlbedoTexture;
        perImageCB["gEmission"] = pEmissionTexture;
        perImageCB["gIllumination"] = mpPingPongFbo[0]->getColorTexture(0);
        mpFinalModulate->execute(pRenderContext, mpFinalFbo);

        // Blit into the output texture.
        pRenderContext->blit(mpFinalFbo->getColorTexture(0)->getSRV(), pOutputTexture->getRTV());

        // Swap resources so we're ready for next frame.
        std::swap(mpCurReprojFbo, mpPrevReprojFbo);
        pRenderContext->blit(mpLinearZAndNormalFbo->getColorTexture(0)->getSRV(),
                             pPrevLinearZAndNormalTexture->getRTV());

        computeDerivatives(pRenderContext, renderData);
    }
    else
    {
        pRenderContext->blit(pColorTexture->getSRV(), pOutputTexture->getRTV());
    }
}

void SVGFPass::allocateFbos(uint2 dim, RenderContext* pRenderContext)
{
    {
        // Screen-size FBOs with 3 MRTs: one that is RGBA32F, one that is
        // RG32F for the luminance moments, and one that is R16F.
        Fbo::Desc desc;
        desc.setSampleCount(0);
        desc.setColorTarget(0, Falcor::ResourceFormat::RGBA32Float); // illumination
        desc.setColorTarget(1, Falcor::ResourceFormat::RG32Float);   // moments
        desc.setColorTarget(2, Falcor::ResourceFormat::R16Float);    // history length
        desc.setColorTarget(3, Falcor::ResourceFormat::RGBA32Float);    // debug buf
        mpCurReprojFbo  = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
        mpPrevReprojFbo = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
    }

    {
        // Screen-size RGBA32F buffer for linear Z, derivative, and packed normal
        Fbo::Desc desc;
        desc.setColorTarget(0, Falcor::ResourceFormat::RGBA32Float);
        mpLinearZAndNormalFbo = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
    }

    {
        // Screen-size FBOs with 1 RGBA32F buffer
        Fbo::Desc desc;
        desc.setColorTarget(0, Falcor::ResourceFormat::RGBA32Float);
        desc.setColorTarget(1, Falcor::ResourceFormat::RGBA32Float);    // debug buf

        mpPingPongFbo[0]  = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
        mpPingPongFbo[1]  = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
        mpFilteredPastFbo = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
        mpFilteredIlluminationFbo       = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
        mpFinalFbo        = Fbo::create2D(mpDevice, dim.x, dim.y, desc);
    }

    mBuffersNeedClear = true;
}

void SVGFPass::computeDerivatives(RenderContext* pRenderContext, const RenderData& renderData)
{

}

void SVGFPass::clearBuffers(RenderContext* pRenderContext, const RenderData& renderData)
{
    pRenderContext->clearFbo(mpPingPongFbo[0].get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpPingPongFbo[1].get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpLinearZAndNormalFbo.get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpFilteredPastFbo.get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpCurReprojFbo.get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpPrevReprojFbo.get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearFbo(mpFilteredIlluminationFbo.get(), float4(0), 1.0f, 0, FboAttachmentType::All);

    pRenderContext->clearTexture(renderData.getTexture(kInternalBufferPreviousLinearZAndNormal).get());
    pRenderContext->clearTexture(renderData.getTexture(kInternalBufferPreviousLighting).get());
    pRenderContext->clearTexture(renderData.getTexture(kInternalBufferPreviousMoments).get());
}

// Extracts linear z and its derivative from the linear Z texture and packs
// the normal from the world normal texture and packes them into the FBO.
// (It's slightly wasteful to copy linear z here, but having this all
// together in a single buffer is a small simplification, since we make a
// copy of it to refer to in the next frame.)
void SVGFPass::computeLinearZAndNormal(RenderContext* pRenderContext, ref<Texture> pLinearZTexture,
                                       ref<Texture> pWorldNormalTexture)
{
    auto perImageCB = mpPackLinearZAndNormal->getRootVar()["PerImageCB"];
    perImageCB["gLinearZ"] = pLinearZTexture;
    perImageCB["gNormal"] = pWorldNormalTexture;

    mpPackLinearZAndNormal->execute(pRenderContext, mpLinearZAndNormalFbo);
}

void SVGFPass::computeReprojection(RenderContext* pRenderContext, ref<Texture> pAlbedoTexture,
                                   ref<Texture> pColorTexture, ref<Texture> pEmissionTexture,
                                   ref<Texture> pMotionVectorTexture,
                                   ref<Texture> pPositionNormalFwidthTexture,
                                   ref<Texture> pPrevLinearZTexture,
                                   ref<Texture> pDebugTexture
    )
{
    auto perImageCB = mpReprojection->getRootVar()["PerImageCB"];

    // Setup textures for our reprojection shader pass
    perImageCB["gMotion"]        = pMotionVectorTexture;
    perImageCB["gColor"]         = pColorTexture;
    perImageCB["gEmission"]      = pEmissionTexture;
    perImageCB["gAlbedo"]        = pAlbedoTexture;
    perImageCB["gPositionNormalFwidth"] = pPositionNormalFwidthTexture;
    perImageCB["gPrevIllum"]     = mpFilteredPastFbo->getColorTexture(0);
    perImageCB["gPrevMoments"]   = mpPrevReprojFbo->getColorTexture(1);
    perImageCB["gLinearZAndNormal"]       = mpLinearZAndNormalFbo->getColorTexture(0);
    perImageCB["gPrevLinearZAndNormal"]   = pPrevLinearZTexture;
    perImageCB["gPrevHistoryLength"] = mpPrevReprojFbo->getColorTexture(2);

    // Setup variables for our reprojection pass
    perImageCB["dvAlpha"] = mReprojectState.dvAlpha;
    perImageCB["dvMomentsAlpha"] = mReprojectState.dvMomentsAlpha;

    pRenderContext->clearUAV(mpTempDiffColor->getUAV().get(), Falcor::uint4(0));
    pRenderContext->clearUAV(mpTempDiffAlbedo->getUAV().get(), Falcor::uint4(0));
    pRenderContext->clearUAV(mpTempDiffEmission->getUAV().get(), Falcor::uint4(0));

    // Setup the temp diff textures
    perImageCB["d_gColor"] = mpTempDiffColor;
    perImageCB["d_gAlbedo"] = mpTempDiffAlbedo;
    perImageCB["d_gEmission"] = mpTempDiffEmission;

    perImageCB["dvLuminanceParams"] = mReprojectState.dvLuminanceParams;

    for (int i = 0; i < 3; i++) {
        perImageCB["dvReprojKernel"][i] = mReprojectState.dvKernel[i];
    }

    for (int i = 0; i < 4; i++) {
        perImageCB["dvReprojParams"][i] = mReprojectState.dvParams[i];
    }

    mpReprojection->execute(pRenderContext, mpCurReprojFbo);
}

void SVGFPass::computeFilteredMoments(RenderContext* pRenderContext)
{
    auto perImageCB = mpFilterMoments->getRootVar()["PerImageCB"];

    perImageCB["gIllumination"]     = mpCurReprojFbo->getColorTexture(0);
    perImageCB["gHistoryLength"]    = mpCurReprojFbo->getColorTexture(2);
    perImageCB["gLinearZAndNormal"]          = mpLinearZAndNormalFbo->getColorTexture(0);
    perImageCB["gMoments"]          = mpCurReprojFbo->getColorTexture(1);

    perImageCB["dvSigmaL"] = mFilterMomentsState.dvSigmaL;
    perImageCB["dvSigmaZ"] = mFilterMomentsState.dvSigmaZ;
    perImageCB["dvSigmaN"] = mFilterMomentsState.dvSigmaN;

    perImageCB["dvLuminanceParams"] =mFilterMomentsState. dvLuminanceParams;
    perImageCB["dvVarianceBoostFactor"] = mFilterMomentsState.dvVarianceBoostFactor;

    for (int i = 0; i < 3; i++) {
        perImageCB["dvWeightFunctionParams"][i] = mFilterMomentsState.dvWeightFunctionParams[i];
    }

    mpFilterMoments->execute(pRenderContext, mpPingPongFbo[0]);
}

void SVGFPass::computeAtrousDecomposition(RenderContext* pRenderContext, ref<Texture> pAlbedoTexture)
{
    auto perImageCB = mpAtrous->getRootVar()["PerImageCB"];

    perImageCB["gAlbedo"]        = pAlbedoTexture;
    perImageCB["gHistoryLength"] = mpCurReprojFbo->getColorTexture(2);
    perImageCB["gLinearZAndNormal"]       = mpLinearZAndNormalFbo->getColorTexture(0);

    perImageCB["dvSigmaL"] = mAtrousState.dvSigmaL;
    perImageCB["dvSigmaZ"] = mAtrousState.dvSigmaZ;
    perImageCB["dvSigmaN"] = mAtrousState.dvSigmaN;

    perImageCB["dvLuminanceParams"] = mAtrousState.dvLuminanceParams;

    for (int i = 0; i < 3; i++) {
        perImageCB["dvWeightFunctionParams"][i] = mAtrousState.dvWeightFunctionParams[i];
    }

    for (int i = 0; i < 3; i++) {
        perImageCB["mAtrousState.dvKernel"][i] = mAtrousState.dvKernel[i];
    }

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            perImageCB["mAtrousState.dvVarianceKernel"][i][j] = mAtrousState.dvVarianceKernel[i][j];
        }
    }

    for (int i = 0; i < mFilterIterations; i++)
    {
        ref<Fbo> curTargetFbo = mpPingPongFbo[1];

        perImageCB["gIllumination"] = mpPingPongFbo[0]->getColorTexture(0);
        perImageCB["gStepSize"] = 1 << i;

        mpAtrous->execute(pRenderContext, curTargetFbo);

        // store the filtered color for the feedback path
        if (i == std::min(mFeedbackTap, mFilterIterations - 1))
        {
            pRenderContext->blit(curTargetFbo->getColorTexture(0)->getSRV(), mpFilteredPastFbo->getRenderTargetView(0));
        }

        std::swap(mpPingPongFbo[0], mpPingPongFbo[1]);
    }

    if (mFeedbackTap < 0)
    {
        pRenderContext->blit(mpCurReprojFbo->getColorTexture(0)->getSRV(), mpFilteredPastFbo->getRenderTargetView(0));
    }
}

void SVGFPass::renderUI(Gui::Widgets& widget)
{
    int dirty = 0;
    dirty |= (int)widget.checkbox("Enable SVGF", mFilterEnabled);

    widget.text("");
    widget.text("Number of filter iterations.  Which");
    widget.text("    iteration feeds into future frames?");
    dirty |= (int)widget.var("Iterations", mFilterIterations, 2, 10, 1);
    dirty |= (int)widget.var("Feedback", mFeedbackTap, -1, mFilterIterations - 2, 1);

    widget.text("");
    widget.text("Contol edge stopping on bilateral fitler");
    dirty |= (int)widget.var("For Color", mAtrousState.dvSigmaL, 0.0f, 10000.0f, 0.01f);  // pass in sigma l as dummy var
    dirty |= (int)widget.var("For Normal", mAtrousState.dvSigmaL, 0.001f, 1000.0f, 0.2f);

    widget.text("");
    widget.text("How much history should be used?");
    widget.text("    (alpha; 0 = full reuse; 1 = no reuse)");
    dirty |= (int)widget.var("Alpha", mAtrousState.dvSigmaL, 0.0f, 1.0f, 0.001f);
    dirty |= (int)widget.var("Moments Alpha", mAtrousState.dvSigmaL, 0.0f, 1.0f, 0.001f);

    if (dirty) mBuffersNeedClear = true;
}
