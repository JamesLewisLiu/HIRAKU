// pass 2: shaders/guest/fast/pre-shaders.slang, converted by tools/slang2glsl.py
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
    float TNTC;
    float LS;
    float LUTLOW;
    float LUTBR;
    float CP;
    float CS;
    float BP;
    float WP;
    float wp_saturation;
    float vigstr;
    float vigdef;
    float sega_fix;
    float pre_bb;
    float contr;
    float pre_gc;
};

uniform Push params;

layout(location = 0) in vec4 Position;
out vec2 HK_VARY0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
}

