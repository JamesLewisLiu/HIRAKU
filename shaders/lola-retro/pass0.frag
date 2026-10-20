// pass 0: ../shaders/native.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    mat4 MVP;
    float LR_HDR_SRC;
    float LR_CONSOLE;
    float LR_NORM;
    float LR_HDR_SIG;
    float LR_CABLE;
    float LR_COMB;
    float LR_YTRAP;
    float LR_DBLEND;
    float LR_SHARP;
    float LR_SAT;
    float LR_TINT;
    float LR_DEMOD;
    float LR_BRIGHT;
    float LR_CONTRAST;
    float LR_NOISE;
    float LR_STEADY;
    float LR_HDR_SHOW;
    float LR_TAPE;
    float LR_GHOST;
    float LR_HDR_BEAM;
    float LR_SIG_LO;
    float LR_SIG_HI;
    float LR_HFOCUS;
    float LR_GAMMA;
    float LR_PERSIST;
    float LR_CONV;
    float LR_HDR_MASK;
    float LR_MASK_TYPE;
    float LR_MASK_STR;
    float LR_MASK_TRIADS;
    float LR_WHITE;
    float LR_PHOS;
    float LR_HDR_GLASS;
    float LR_CURV_X;
    float LR_CURV_Y;
    float LR_BEND;
    float LR_CORNER;
    float LR_GLOW;
    float LR_GLOW_SIZE;
    float LR_VIGNETTE;
    float LR_OVERSCAN;
    float LR_OVERSCAN_V;
    float LR_FILL;
    float LR_LIN_X;
    float LR_LIN_Y;
    float LR_AMBIENT;
    float LR_HDR_MON;
    float LR_MON_MM;
    float LR_MON_SIZE;
    float LR_MON_PITCH;
    float LR_MON_SPOT_LO;
    float LR_MON_SPOT_HI;
    float LR_MON_CORNER;
    float LR_SCREEN_H;
    float LR_SCREEN_UNIT;
    float LR_BACKDROP;
    float LR_ROOM;
    float LR_SPILL;
    float LR_SCENE_SPEED;
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

in vec2 HK_VARY0;
flat in float HK_VARY1;
layout(location = 0) out vec4 FragColor;
flat in float HK_VARY2;

void main()
{
    float y = floor(HK_VARY0.y * params.SourceSize.y);
    if (mod(y, HK_VARY1) > 0.5)
    {
        FragColor = vec4(0.0);
        return;
    }
    float x0 = floor(HK_VARY0.x * params.SourceSize.x) - floor(HK_VARY2 * 0.5);
    vec3 c = vec3(0.0);
    for (float b = 0.0; b < HK_VARY2; b += 1.0)
    {
        float u = (clamp(x0 + b, 0.0, params.SourceSize.x - 1.0) + 0.5) * params.SourceSize.z;
        for (float r = 0.0; r < HK_VARY1; r += 1.0)
        {
            c += textureLod(Source, vec2(u, ((y + r) + 0.5) * params.SourceSize.w), 0.0).xyz;
        }
    }
    FragColor = vec4(c / vec3(HK_VARY1 * HK_VARY2), 1.0);
}

