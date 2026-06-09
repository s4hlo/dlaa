cbuffer ModelViewProjection : register(b0)
{
    matrix mvp;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), mvp);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}

// Downsample pass — fullscreen triangle, used when ENABLE_SUPERSAMPLING is defined

Texture2D SceneTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct QuadVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

QuadVSOutput QuadVS(uint vertexID : SV_VertexID)
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    QuadVSOutput output;
    output.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.uv = float2(uv.x, 1.0f - uv.y);
    return output;
}

float4 QuadPS(QuadVSOutput input) : SV_TARGET
{
    return SceneTexture.Sample(LinearSampler, input.uv);
}
