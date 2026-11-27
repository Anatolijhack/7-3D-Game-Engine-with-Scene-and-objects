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
    float4x4 lightVP;   
};

Texture2D g_texture : register(t0);
Texture2D<float> g_shadow : register(t1);
SamplerState g_sampler : register(s0);
SamplerComparisonState g_shadowSamp : register(s1);

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
float4 VSShadow(VSInput i) : SV_POSITION
{
    float scale = 1.0f + pulse * sin(time * 4.0f);   // та же пульсаци€, что у шара
    return mul(float4(i.position * scale, 1.0f), mvp);
}
float ShadowFactor(float3 worldPos)
{
    float4 lp = mul(float4(worldPos, 1.0f), lightVP);
    float3 p  = lp.xyz / lp.w;
    float2 uv = float2(p.x * 0.5f + 0.5f, -p.y * 0.5f + 0.5f);

    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1 || p.z > 1.0f)
        return 1.0f;                                  // вне карты теней: свет

    const float texel = 1.0f / 2048.0f;
    float sum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            sum += g_shadow.SampleCmpLevelZero(g_shadowSamp, uv + float2(x, y) * texel, p.z - 0.0015f);
    return sum / 9.0f;                                // 3x3 PCF, м€гкие кра€
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

     float sh = ShadowFactor(i.worldPos + N * 0.03f);

    float3 ambient  = baseColor * ambientColor.rgb;
    float3 diffuse  = baseColor * (dDiff * sh * 0.9f + pDiffuse);
    float3 specular = (dSpec * sh * 0.2f + pSpec) * 0.5f;

    float3 rgb = ambient + diffuse + specular;
    return float4(rgb, 1.0f);
}

struct SkyVSOut
{
    float4 pos : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

// один треугольник на весь экран, вершинный буфер не нужен
SkyVSOut VSSky(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);          // (0,0) (2,0) (0,2)
    SkyVSOut o;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
    o.ndc = o.pos.xy;
    return o;
}

float4 PSSky(SkyVSOut i) : SV_TARGET
{
    // в слоте mvp на этот проход лежит обратна€ view*proj
    float4 w = mul(float4(i.ndc, 1.0f, 1.0f), mvp);
    float3 dir = normalize(w.xyz / w.w - camPos.xyz);

    // палитра (сумерки, под цветные лампы; дл€ дн€ сделай всЄ светлее и голубее)
    float3 zenith  = float3(0.03f, 0.06f, 0.18f);
    float3 horizon = float3(0.55f, 0.38f, 0.42f);
    float3 ground  = float3(0.10f, 0.10f, 0.13f);

    float h = dir.y;
    float3 col = lerp(horizon, zenith, pow(saturate(h), 0.5f));
    col = lerp(col, ground, saturate(-h * 4.0f));      // ниже горизонта темнеем

    // солнце/луна в стороне, откуда Ђсветитї lightDir
    float3 S = normalize(-lightDir.xyz);
    float sd = saturate(dot(dir, S));
    col += float3(1.0f, 0.85f, 0.6f) * (pow(sd, 400.0f) * 2.0f + pow(sd, 8.0f) * 0.25f);

    return float4(col, 1.0f);
}