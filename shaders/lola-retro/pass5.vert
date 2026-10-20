// pass 5: ../shaders/beam.slang, converted by tools/slang2glsl.py
#version 330

struct Timing
{
    float pclk;
    float fsc;
    float lineFrac;
    float fieldStep;
    float framePhase;
    float vsw;
    float vswStep;
    float pal;
    float scan;
    float interlace;
    float scale;
    float id;
    float hres;
};

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

layout(location = 0) in vec4 Position;
out vec2 HK_VARY0;
layout(location = 1) in vec2 TexCoord;
flat out float HK_VARY1;
flat out float HK_VARY2;
flat out vec2 HK_VARY3;
flat out float HK_VARY4;

float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool _303 = id < 5.5;
    bool _312;
    if (!_303)
    {
        _312 = (id > 7.5) && (id < 8.5);
    }
    else
    {
        _312 = _303;
    }
    bool tv = _312;
    float base = ((id > 4.5) && (id < 5.5)) ? 480.0 : 240.0;
    bool _325;
    if (id < 0.5)
    {
        _325 = orig.y > 576.0;
    }
    else
    {
        bool _341;
        if (tv)
        {
            _341 = orig.y > (base * 1.5);
        }
        else
        {
            _341 = tv;
        }
        _325 = _341;
    }
    bool scaled = _325;
    if (!scaled)
    {
        return 1.0;
    }
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        bool _364 = lines == floor(lines);
        bool _372;
        if (_364)
        {
            _372 = lines >= (base * 0.829999983310699462890625);
        }
        else
        {
            _372 = _364;
        }
        bool _380;
        if (_372)
        {
            _380 = lines <= (base * 1.21000003814697265625);
        }
        else
        {
            _380 = _372;
        }
        if (_380)
        {
            return k;
        }
    }
    return max(1.0, floor((orig.y / base) + 0.5));
}

float autoConsole(vec2 s)
{
    float w = s.x;
    float h = s.y;
    bool mon15 = global.LR_MON_MM > 0.5;
    bool _76 = !mon15;
    bool _103;
    if (_76)
    {
        bool _85 = (w == 320.0) && (h == 200.0);
        bool _102;
        if (!_85)
        {
            bool _91 = w == 640.0;
            bool _101;
            if (_91)
            {
                _101 = (h == 350.0) || (h == 400.0);
            }
            else
            {
                _101 = _91;
            }
            _102 = _101;
        }
        else
        {
            _102 = _85;
        }
        _103 = _102;
    }
    else
    {
        _103 = _76;
    }
    if (_103)
    {
        return 7.0;
    }
    bool _114 = (h == 256.0) || (h == 512.0);
    bool _123;
    if (!_114)
    {
        _123 = (w == 640.0) && (h == 200.0);
    }
    else
    {
        _123 = _114;
    }
    if (_123)
    {
        return 6.0;
    }
    bool _136;
    if (mon15)
    {
        _136 = (h == 200.0) || (h == 400.0);
    }
    else
    {
        _136 = mon15;
    }
    bool _144;
    if (_136)
    {
        _144 = (w == 320.0) || (w == 640.0);
    }
    else
    {
        _144 = _136;
    }
    if (_144)
    {
        return 6.0;
    }
    bool _158 = ((w == 360.0) || (w == 720.0)) || (w == 1440.0);
    bool _176;
    if (_158)
    {
        _176 = (((h == 287.0) || (h == 574.0)) || (h == 242.0)) || (h == 484.0);
    }
    else
    {
        _176 = _158;
    }
    if (_176)
    {
        return 6.0;
    }
    if (((h >= 336.0) && (h <= 400.0)) && (w >= 400.0))
    {
        return 9.0;
    }
    if (h > 300.0)
    {
        return 5.0;
    }
    if (w > 420.0)
    {
        return 4.0;
    }
    if (w <= 300.0)
    {
        return 1.0;
    }
    if (h >= 232.0)
    {
        return 4.0;
    }
    return 2.0;
}

