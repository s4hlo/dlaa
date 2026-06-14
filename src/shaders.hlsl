cbuffer ModelViewProjection : register(b0)
{
    matrix mvp;
    matrix mvpPrev;
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

// Motion vector capture — MRT: RT0 = color (R8G8B8A8), RT1 = motion (R16G16F)
// motion.xy = NDC displacement current - previous (screen-space reprojection vector)

struct MVVSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float4 currClip : TEXCOORD0;
    float4 prevClip : TEXCOORD1;
};

MVVSOutput MVVSMain(VSInput input)
{
    MVVSOutput o;
    o.currClip = mul(float4(input.position, 1.0f), mvp);
    o.prevClip = mul(float4(input.position, 1.0f), mvpPrev);
    o.position = o.currClip;
    o.color    = input.color;
    return o;
}

struct MVPSOutput
{
    float4 color  : SV_TARGET0;
    float2 motion : SV_TARGET1;
};

MVPSOutput MVPSMain(MVVSOutput input)
{
    MVPSOutput o;
    o.color  = input.color;
    float2 curr = input.currClip.xy / input.currClip.w;
    float2 prev = input.prevClip.xy / input.prevClip.w;
    o.motion = curr - prev;
    return o;
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
