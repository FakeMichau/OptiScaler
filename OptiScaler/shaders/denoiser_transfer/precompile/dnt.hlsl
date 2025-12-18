cbuffer Params : register(b0)
{
    int depthNonLinear;
    int depthInverted;
    float cameraFar;
    float cameraNear;
};

Texture2D<float> DepthInput : register(t0);
RWTexture2D<float> LinearDepthOutput : register(u0);

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    float depth = DepthInput.Load(int3(DTid.xy, 0)).x;
    
    if (depthNonLinear == 0)
    {
        LinearDepthOutput[DTid.xy] = depth;
        return;
    }
    
    float linearDepth = 0.0f;
    
    if (depthInverted > 0)
        linearDepth = (cameraNear * cameraFar) / (cameraFar - depth * (cameraFar - cameraNear));
    else
        linearDepth = (cameraNear * cameraFar) / (cameraNear + depth * (cameraFar - cameraNear));
    
    LinearDepthOutput[DTid.xy] = (linearDepth - cameraNear) / (cameraFar - cameraNear);
}