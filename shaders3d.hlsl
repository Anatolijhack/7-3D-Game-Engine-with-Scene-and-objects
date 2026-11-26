cbuffer Params : register(b0)
{
    float4x4 mvp;
    float4x4 world;
    float time;
    float pulse;
    float mode;
    float pad;
    float4 lightDir;
    float4 camPos;
};

struct Light
{
    float3 pos;   float range;
    float3 color; float intensity;
};

cbuffer LightCB : register(b1)
{
    float4 ambientColor;
    int    lightCount;
    float3 _lpad;
    Light  lights[8];
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

PSInput VSMain(VSInput i)
{
    PSInput o;

    float scale = 1.0f + pulse * sin(time * 4.0f);
    float3 p = i.position * scale;
    float4 localPos = float4(p, 1.0f);

    float4 wp = mul(localPos, world);

    o.position = mul(localPos, mvp);
    o.worldPos = wp.xyz;
    o.normal = normalize(mul(i.normal, (float3x3)world));
    o.color = i.color;
    o.uv = i.uv;
    return o;
}

// —умма диффузной и бликовой составл€ющих от всех точечных ламп
void CalcPointLights(float3 N, float3 worldPos, float3 V,
                     out float3 diffuseSum, out float3 specSum)
{
    diffuseSum = 0;
    specSum = 0;

    for (int k = 0; k < lightCount; ++k)
    {
        float3 Lv = lights[k].pos - worldPos;
        float d = length(Lv);
        float3 L = Lv / max(d, 0.0001f);

        // затухание: плавно до нул€ на границе range
        float att = saturate(1.0f - d / lights[k].range);
        att *= att;

        float diff = saturate(dot(N, L));
        float3 lc = lights[k].color * lights[k].intensity * att;

        diffuseSum += lc * diff;

        if (diff > 0.0f)
        {
            float3 H = normalize(L + V);
            specSum += lc * pow(saturate(dot(N, H)), 32.0f);
        }
    }
}

float4 PSMain(PSInput i) : SV_TARGET
{
    // „астицы и свечение
    if (mode > 0.5f)
    {
        float d = length(i.uv * 2.0f - 1.0f);
        float b = saturate(1.0f - d);
        b *= b;
        return float4(i.color.rgb, i.color.a * b);
    }

    float3 N = normalize(i.normal);
    float3 V = normalize(camPos.xyz - i.worldPos);

    float3 baseColor = i.color.rgb * g_texture.Sample(g_sampler, i.uv).rgb;

    // --- направленный свет (слабый, как заполн€ющий) ---
    float3 Ld = normalize(-lightDir.xyz);
    float dDiff = saturate(dot(N, Ld));
    float dSpec = 0.0f;
    if (dDiff > 0.0f)
        dSpec = pow(saturate(dot(N, normalize(Ld + V))), 32.0f);

    // --- точечные лампы ---
    float3 pDiffuse, pSpec;
    CalcPointLights(N, i.worldPos, V, pDiffuse, pSpec);

    float3 ambient  = baseColor * ambientColor.rgb;
    float3 diffuse  = baseColor * (dDiff * 0.3f + pDiffuse);
    float3 specular = (dSpec * 0.2f + pSpec) * 0.5f;

    float3 rgb = ambient + diffuse + specular;
    return float4(rgb, 1.0f);
}