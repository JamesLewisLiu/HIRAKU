// pass 5: shaders/guest/fast/bloom_horizontal.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    mat4 MVP;
} global;

struct Push
{
    vec4 LinearizePassSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float SIZEHB;
    float SIGMA_HB;
    float FINE_BLOOM;
};

uniform Push params;

uniform sampler2D LinearizePass;

in vec2 HK_VARY0;
layout(location = 0) out vec4 FragColor;
float FINE_BLOOM;
float invsqrsigma;

float gaussian(float x)
{
    return exp(((-x) * x) * invsqrsigma);
}

void main()
{
    float _27;
    if (params.FINE_BLOOM > 0.5)
    {
        _27 = params.FINE_BLOOM;
    }
    else
    {
        _27 = mix(0.75, 0.5, -params.FINE_BLOOM);
    }
    FINE_BLOOM = _27;
    invsqrsigma = 1.0 / ((2.0 * params.SIGMA_HB) * params.SIGMA_HB);
    vec4 SourceSize1 = params.OriginalSize * vec4(FINE_BLOOM, FINE_BLOOM, 1.0 / FINE_BLOOM, 1.0 / FINE_BLOOM);
    float f = fract(SourceSize1.x * HK_VARY0.x);
    f = 0.5 - f;
    vec2 tex = (floor(SourceSize1.xy * HK_VARY0) * SourceSize1.zw) + (SourceSize1.zw * 0.5);
    vec4 color = vec4(0.0);
    vec2 dx = vec2(SourceSize1.z, 0.0);
    float wsum = 0.0;
    float _113 = -params.SIZEHB;
    float n = _113;
    do
    {
        vec4 pixel = texture(LinearizePass, tex + (dx * n));
        float param = n + f;
        float w = gaussian(param);
        pixel.w = max(max(pixel.x, pixel.y), pixel.z);
        pixel.w *= (pixel.w * pixel.w);
        color += (pixel * w);
        wsum += w;
        n += 1.0;
    } while (n <= params.SIZEHB);
    color /= vec4(wsum);
    FragColor = vec4(color.xyz, pow(color.w, 0.3333329856395721435546875));
}

