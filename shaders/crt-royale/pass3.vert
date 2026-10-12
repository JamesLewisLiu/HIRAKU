// pass 3: ../blurs/shaders/royale/blur9fast-vertical.slang, converted by tools/slang2glsl.py
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

layout(location = 0) in vec4 Position;
out vec2 HK_VARY0;
layout(location = 1) in vec2 TexCoord;
out vec2 HK_VARY1;

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
    vec2 dxdy_scale = params.SourceSize.xy / params.OutputSize.xy;
    vec2 dxdy = dxdy_scale / params.SourceSize.xy;
    HK_VARY1 = vec2(0.0, dxdy.y);
}

