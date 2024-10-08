#include "SVGFTransformer.h"

SVGFTransformer::SVGFTransformer(ref<Device> pDevice, ref<SVGFUtilitySet> pUtilities, ref<FilterParameterReflector> pParameterReflector)
    : mpDevice(pDevice), mpUtilities(pUtilities), mpParameterReflector(pParameterReflector)
{
    mpEvaluatePass = mpUtilities->createComputePassAndDumpIR(kTransformerShaderS);
    mpBackPropagatePass = mpUtilities->createComputePassAndDumpIR(kTransformerShaderD);

    // create some test stuff
    mpTestIllum = mpDevice->createTexture2D(kMapDim, kMapDim, ResourceFormat::RGBA32Float, 1, 1);
    mpTestNormalDepth = mpDevice->createTexture2D(kMapDim, kMapDim, ResourceFormat::RGBA32Float, 1, 1);
    mpTestOutput = mpDevice->createTexture2D(
        kMapDim, kMapDim, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < kNumFeatures; j++)
        {
            for (int k = 0; k < kNumFeatures; k++)
            {
                mWeights.dv[i].weights[j][k] = (j == k ? 1.0 : 0.0);
            }
        }
    }
}
void SVGFTransformer::allocateFbos(uint2 dim, RenderContext* pRenderContext)
{

}

void SVGFTransformer::computeEvaluation(RenderContext* pRenderContext, SVGFRenderData& svgfrd, bool updateInternalBuffers)
{

    set_and_update_test_data(pRenderContext);

    auto perImageCB = mpEvaluatePass->getRootVar()["PerImageCB"];
    set_common_parameters(perImageCB);
    //mpUtilities->setPatchingState(mpEvaluatePass);

    //mpPixelDebug->beginFrame(pRenderContext, uint2(kMapDim, kMapDim));
    //mpPixelDebug->prepareProgram(mpEvaluatePass->getProgram(), mpEvaluatePass->getRootVar());
    mpEvaluatePass->execute(pRenderContext, uint3(1, 1, 25));
    //mpPixelDebug->endFrame(pRenderContext);

    std::cout << "GPU Test Result:\n";

    auto outputBitmap = pRenderContext->readTextureSubresource(mpTestOutput.get(), 0);
    float4(*filteredImage)[kMapDim] = (float4(*)[kMapDim])outputBitmap.data(); // uh super weird syntax I do not understand
    print_test_result(filteredImage);
}

void SVGFTransformer::computeBackPropagation(RenderContext* pRenderContext, SVGFRenderData& svgfrd)
{

}

void SVGFTransformer::set_common_parameters(ShaderVar& perImageCB)
{
    /*
        WeightMatrix weights[3];

    texture2D gIllumination;
    RWTexture2D<float4> gFiltered;

    */


    perImageCB["weights"].setBlob(mWeights.dv);
    perImageCB["gIllumination"] = mpTestIllum;
    perImageCB["gFiltered"] = mpTestOutput;
}

void SVGFTransformer::set_and_update_test_data(RenderContext* pRenderContext)
{
    for (int y = 0; y < kMapDim; y++)
    {
        for (int x = 0; x < kMapDim; x++)
        {
            mTestIllumData[y][x] = tempTestIllumData[y][x];
            mTestNormalData[y][x] = float4(0.0f);
        }
    }
    pRenderContext->updateTextureData(mpTestIllum.get(), (const void*)mTestIllumData);
    pRenderContext->updateTextureData(mpTestNormalDepth.get(), (const void*)mTestNormalData);
}

void SVGFTransformer::print_test_result(float4 grid[][kMapDim]) {
    for (int y = -1; y < kMapDim; y++)
    {
        for (int x = -1; x < kMapDim; x++)
        {
            if (x == -1 && y == -1)
            {
                std::cout << "y/x\t";
            }
            else if (y == -1)
            {
                std::cout << x << "\t";
            }
            else if (x == -1)
            {
                std::cout << y << "\t";
            }
            else
            {
                std::cout << grid[y][x].r << "," << grid[y][x].g << "\t";
            }
        }
        std::cout << "\n";
    }
    std::cout.flush();
}