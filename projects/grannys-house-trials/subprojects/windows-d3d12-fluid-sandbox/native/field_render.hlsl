struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TextureUV : TEXCOORD0;
};

Texture2D<float4> FieldTexture : register(t0);

PSInput VSMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    PSInput output;
    output.Position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    output.TextureUV = uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = saturate(input.TextureUV);
    uint width, height;
    FieldTexture.GetDimensions(width, height);
    uint2 fieldPosition = min(uint2(uv * float2(width, height)), uint2(width - 1, height - 1));
    return FieldTexture.Load(int3(fieldPosition, 0));
}
