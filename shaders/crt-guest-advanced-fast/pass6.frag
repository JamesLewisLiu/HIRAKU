// pass 6: shaders/guest/fast/bloom_vertical.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    mat4 MVP;
} global;

struct Push
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float SIZEVB;
    float SIGMA_VB;
    float FINE_BLOOM;
};

uniform Push params;

uniform sampler2D Source;

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
    invsqrsigma = 1.0 / ((2.0 * params.SIGMA_VB) * params.SIGMA_VB);
    vec4 SourceSize1 = params.SourceSize;
    SourceSize1.y = params.OriginalSize.yw.x;
    SourceSize1.w = params.OriginalSize.yw.y;
    SourceSize1 *= vec4(FINE_BLOOM, FINE_BLOOM, 1.0 / FINE_BLOOM, 1.0 / FINE_BLOOM);
    float f = fract(SourceSize1.y * HK_VARY0.y);
    f = 0.5 - f;
    vec2 tex = (floor(SourceSize1.xy * HK_VARY0) * SourceSize1.zw) + (SourceSize1.zw * 0.5);
    vec4 color = vec4(0.0);
    vec2 dy = vec2(0.0, SourceSize1.w);
    float wsum = 0.0;
    float _122 = -params.SIZEVB;
    float n = _122;
    do
    {
        vec4 pixel = texture(Source, tex + (dy * n));
        float param = n + f;
        float w = gaussian(param);
        pixel.w *= (pixel.w * pixel.w);
        color += (pixel * w);
        wsum += w;
        n += 1.0;
    } while (n <= params.SIZEVB);
    color /= vec4(wsum);
    FragColor = vec4(color.xyz, pow(color.w, 0.17499999701976776123046875));
}

