// pass 3: ../shaders/glow-h.slang, converted by tools/slang2glsl.py
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

flat in float HK_VARY2;
flat in float HK_VARY1;
in vec2 HK_VARY0;
layout(location = 0) out vec4 FragColor;

void main()
{
    float dx = (((2.0 * global.LR_GLOW_SIZE) * HK_VARY2) * HK_VARY1) * params.OriginalSize.z;
    vec2 tc = HK_VARY0;
    if (HK_VARY1 > 1.5)
    {
        float lines = params.SourceSize.y / HK_VARY1;
        tc.y = (floor(tc.y * lines) + 0.5) * params.SourceSize.w;
    }
    vec3 acc = vec3(0.0);
    float ws = 0.0;
    for (int k = -6; k <= 6; k++)
    {
        float w = exp(((-0.5) * float(k * k)) / 9.0);
        vec2 p = tc + vec2(float(k) * dx, 0.0);
        vec3 c = (textureLod(Source, p - vec2(0.25 * dx, 0.0), 0.0).xyz + textureLod(Source, p + vec2(0.25 * dx, 0.0), 0.0).xyz) * 0.5;
        acc += (pow(c, vec3(global.LR_GAMMA)) * w);
        ws += w;
    }
    FragColor = vec4(acc / vec3(ws), 1.0);
}

