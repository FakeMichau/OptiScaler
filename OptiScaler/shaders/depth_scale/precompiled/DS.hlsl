cbuffer Params : register(b0)
{
    int depthInverted;
    float cameraFar;
    float cameraNear;
};

// Input texture
Texture2D<float> SourceTexture : register(t0);

// Output texture
RWTexture2D<float> DestinationTexture : register(u0);

// Compute shader thread group size
[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelId = DTid.xy;

    float linearDepth = SourceTexture.Load(int3(pixelId, 0));
    
    if (depthInverted)
    {
        DestinationTexture[pixelId] = saturate(
            (cameraFar / linearDepth - 1.0f) / 
            (cameraFar / cameraNear - 1.0f)
        );
    }
    else
    {
        DestinationTexture[pixelId] = saturate(
            (linearDepth * cameraFar - cameraFar * cameraNear) /
            (linearDepth * (cameraFar - cameraNear))
        );
    }
}
