// pass 4: shaders/guest/fast/crt-guest-advanced-pass1.slang, converted by tools/slang2glsl.py
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
    float IOS;
    float h_sharp;
    float s_sharp;
    float spike;
    float ring;
};

uniform Push params;

uniform sampler2D LinearizePass;

in vec2 HK_VARY0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 prescalex = vec2(textureSize(LinearizePass, 0)) / params.OriginalSize.xy;
    vec4 SourceSize = params.OriginalSize * vec4(prescalex.x, prescalex.y, 1.0 / prescalex.x, 1.0 / prescalex.y);
    float intera = texture(LinearizePass, vec2(0.75, 0.25)).w;
    bool interb = intera < 0.5;
    vec2 texcoord = HK_VARY0;
    vec2 pos = texcoord;
    vec2 coffset = vec2(0.5);
    vec2 ps = SourceSize.zw;
    vec2 OGL2Pos = (pos * SourceSize.xy) - coffset;
    float fpx = fract(OGL2Pos.x);
    vec2 offx = vec2(ps.x, 0.0);
    vec2 x2 = offx * 2.0;
    vec2 pC4 = (floor(OGL2Pos) * ps) + (ps * 0.5);
    float zero = exp2(-params.h_sharp);
    float sharp1 = params.s_sharp * zero;
    float idiv = clamp(SourceSize.x / 400.0, 1.0, 2.0);
    float fdivider = max(min(prescalex.x, 2.0), idiv * float(interb));
    fdivider = 1.0 / max(fdivider, 1.0);
    float wl3 = (2.0 + fpx) * fdivider;
    float wl2 = (1.0 + fpx) * fdivider;
    float wl1 = fpx * fdivider;
    float wr1 = (1.0 - fpx) * fdivider;
    float wr2 = (2.0 - fpx) * fdivider;
    float wr3 = (3.0 - fpx) * fdivider;
    wl3 *= wl3;
    wl3 = exp2((-params.h_sharp) * wl3);
    wl2 *= wl2;
    wl2 = exp2((-params.h_sharp) * wl2);
    wl1 *= wl1;
    wl1 = exp2((-params.h_sharp) * wl1);
    wr1 *= wr1;
    wr1 = exp2((-params.h_sharp) * wr1);
    wr2 *= wr2;
    wr2 = exp2((-params.h_sharp) * wr2);
    wr3 *= wr3;
    wr3 = exp2((-params.h_sharp) * wr3);
    float fp1 = 1.0 - fpx;
    float twl3 = max(wl3 - sharp1, 0.0);
    float twl2 = max(wl2 - sharp1, mix(-0.119999997317790985107421875, 0.0, 1.0 - (fp1 * fp1)));
    float twl1 = max(wl1 - sharp1, -0.119999997317790985107421875);
    float twr1 = max(wr1 - sharp1, -0.119999997317790985107421875);
    float twr2 = max(wr2 - sharp1, mix(-0.119999997317790985107421875, 0.0, 1.0 - (fpx * fpx)));
    float twr3 = max(wr3 - sharp1, 0.0);
    bool sharp = sharp1 > 0.0;
    vec3 l3 = texture(LinearizePass, pC4 - x2).xyz;
    vec3 l2 = texture(LinearizePass, pC4 - offx).xyz;
    vec3 l1 = texture(LinearizePass, pC4).xyz;
    vec3 r1 = texture(LinearizePass, pC4 + offx).xyz;
    vec3 r2 = texture(LinearizePass, pC4 + x2).xyz;
    vec3 r3 = texture(LinearizePass, (pC4 + offx) + x2).xyz;
    vec3 colmin = min(min(l1, r1), min(l2, r2));
    vec3 colmax = max(max(l1, r1), max(l2, r2));
    vec3 color1 = ((((((l3 * twl3) + (l2 * twl2)) + (l1 * twl1)) + (r1 * twr1)) + (r2 * twr2)) + (r3 * twr3)) / vec3(((((twl3 + twl2) + twl1) + twr1) + twr2) + twr3);
    if (sharp)
    {
        color1 = clamp(mix(clamp(color1, colmin, colmax), color1, vec3(params.ring)), vec3(0.0), vec3(1.0));
    }
    float ts = 0.02500000037252902984619140625;
    vec3 luma = vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875);
    float lm2 = max(max(l2.x, l2.y), l2.z);
    float lm1 = max(max(l1.x, l1.y), l1.z);
    float rm1 = max(max(r1.x, r1.y), r1.z);
    float rm2 = max(max(r2.x, r2.y), r2.z);
    float swl2 = max(twl2, 0.0) * (dot(l2, luma) + ts);
    float swl1 = max(twl1, 0.0) * (dot(l1, luma) + ts);
    float swr1 = max(twr1, 0.0) * (dot(r1, luma) + ts);
    float swr2 = max(twr2, 0.0) * (dot(r2, luma) + ts);
    float fscolor1 = ((((lm2 * swl2) + (lm1 * swl1)) + (rm1 * swr1)) + (rm2 * swr2)) / (((swl2 + swl1) + swr1) + swr2);
    float sresult = clamp(mix(max(max(color1.x, color1.y), color1.z), fscolor1, params.spike), 0.0, 1.0);
    FragColor = vec4(color1, sresult);
}

