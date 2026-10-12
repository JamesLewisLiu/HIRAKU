// pass 8: shaders/guest/fast/deconvergence-f.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    mat4 MVP;
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float bloom;
    float halation;
    float slotms;
    float mask_gamma;
    float gamma_out;
    float overscanX;
    float overscanY;
    float VShift;
    float intres;
    float prescalex;
    float c_shape;
    float barspeed;
    float bardir;
    float sborder;
    float bloom_dist;
    float deconr;
    float decons;
    float deconrr;
    float deconrg;
    float deconrb;
    float deconrry;
    float deconrgy;
    float deconrby;
    float dctypex;
    float dctypey;
    float post_br;
    float noisetype;
    float maskboost;
    float smoothmask;
    float gamma_c;
    float gamma_c2;
    float smask_mit;
    float mask_zoom;
    float no_scanlines;
    float bmask;
    float bmask1;
    float hmask1;
    float mzoom_sh;
    float mclip;
    float edgemask;
    float oimage;
    float interm;
} global;

struct Push
{
    float IOS;
    float brightboost;
    float brightboost1;
    float csize;
    float bsize1;
    float warpX;
    float warpY;
    float glow;
    float shadowMask;
    float masksize;
    float hiscan;
    float slotmask;
    float slotmask1;
    float slotwidth;
    float double_slot;
    float mcut;
    float maskDark;
    float maskLight;
    float maskstr;
    float addnoised;
    float noiseresd;
    float barintensity;
    float mshift;
    float mask_layout;
    float mask_bloom;
    float pr_scan;
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

