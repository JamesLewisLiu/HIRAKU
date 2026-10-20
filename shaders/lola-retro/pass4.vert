// pass 4: ../shaders/glow-v.slang, converted by tools/slang2glsl.py
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

float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool _302 = id < 5.5;
    bool _311;
    if (!_302)
    {
        _311 = (id > 7.5) && (id < 8.5);
    }
    else
    {
        _311 = _302;
    }
    bool tv = _311;
    float base = ((id > 4.5) && (id < 5.5)) ? 480.0 : 240.0;
    bool _324;
    if (id < 0.5)
    {
        _324 = orig.y > 576.0;
    }
    else
    {
        bool _340;
        if (tv)
        {
            _340 = orig.y > (base * 1.5);
        }
        else
        {
            _340 = tv;
        }
        _324 = _340;
    }
    bool scaled = _324;
    if (!scaled)
    {
        return 1.0;
    }
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        bool _363 = lines == floor(lines);
        bool _371;
        if (_363)
        {
            _371 = lines >= (base * 0.829999983310699462890625);
        }
        else
        {
            _371 = _363;
        }
        bool _379;
        if (_371)
        {
            _379 = lines <= (base * 1.21000003814697265625);
        }
        else
        {
            _379 = _371;
        }
        if (_379)
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
    bool _75 = !mon15;
    bool _102;
    if (_75)
    {
        bool _84 = (w == 320.0) && (h == 200.0);
        bool _101;
        if (!_84)
        {
            bool _90 = w == 640.0;
            bool _100;
            if (_90)
            {
                _100 = (h == 350.0) || (h == 400.0);
            }
            else
            {
                _100 = _90;
            }
            _101 = _100;
        }
        else
        {
            _101 = _84;
        }
        _102 = _101;
    }
    else
    {
        _102 = _75;
    }
    if (_102)
    {
        return 7.0;
    }
    bool _113 = (h == 256.0) || (h == 512.0);
    bool _122;
    if (!_113)
    {
        _122 = (w == 640.0) && (h == 200.0);
    }
    else
    {
        _122 = _113;
    }
    if (_122)
    {
        return 6.0;
    }
    bool _135;
    if (mon15)
    {
        _135 = (h == 200.0) || (h == 400.0);
    }
    else
    {
        _135 = mon15;
    }
    bool _143;
    if (_135)
    {
        _143 = (w == 320.0) || (w == 640.0);
    }
    else
    {
        _143 = _135;
    }
    if (_143)
    {
        return 6.0;
    }
    bool _157 = ((w == 360.0) || (w == 720.0)) || (w == 1440.0);
    bool _175;
    if (_157)
    {
        _175 = (((h == 287.0) || (h == 574.0)) || (h == 242.0)) || (h == 484.0);
    }
    else
    {
        _175 = _157;
    }
    if (_175)
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
    float _423;
    if (global.LR_CONSOLE < 0.5)
    {
        vec2 param_1 = nat;
        _423 = autoConsole(param_1);
    }
    else
    {
        _423 = global.LR_CONSOLE;
    }
    float id = _423;
    float param_2 = id;
    vec4 param_3;
    vec4 param_4;
    float param_5;
    consoleTable(param_2, param_3, param_4, param_5);
    vec4 a = param_3;
    vec4 b = param_4;
    float c = param_5;
    t.id = id;
    bool _451 = (id > 4.5) && (id < 5.5);
    bool _458;
    if (_451)
    {
        _458 = nat.x > 700.0;
    }
    else
    {
        _458 = _451;
    }
    if (_458)
    {
        a.z = 720.0;
    }
    t.scan = a.w;
    bool _471 = t.scan < 0.5;
    bool _477;
    if (_471)
    {
        _477 = nat.y > 300.0;
    }
    else
    {
        _477 = _471;
    }
    t.interlace = float(_477);
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
            float _506;
            if (t.interlace > 0.5)
            {
                _506 = nat.y * 0.5;
            }
            else
            {
                _506 = nat.y;
            }
            float lines = _506;
            bool _518 = t.scan < 0.5;
            bool _527;
            if (_518)
            {
                _527 = lines > (amiga ? 249.5 : 240.5);
            }
            else
            {
                _527 = _518;
            }
            t.pal = float(_527);
        }
    }
    float base = mix(a.x, a.y, t.pal);
    t.pclk = (base * orig.x) / a.z;
    float _551;
    if (orig.x < 480.0)
    {
        _551 = 1.0;
    }
    else
    {
        _551 = (orig.x < 960.0) ? 2.0 : 4.0;
    }
    float cls = _551;
    if (amiga)
    {
        t.pclk = (base * (320.0 * cls)) / a.z;
    }
    bool _574 = t.scan > 0.5;
    bool _580;
    if (_574)
    {
        _580 = t.scan < 1.5;
    }
    else
    {
        _580 = _574;
    }
    bool _586;
    if (_580)
    {
        _586 = orig.x > 720.0;
    }
    else
    {
        _586 = _580;
    }
    if (_586)
    {
        t.pclk = ((78.75 * orig.x) * orig.y) / 786432.0;
    }
    float _601;
    if (amiga)
    {
        _601 = cls;
    }
    else
    {
        _601 = orig.x / a.z;
    }
    t.hres = _601;
    t.fsc = (t.pal > 0.5) ? 4.433619022369384765625 : 3.57954502105712890625;
    float param_6 = global.LR_CABLE;
    if (isShow(param_6))
    {
        b = vec4(0.5, 0.75, 0.75, 0.875);
        c = 0.0;
    }
    t.lineFrac = mix(b.x, b.y, t.pal);
    t.fieldStep = mix(b.z, b.w, t.pal);
    float _649;
    if (global.LR_STEADY > 0.5)
    {
        _649 = 0.0;
    }
    else
    {
        _649 = float(frameCount % 24u);
    }
    float fc = _649;
    t.framePhase = fract(fc * t.fieldStep);
    t.vsw = mod(fc * c, 2.0);
    t.vswStep = c;
    return t;
}

bool isAmiga(Timing t)
{
    bool _398 = t.id > 5.5;
    bool _404;
    if (_398)
    {
        _404 = t.id < 6.5;
    }
    else
    {
        _404 = _398;
    }
    return _404;
}

vec2 consoleUnit(Timing t)
{
    Timing param = t;
    vec2 _679;
    if (isAmiga(param))
    {
        _679 = vec2(t.hres, (t.interlace > 0.5) ? 2.0 : 1.0);
    }
    else
    {
        _679 = vec2(1.0);
    }
    return _679;
}

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
    vec2 param = params.OriginalSize.xy;
    uint param_1 = params.FrameCount;
    Timing T = getTiming(param, param_1);
    Timing param_2 = T;
    HK_VARY1 = consoleUnit(param_2).y * T.scale;
}

