// pass 0: ../shaders/native.slang, converted by tools/slang2glsl.py
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

float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool _293 = id < 5.5;
    bool _302;
    if (!_293)
    {
        _302 = (id > 7.5) && (id < 8.5);
    }
    else
    {
        _302 = _293;
    }
    bool tv = _302;
    float base = ((id > 4.5) && (id < 5.5)) ? 480.0 : 240.0;
    bool _315;
    if (id < 0.5)
    {
        _315 = orig.y > 576.0;
    }
    else
    {
        bool _331;
        if (tv)
        {
            _331 = orig.y > (base * 1.5);
        }
        else
        {
            _331 = tv;
        }
        _315 = _331;
    }
    bool scaled = _315;
    if (!scaled)
    {
        return 1.0;
    }
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        bool _354 = lines == floor(lines);
        bool _362;
        if (_354)
        {
            _362 = lines >= (base * 0.829999983310699462890625);
        }
        else
        {
            _362 = _354;
        }
        bool _370;
        if (_362)
        {
            _370 = lines <= (base * 1.21000003814697265625);
        }
        else
        {
            _370 = _362;
        }
        if (_370)
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
    bool _66 = !mon15;
    bool _93;
    if (_66)
    {
        bool _75 = (w == 320.0) && (h == 200.0);
        bool _92;
        if (!_75)
        {
            bool _81 = w == 640.0;
            bool _91;
            if (_81)
            {
                _91 = (h == 350.0) || (h == 400.0);
            }
            else
            {
                _91 = _81;
            }
            _92 = _91;
        }
        else
        {
            _92 = _75;
        }
        _93 = _92;
    }
    else
    {
        _93 = _66;
    }
    if (_93)
    {
        return 7.0;
    }
    bool _104 = (h == 256.0) || (h == 512.0);
    bool _113;
    if (!_104)
    {
        _113 = (w == 640.0) && (h == 200.0);
    }
    else
    {
        _113 = _104;
    }
    if (_113)
    {
        return 6.0;
    }
    bool _126;
    if (mon15)
    {
        _126 = (h == 200.0) || (h == 400.0);
    }
    else
    {
        _126 = mon15;
    }
    bool _134;
    if (_126)
    {
        _134 = (w == 320.0) || (w == 640.0);
    }
    else
    {
        _134 = _126;
    }
    if (_134)
    {
        return 6.0;
    }
    bool _148 = ((w == 360.0) || (w == 720.0)) || (w == 1440.0);
    bool _166;
    if (_148)
    {
        _166 = (((h == 287.0) || (h == 574.0)) || (h == 242.0)) || (h == 484.0);
    }
    else
    {
        _166 = _148;
    }
    if (_166)
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
    float _403;
    if (global.LR_CONSOLE < 0.5)
    {
        vec2 param_1 = nat;
        _403 = autoConsole(param_1);
    }
    else
    {
        _403 = global.LR_CONSOLE;
    }
    float id = _403;
    float param_2 = id;
    vec4 param_3;
    vec4 param_4;
    float param_5;
    consoleTable(param_2, param_3, param_4, param_5);
    vec4 a = param_3;
    vec4 b = param_4;
    float c = param_5;
    t.id = id;
    bool _432 = (id > 4.5) && (id < 5.5);
    bool _439;
    if (_432)
    {
        _439 = nat.x > 700.0;
    }
    else
    {
        _439 = _432;
    }
    if (_439)
    {
        a.z = 720.0;
    }
    t.scan = a.w;
    bool _452 = t.scan < 0.5;
    bool _458;
    if (_452)
    {
        _458 = nat.y > 300.0;
    }
    else
    {
        _458 = _452;
    }
    t.interlace = float(_458);
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
            float _487;
            if (t.interlace > 0.5)
            {
                _487 = nat.y * 0.5;
            }
            else
            {
                _487 = nat.y;
            }
            float lines = _487;
            bool _499 = t.scan < 0.5;
            bool _508;
            if (_499)
            {
                _508 = lines > (amiga ? 249.5 : 240.5);
            }
            else
            {
                _508 = _499;
            }
            t.pal = float(_508);
        }
    }
    float base = mix(a.x, a.y, t.pal);
    t.pclk = (base * orig.x) / a.z;
    float _532;
    if (orig.x < 480.0)
    {
        _532 = 1.0;
    }
    else
    {
        _532 = (orig.x < 960.0) ? 2.0 : 4.0;
    }
    float cls = _532;
    if (amiga)
    {
        t.pclk = (base * (320.0 * cls)) / a.z;
    }
    bool _555 = t.scan > 0.5;
    bool _561;
    if (_555)
    {
        _561 = t.scan < 1.5;
    }
    else
    {
        _561 = _555;
    }
    bool _567;
    if (_561)
    {
        _567 = orig.x > 720.0;
    }
    else
    {
        _567 = _561;
    }
    if (_567)
    {
        t.pclk = ((78.75 * orig.x) * orig.y) / 786432.0;
    }
    float _582;
    if (amiga)
    {
        _582 = cls;
    }
    else
    {
        _582 = orig.x / a.z;
    }
    t.hres = _582;
    t.fsc = (t.pal > 0.5) ? 4.433619022369384765625 : 3.57954502105712890625;
    float param_6 = global.LR_CABLE;
    if (isShow(param_6))
    {
        b = vec4(0.5, 0.75, 0.75, 0.875);
        c = 0.0;
    }
    t.lineFrac = mix(b.x, b.y, t.pal);
    t.fieldStep = mix(b.z, b.w, t.pal);
    float _630;
    if (global.LR_STEADY > 0.5)
    {
        _630 = 0.0;
    }
    else
    {
        _630 = float(frameCount % 24u);
    }
    float fc = _630;
    t.framePhase = fract(fc * t.fieldStep);
    t.vsw = mod(fc * c, 2.0);
    t.vswStep = c;
    return t;
}

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
    vec2 param = params.OriginalSize.xy;
    uint param_1 = params.FrameCount;
    Timing T = getTiming(param, param_1);
    HK_VARY1 = T.scale;
    float _697;
    if (T.scan < 0.5)
    {
        _697 = max(1.0, floor((params.OriginalSize.x / 1280.0) + 0.5));
    }
    else
    {
        _697 = 1.0;
    }
    HK_VARY2 = _697;
}

