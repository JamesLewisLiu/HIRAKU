// pass 7: shaders/guest/fast/crt-guest-advanced-pass2.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    mat4 MVP;
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float addnoise;
    float warpX;
    float warpY;
    float csize;
    float bsize1;
    float intres;
    float c_shape;
    float barspeed;
    float barintensity;
    float bardir;
    float sborder;
    float scan_falloff;
    float overscanX;
    float overscanY;
    float VShift;
    float bloom_dist;
    float bmask1;
    float hmask1;
    float interm;
} global;

struct Push
{
    float brightboost;
    float brightboost1;
    float gsl;
    float scanline1;
    float scanline2;
    float beam_min;
    float beam_max;
    float beam_size;
    float glow;
    float maskstr;
    float inters;
    float bloom;
    float halation;
    float scans;
    float gamma_c;
    float gamma_c2;
    float gamma_out;
    float IOS;
    float no_scanlines;
    float tds;
    float clips;
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