void consoleTable(float id, out vec4 a, out vec4 b, out float c)
{
    if (id < 1.5)
    {
        a = vec4(5.3693180084228515625, 5.32034206390380859375, 256.0, 0.0);
        b = vec4(0.3333333432674407958984375, 0.16666667163372039794921875, 0.3333333432674407958984375, 0.0);
        c = 0.0;
    }
    else
    {
        if (id < 2.5)
        {
            a = vec4(6.71164703369140625, 6.65042781829833984375, 320.0, 0.0);
            b = vec4(0.0);
            c = 1.0;
        }
        else
        {
            if (id < 3.5)
            {
                a = vec4(5.3693180084228515625, 5.32034206390380859375, 256.0, 0.0);
                b = vec4(0.0);
                c = 1.0;
            }
            else
            {
                if (id < 4.5)
                {
                    a = vec4(6.71164703369140625, 6.652800083160400390625, 320.0, 0.0);
                    b = vec4(0.5, 0.75, 0.5, 0.5);
                    c = 0.0;
                }
                else
                {
                    if (id < 5.5)
                    {
                        a = vec4(13.5, 13.5, 640.0, 0.0);
                        b = vec4(0.5, 0.75, 0.75, 0.875);
                        c = 0.0;
                    }
                    else
                    {
                        if (id < 6.5)
                        {
                            a = vec4(7.1590900421142578125, 7.0937900543212890625, 320.0, 0.0);
                            b = vec4(0.5, 0.75, 0.75, 0.875);
                            c = 0.0;
                        }
                        else
                        {
                            if (id < 7.5)
                            {
                                a = vec4(12.5874996185302734375, 12.5874996185302734375, 320.0, 1.0);
                                b = vec4(0.5, 0.75, 0.75, 0.875);
                                c = 0.0;
                            }
                            else
                            {
                                if (id < 8.5)
                                {
                                    a = vec4(6.71164703369140625, 6.71164703369140625, 320.0, 0.0);
                                    b = vec4(0.5, 0.75, 0.5, 0.5);
                                    c = 0.0;
                                }
                                else
                                {
                                    a = vec4(16.0, 16.0, 496.0, 2.0);
                                    b = vec4(0.5, 0.75, 0.75, 0.875);
                                    c = 0.0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

bool isShow(float cable)
{
    return cable > 3.5;
}

Timing getTiming(vec2 orig, uint frameCount)
{
    vec2 param = orig;
    Timing t;
    t.scale = upscaleOf(param);
    vec2 nat = orig / vec2(t.scale);
    float _424;
    if (global.LR_CONSOLE < 0.5)
    {
        vec2 param_1 = nat;
        _424 = autoConsole(param_1);
    }
    else
    {
        _424 = global.LR_CONSOLE;
    }
    float id = _424;
    float param_2 = id;
    vec4 param_3;
    vec4 param_4;
    float param_5;
    consoleTable(param_2, param_3, param_4, param_5);
    vec4 a = param_3;
    vec4 b = param_4;
    float c = param_5;
    t.id = id;
    bool _452 = (id > 4.5) && (id < 5.5);
    bool _459;
    if (_452)
    {
        _459 = nat.x > 700.0;
    }
    else
    {
        _459 = _452;
    }
    if (_459)
    {
        a.z = 720.0;
    }
    t.scan = a.w;
    bool _472 = t.scan < 0.5;
    bool _478;
    if (_472)
    {
        _478 = nat.y > 300.0;
    }
    else
    {
        _478 = _472;
    }
    t.interlace = float(_478);
    bool amiga = (id > 5.5) && (id < 6.5);
    if (global.LR_NORM > 1.5)
    {
        t.pal = 1.0;
    }
    else
    {
        if (global.LR_NORM > 0.5)
        {
            t.pal = 0.0;
        }
        else
        {
            float _507;
            if (t.interlace > 0.5)
            {
                _507 = nat.y * 0.5;
            }
            else
            {
                _507 = nat.y;
            }
            float lines = _507;
            bool _519 = t.scan < 0.5;
            bool _528;
            if (_519)
            {
                _528 = lines > (amiga ? 249.5 : 240.5);
            }
            else
            {
                _528 = _519;
            }
            t.pal = float(_528);
        }
    }
    float base = mix(a.x, a.y, t.pal);
    t.pclk = (base * orig.x) / a.z;
    float _552;
    if (orig.x < 480.0)
    {
        _552 = 1.0;
    }
    else
    {
        _552 = (orig.x < 960.0) ? 2.0 : 4.0;
    }
    float cls = _552;
    if (amiga)
    {
        t.pclk = (base * (320.0 * cls)) / a.z;
    }
    bool _575 = t.scan > 0.5;
    bool _581;
    if (_575)
    {
        _581 = t.scan < 1.5;
    }
    else
    {
        _581 = _575;
    }
    bool _587;
    if (_581)
    {
        _587 = orig.x > 720.0;
    }
    else
    {
        _587 = _581;
    }
    if (_587)
    {
        t.pclk = ((78.75 * orig.x) * orig.y) / 786432.0;
    }
    float _602;
    if (amiga)
    {
        _602 = cls;
    }
    else
    {
        _602 = orig.x / a.z;
    }
    t.hres = _602;
    t.fsc = (t.pal > 0.5) ? 4.433619022369384765625 : 3.57954502105712890625;
    float param_6 = global.LR_CABLE;
    if (isShow(param_6))
    {
        b = vec4(0.5, 0.75, 0.75, 0.875);
        c = 0.0;
    }
    t.lineFrac = mix(b.x, b.y, t.pal);
    t.fieldStep = mix(b.z, b.w, t.pal);
    float _650;
    if (global.LR_STEADY > 0.5)
    {
        _650 = 0.0;
    }
    else
    {
        _650 = float(frameCount % 24u);
    }
    float fc = _650;
    t.framePhase = fract(fc * t.fieldStep);
    t.vsw = mod(fc * c, 2.0);
    t.vswStep = c;
    return t;
}

float activeShare(Timing t, vec2 orig)
{
    bool _679 = t.scan < 0.5;
    bool _685;
    if (_679)
    {
        _685 = global.LR_MON_MM > 0.5;
    }
    else
    {
        _685 = _679;
    }
    if (!_685)
    {
        return 1.0;
    }
    return min((orig.x / t.pclk) / ((t.pal > 0.5) ? 52.0 : 52.59999847412109375), 1.0);
}

bool isAmiga(Timing t)
{
    bool _399 = t.id > 5.5;
    bool _405;
    if (_399)
    {
        _405 = t.id < 6.5;
    }
    else
    {
        _405 = _399;
    }
    return _405;
}

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
    vec2 param = params.OriginalSize.xy;
    uint param_1 = params.FrameCount;
    Timing T = getTiming(param, param_1);
    HK_VARY1 = T.scale;
    HK_VARY2 = T.scan;
    float vis = ((T.pal > 0.5) ? 288.0 : 240.0) * ((T.interlace > 0.5) ? 2.0 : 1.0);
    bool _759 = global.LR_FILL > 0.5;
    bool _767;
    if (_759)
    {
        float param_2 = global.LR_CABLE;
        _767 = !isShow(param_2);
    }
    else
    {
        _767 = _759;
    }
    bool fill = _767;
    Timing param_3 = T;
    vec2 param_4 = params.OriginalSize.xy;
    float _782;
    if ((T.scan < 0.5) && (!fill))
    {
        _782 = min((params.OriginalSize.y / T.scale) / vis, 1.0);
    }
    else
    {
        _782 = 1.0;
    }
    HK_VARY3 = vec2(activeShare(param_3, param_4), _782);
    Timing param_5 = T;
    float _801;
    if (isAmiga(param_5))
    {
        _801 = 320.0 * T.hres;
    }
    else
    {
        _801 = params.OriginalSize.x;
    }
    HK_VARY4 = _801;
}

