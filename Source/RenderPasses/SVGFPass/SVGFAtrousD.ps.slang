import SVGFAtrousC;
import SVGFCommon;

cbuffer PerImageCB_D
{
    RWByteAddressBuffer drIllumination;

    RWByteAddressBuffer daSigmaL;
    RWByteAddressBuffer daSigmaZ;
    RWByteAddressBuffer daSigmaN;

    RWByteAddressBuffer daKernel;
    RWByteAddressBuffer daVarianceKernel;
    RWByteAddressBuffer daLuminanceParams;
    RWByteAddressBuffer daWeightFunctionParams;

};

void main(FullScreenPassVsOut vsOut)
{
    var dpKernel = diffPair(dvKernel, {0.0, 0.0, 0.0});
    var dpVarianceKernel = diffPair(dvVarianceKernel, {{0.0, 0.0}, {0.0, 0.0}});
    var dpLuminanceParams =  diffPair(dvLuminanceParams, float3(0.0));
    var dpWeightFunctionParams = diffPair(dvWeightFunctionParams, {0.0, 0.0, 0.0});
    var dpSigmaL = diffPair(dvSigmaL, 0.0);
    var dpSigmaZ = diffPair(dvSigmaZ, 0.0);
    var dpSigmaN = diffPair(dvSigmaN, 0.0);
    
    const int2 ipos = int2(vsOut.posH.xy);

    __bwd_diff(atrous)(vsOut, dpKernel, dpVarianceKernel, dpLuminanceParams, dpWeightFunctionParams, dpSigmaL, dpSigmaZ, dpSigmaN, readDerivBuf4(drIllumination, ipos, gAlbedo));

    accumDerivBuf4(daSigmaL, ipos, float4(dpSigmaL.d), gAlbedo);
    accumDerivBuf4(daSigmaZ, ipos, float4(dpSigmaZ.d), gAlbedo);
    accumDerivBuf4(daSigmaN, ipos, float4(dpSigmaN.d), gAlbedo);

    accumDerivBuf4(daKernel, ipos, float4(dpKernel.d[0], dpKernel.d[1], dpKernel.d[2], 0.0f), gAlbedo);
    accumDerivBuf4(daVarianceKernel, ipos, float4(dpVarianceKernel.d[0][0], dpVarianceKernel.d[0][1], dpVarianceKernel.d[1][0], dpVarianceKernel.d[1][1]), gAlbedo);
    accumDerivBuf4(daLuminanceParams, ipos, float4(dpLuminanceParams.d, 0.0f), gAlbedo);
    accumDerivBuf4(daWeightFunctionParams, ipos, float4(dpWeightFunctionParams.d[0], dpWeightFunctionParams.d[1], dpWeightFunctionParams.d[2], 0.0f), gAlbedo);
}