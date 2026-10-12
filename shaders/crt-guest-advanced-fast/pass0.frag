// pass 0: shaders/guest/fast/stock.slang, converted by tools/slang2glsl.py
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
};

uniform Push params;

uniform sampler2D Source;

layout(location = 0) out vec4 FragColor;
in vec2 HK_VARY0;

void main()
{
    FragColor = vec4(texture(Source, HK_VARY0).xyz, 1.0);
}

