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

layout(location = 0) in vec4 Position;
out vec2 HK_VARY0;
layout(location = 1) in vec2 TexCoord;
float FINE_BLOOM;

void main()
{
    float _23;
    if (params.FINE_BLOOM > 0.5)
    {
        _23 = params.FINE_BLOOM;
    }
    else
    {
        _23 = mix(0.75, 0.5, -params.FINE_BLOOM);
    }
    FINE_BLOOM = _23;
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
}

