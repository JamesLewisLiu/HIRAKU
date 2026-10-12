// pass 3: shaders/guest/fast/linearize.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    vec4 SourceSize;
    mat4 MVP;
} global;

struct Push
{
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float GAMMA_INPUT;
    float inter;
    float interm;
    float iscan;
    float intres;
    float iscans;
    float downsample_levelx;
    float downsample_levely;
    float gamma_out;
    float vga_mode;
    float hiscan;
};

uniform Push params;

layout(location = 0) in vec4 Position;
out vec2 HK_VARY0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord * 1.000010013580322265625;
}

