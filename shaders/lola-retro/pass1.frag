// pass 1: ../shaders/encode.slang, converted by tools/slang2glsl.py
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

const float _1280[5] = float[](1.0, 0.726149022579193115234375, 0.278037011623382568359375, 0.056134998798370361328125, 0.005975999869406223297119140625);

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
layout(location = 0) out vec4 FragColor;

float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool _440 = id < 5.5;
    bool _449;
    if (!_440)
    {
        _449 = (id > 7.5) && (id < 8.5);
    }
    else
    {
        _449 = _440;
    }
    bool tv = _449;
    float base = ((id > 4.5) && (id < 5.5)) ? 480.0 : 240.0;
    bool _462;
    if (id < 0.5)
    {
        _462 = orig.y > 576.0;
    }
    else
    {
        bool _478;
        if (tv)
        {
            _478 = orig.y > (base * 1.5);
        }
        else
        {
            _478 = tv;
        }
        _462 = _478;
    }
    bool scaled = _462;
    if (!scaled)
    {
        return 1.0;
    }
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        bool _501 = lines == floor(lines);
        bool _509;
        if (_501)
        {
            _509 = lines >= (base * 0.829999983310699462890625);
        }
        else
        {
            _509 = _501;
        }
        bool _517;
        if (_509)
        {
            _517 = lines <= (base * 1.21000003814697265625);
        }
        else
        {
            _517 = _509;
        }
        if (_517)
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
    bool _215 = !mon15;
    bool _242;
    if (_215)
    {
        bool _224 = (w == 320.0) && (h == 200.0);
        bool _241;
        if (!_224)
        {
            bool _230 = w == 640.0;
            bool _240;
            if (_230)
            {
                _240 = (h == 350.0) || (h == 400.0);
            }
            else
            {
                _240 = _230;
            }
            _241 = _240;
        }
        else
        {
            _241 = _224;
        }
        _242 = _241;
    }
    else
    {
        _242 = _215;
    }
    if (_242)
    {
        return 7.0;
    }
    bool _253 = (h == 256.0) || (h == 512.0);
    bool _262;
    if (!_253)
    {
        _262 = (w == 640.0) && (h == 200.0);
    }
    else
    {
        _262 = _253;
    }
    if (_262)
    {
        return 6.0;
    }
    bool _275;
    if (mon15)
    {
        _275 = (h == 200.0) || (h == 400.0);
    }
    else
    {
        _275 = mon15;
    }
    bool _283;
    if (_275)
    {
        _283 = (w == 320.0) || (w == 640.0);
    }
    else
    {
        _283 = _275;
    }
    if (_283)
    {
        return 6.0;
    }
    bool _297 = ((w == 360.0) || (w == 720.0)) || (w == 1440.0);
    bool _315;
    if (_297)
    {
        _315 = (((h == 287.0) || (h == 574.0)) || (h == 242.0)) || (h == 484.0);
    }
    else
    {
        _315 = _297;
    }
    if (_315)
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
    float _549;
    if (global.LR_CONSOLE < 0.5)
    {
        vec2 param_1 = nat;
        _549 = autoConsole(param_1);
    }
    else
    {
        _549 = global.LR_CONSOLE;
    }
    float id = _549;
    float param_2 = id;
    vec4 param_3;
    vec4 param_4;
    float param_5;
    consoleTable(param_2, param_3, param_4, param_5);
    vec4 a = param_3;
    vec4 b = param_4;
    float c = param_5;
    t.id = id;
    bool _578 = (id > 4.5) && (id < 5.5);
    bool _585;
    if (_578)
    {
        _585 = nat.x > 700.0;
    }
    else
    {
        _585 = _578;
    }
    if (_585)
    {
        a.z = 720.0;
    }
    t.scan = a.w;
    bool _597 = t.scan < 0.5;
    bool _603;
    if (_597)
    {
        _603 = nat.y > 300.0;
    }
    else
    {
        _603 = _597;
    }
    t.interlace = float(_603);
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
            float _632;
            if (t.interlace > 0.5)
            {
                _632 = nat.y * 0.5;
            }
            else
            {
                _632 = nat.y;
            }
            float lines = _632;
            bool _644 = t.scan < 0.5;
            bool _653;
            if (_644)
            {
                _653 = lines > (amiga ? 249.5 : 240.5);
            }
            else
            {
                _653 = _644;
            }
            t.pal = float(_653);
        }
    }
    float base = mix(a.x, a.y, t.pal);
    t.pclk = (base * orig.x) / a.z;
    float _677;
    if (orig.x < 480.0)
    {
        _677 = 1.0;
    }
    else
    {
        _677 = (orig.x < 960.0) ? 2.0 : 4.0;
    }
    float cls = _677;
    if (amiga)
    {
        t.pclk = (base * (320.0 * cls)) / a.z;
    }
    bool _700 = t.scan > 0.5;
    bool _706;
    if (_700)
    {
        _706 = t.scan < 1.5;
    }
    else
    {
        _706 = _700;
    }
    bool _712;
    if (_706)
    {
        _712 = orig.x > 720.0;
    }
    else
    {
        _712 = _706;
    }
    if (_712)
    {
        t.pclk = ((78.75 * orig.x) * orig.y) / 786432.0;
    }
    float _727;
    if (amiga)
    {
        _727 = cls;
    }
    else
    {
        _727 = orig.x / a.z;
    }
    t.hres = _727;
    t.fsc = (t.pal > 0.5) ? 4.433619022369384765625 : 3.57954502105712890625;
    float param_6 = global.LR_CABLE;
    if (isShow(param_6))
    {
        b = vec4(0.5, 0.75, 0.75, 0.875);
        c = 0.0;
    }
    t.lineFrac = mix(b.x, b.y, t.pal);
    t.fieldStep = mix(b.z, b.w, t.pal);
    float _774;
    if (global.LR_STEADY > 0.5)
    {
        _774 = 0.0;
    }
    else
    {
        _774 = float(frameCount % 24u);
    }
    float fc = _774;
    t.framePhase = fract(fc * t.fieldStep);
    t.vsw = mod(fc * c, 2.0);
    t.vswStep = c;
    return t;
}

vec3 srcPix(float xs, float y, float k)
{
    bool _1062 = xs < 0.0;
    bool _1074;
    if (!_1062)
    {
        _1074 = xs >= params.SourceSize.x;
    }
    else
    {
        _1074 = _1062;
    }
    if (_1074)
    {
        return vec3(0.0);
    }
    return textureLod(Source, vec2((floor(xs) + 0.5) * params.SourceSize.z, (y + 0.5) * params.SourceSize.w), 0.0).xyz;
}

vec3 gaussCdf(vec3 z)
{
    vec3 x = abs(z) * 0.707106769084930419921875;
    vec3 t = vec3(1.0) / (vec3(1.0) + (x * 0.3275910913944244384765625));
    vec3 p = ((((((((t * 1.0614054203033447265625) - vec3(1.45315206050872802734375)) * t) + vec3(1.42141377925872802734375)) * t) - vec3(0.284496724605560302734375)) * t) + vec3(0.254829585552215576171875)) * t;
    return vec3(0.5) + ((sign(z) * 0.5) * (vec3(1.0) - (p * exp((-x) * x))));
}

float dbSign(int k)
{
    return ((abs(k) & 1) == 1) ? (-1.0) : 1.0;
}

float nativeLine(float y, Timing t)
{
    return floor(y / t.scale);
}

bool oddField(float y, Timing t)
{
    bool _830 = t.interlace > 0.5;
    bool _840;
    if (_830)
    {
        float param = y;
        Timing param_1 = t;
        _840 = mod(nativeLine(param, param_1), 2.0) > 0.5;
    }
    else
    {
        _840 = _830;
    }
    return _840;
}

float physLine(float y, Timing t)
{
    float param = y;
    Timing param_1 = t;
    float l = nativeLine(param, param_1);
    float _817;
    if (t.interlace > 0.5)
    {
        _817 = floor(l * 0.5);
    }
    else
    {
        _817 = l;
    }
    return _817;
}

float phaseAtOff(float tUs, float y, Timing t, float off)
{
    float param = y;
    Timing param_1 = t;
    float _849;
    if (oddField(param, param_1))
    {
        _849 = t.fieldStep;
    }
    else
    {
        _849 = 0.0;
    }
    float fo = _849;
    float param_2 = y;
    Timing param_3 = t;
    return 6.283185482025146484375 * fract((((fract(t.fsc * tUs) + fract(t.lineFrac * physLine(param_2, param_3))) + fo) + t.framePhase) + off);
}

float phaseAt(float tUs, float y, Timing t)
{
    float param = tUs;
    float param_1 = y;
    Timing param_2 = t;
    float param_3 = 0.0;
    return phaseAtOff(param, param_1, param_2, param_3);
}

vec3 encYUV(float n, float y, float fs, Timing T, float trap, float blend, bool show)
{
    float ppx = T.pclk / fs;
    float xs = (n + 0.5) * ppx;
    float param = xs;
    float param_1 = y;
    float param_2 = T.scale;
    vec3 c = srcPix(param, param_1, param_2);
    float Y = dot(c, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
    float _1127;
    if (show)
    {
        _1127 = global.LR_TAPE;
    }
    else
    {
        _1127 = 0.0;
    }
    float copies = _1127;
    if (copies > 0.0)
    {
        float sp = (0.024090908467769622802734375 * sqrt(copies)) * T.pclk;
        float j0 = floor(xs - (3.0 * sp));
        float j1 = floor(xs + (3.0 * sp));
        float ay = 0.0;
        float wy = 0.0;
        for (int k = 0; k < 16; k++)
        {
            float j = j0 + float(k);
            if (j > j1)
            {
                break;
            }
            vec3 param_3 = vec3(j - xs, (j + 1.0) - xs, 0.0) / vec3(sp);
            vec3 z = gaussCdf(param_3);
            float w = z.y - z.x;
            float param_4 = j + 0.5;
            float param_5 = y;
            float param_6 = T.scale;
            ay += (w * dot(srcPix(param_4, param_5, param_6), vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625)));
            wy += w;
        }
        Y = ay / wy;
    }
    if (blend > 0.0)
    {
        float sp_1 = T.scale;
        float j0_1 = floor(xs / sp_1);
        float param_7 = (j0_1 + 0.5) * sp_1;
        float param_8 = y;
        float param_9 = T.scale;
        float y0 = dot(srcPix(param_7, param_8, param_9), vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
        float a = 0.0;
        for (int k_1 = -4; k_1 <= 4; k_1++)
        {
            float param_10 = ((j0_1 + float(k_1)) + 0.5) * sp_1;
            float param_11 = y;
            float param_12 = T.scale;
            float yk = dot(srcPix(param_10, param_11, param_12), vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
            int param_13 = k_1;
            a += ((_1280[abs(k_1)] * dbSign(param_13)) * (yk - y0));
        }
        Y -= ((blend * a) / 3.12913608551025390625);
    }
    float param_14 = n / fs;
    float param_15 = y;
    Timing param_16 = T;
    float ph = phaseAt(param_14, param_15, param_16);
    float _1318;
    if (show)
    {
        _1318 = 0.132499992847442626953125 * sqrt((copies / 4.409999847412109375) + 0.591715991497039794921875);
    }
    else
    {
        _1318 = 0.088333331048488616943359375;
    }
    float sigUs = _1318;
    vec2 uv = vec2(0.0);
    float ws = 0.0;
    for (int k_2 = -8; k_2 <= 8; k_2++)
    {
        float u = float(k_2) * 0.4000000059604644775390625;
        float w_1 = exp(((-0.5) * u) * u);
        float param_17 = xs + ((u * sigUs) * T.pclk);
        float param_18 = y;
        float param_19 = T.scale;
        vec3 cc = srcPix(param_17, param_18, param_19);
        float yy = dot(cc, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
        uv += (vec2(0.4920000135898590087890625 * (cc.z - yy), 0.87699997425079345703125 * (cc.x - yy)) * w_1);
        ws += w_1;
    }
    uv /= vec2(ws);
    if (trap > 0.0)
    {
        float sigT = 0.26499998569488525390625;
        float da = ((6.283185482025146484375 * T.fsc) * 0.25) * sigT;
        vec2 rp = vec2(cos(da), sin(da));
        vec2 e = vec2(cos(ph - (12.0 * da)), sin(ph - (12.0 * da)));
        vec2 yb = vec2(0.0);
        float wt = 0.0;
        for (int k_3 = -12; k_3 <= 12; k_3++)
        {
            float u_1 = float(k_3) * 0.25;
            float w_2 = exp(((-0.5) * u_1) * u_1);
            float param_20 = xs + ((u_1 * sigT) * T.pclk);
            float param_21 = y;
            float param_22 = T.scale;
            float yy_1 = dot(srcPix(param_20, param_21, param_22), vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
            yb += (e.yx * (w_2 * yy_1));
            wt += w_2;
            e = vec2((e.x * rp.x) - (e.y * rp.y), (e.y * rp.x) + (e.x * rp.y));
        }
        Y -= (((trap * 2.0) * ((yb.x * sin(ph)) + (yb.y * cos(ph)))) / wt);
    }
    return vec3(Y, uv);
}

int steadyFrames(Timing t, out vec3 off, out vec3 vsw)
{
    off = vec3(0.0);
    vsw = vec3(t.vsw, 0.0, 0.0);
    if (global.LR_STEADY < 0.5)
    {
        return 1;
    }
    float s = t.fieldStep;
    bool _937 = abs(s - 0.3333333432674407958984375) < 0.00999999977648258209228515625;
    bool _946;
    if (!_937)
    {
        _946 = abs(s - 0.666666686534881591796875) < 0.00999999977648258209228515625;
    }
    else
    {
        _946 = _937;
    }
    if (_946)
    {
        off = vec3(0.0, fract(s), fract(2.0 * s));
        vsw = vec3(0.0, mod(t.vswStep, 2.0), mod(2.0 * t.vswStep, 2.0));
        return 3;
    }
    for (float k = 1.0; k <= 8.0; k += 1.0)
    {
        if (abs(fract(k * s) - 0.5) < 0.00999999977648258209228515625)
        {
            off.y = 0.5;
            vsw.y = mod(k * t.vswStep, 2.0);
            return 2;
        }
    }
    bool _994 = t.pal > 0.5;
    bool _1000;
    if (_994)
    {
        _1000 = t.vswStep > 0.5;
    }
    else
    {
        _1000 = _994;
    }
    if (_1000)
    {
        vsw.y = 1.0;
        return 2;
    }
    return 1;
}

float vSignW(float y, Timing t, float vsw)
{
    if (t.pal < 0.5)
    {
        return 1.0;
    }
    float param = y;
    Timing param_1 = t;
    float param_2 = y;
    Timing param_3 = t;
    float pl = (physLine(param, param_1) + vsw) + float(oddField(param_2, param_3));
    return (mod(pl, 2.0) < 0.5) ? 1.0 : (-1.0);
}

float chromaAt(vec2 uv, float n, float y, float fs, Timing T, float off, float vsw)
{
    float param = n / fs;
    float param_1 = y;
    Timing param_2 = T;
    float param_3 = off;
    float ph = phaseAtOff(param, param_1, param_2, param_3);
    float param_4 = y;
    Timing param_5 = T;
    float param_6 = vsw;
    return (uv.x * sin(ph)) + ((vSignW(param_4, param_5, param_6) * uv.y) * cos(ph));
}

float hash13(inout vec3 p3)
{
    p3 = fract(p3 * 0.103100001811981201171875);
    p3 += vec3(dot(p3, p3.zyx + vec3(31.31999969482421875)));
    return fract((p3.x + p3.y) * p3.z);
}

float whiteClip(float v, bool pal)
{
    float knee = 0.0599999986588954925537109375;
    float k = (pal ? 1.10000002384185791015625 : 1.14999997615814208984375) - knee;
    float _1578;
    if (v < k)
    {
        _1578 = v;
    }
    else
    {
        _1578 = k + (knee * tanh((v - k) / knee));
    }
    return _1578;
}

vec2 rfEcho(float cable)
{
    if (cable < 0.5)
    {
        return vec2(0.119999997317790985107421875, 0.800000011920928955078125);
    }
    float param = cable;
    if (isShow(param))
    {
        return vec2(global.LR_GHOST, 1.2000000476837158203125);
    }
    return vec2(0.0, 1.0);
}

void main()
{
    vec2 param = params.OriginalSize.xy;
    uint param_1 = params.FrameCount;
    Timing T = getTiming(param, param_1);
    float SW = params.OutputSize.x;
    float fs = (T.pclk * SW) / params.SourceSize.x;
    float n = floor(HK_VARY0.x * SW);
    float row = floor(HK_VARY0.y * params.SourceSize.y);
    float y = row * T.scale;
    float cable = global.LR_CABLE;
    float param_2 = cable;
    bool show = isShow(param_2);
    bool _1650 = (cable > 2.5) && (!show);
    bool _1658;
    if (!_1650)
    {
        _1658 = y >= params.SourceSize.y;
    }
    else
    {
        _1658 = _1650;
    }
    if (_1658)
    {
        FragColor = vec4(0.0);
        return;
    }
    float _1670;
    if ((cable < 1.5) && (!show))
    {
        _1670 = global.LR_YTRAP;
    }
    else
    {
        _1670 = 0.0;
    }
    float trap = _1670;
    float _1683;
    if ((cable > 1.5) && (!show))
    {
        _1683 = global.LR_DBLEND;
    }
    else
    {
        _1683 = 0.0;
    }
    float blend = _1683;
    float param_3 = n;
    float param_4 = y;
    float param_5 = fs;
    Timing param_6 = T;
    float param_7 = trap;
    float param_8 = blend;
    bool param_9 = show;
    vec3 a = encYUV(param_3, param_4, param_5, param_6, param_7, param_8, param_9);
    Timing param_10 = T;
    vec3 param_11;
    vec3 param_12;
    int _1713 = steadyFrames(param_10, param_11, param_12);
    vec3 off = param_11;
    vec3 vsw = param_12;
    int nf = _1713;
    vec2 param_13 = a.yz;
    float param_14 = n;
    float param_15 = y;
    float param_16 = fs;
    Timing param_17 = T;
    float param_18 = off.x;
    float param_19 = vsw.x;
    vec3 cr = vec3(chromaAt(param_13, param_14, param_15, param_16, param_17, param_18, param_19), 0.0, 0.0);
    if (nf > 1)
    {
        vec2 param_20 = a.yz;
        float param_21 = n;
        float param_22 = y;
        float param_23 = fs;
        Timing param_24 = T;
        float param_25 = off.y;
        float param_26 = vsw.y;
        cr.y = chromaAt(param_20, param_21, param_22, param_23, param_24, param_25, param_26);
    }
    if (nf > 2)
    {
        vec2 param_27 = a.yz;
        float param_28 = n;
        float param_29 = y;
        float param_30 = fs;
        Timing param_31 = T;
        float param_32 = off.z;
        float param_33 = vsw.z;
        cr.z = chromaAt(param_27, param_28, param_29, param_30, param_31, param_32, param_33);
    }
    if ((cable > 1.5) && (!show))
    {
        vec4 _1791;
        if (nf > 1)
        {
            _1791 = vec4(a.x, cr);
        }
        else
        {
            _1791 = vec4(a.x, cr.x, 0.0, 1.0);
        }
        FragColor = _1791;
        return;
    }
    vec3 comp = vec3(a.x) + cr;
    if (cable < 0.5)
    {
        float param_34 = n - (0.800000011920928955078125 * fs);
        float param_35 = y;
        float param_36 = fs;
        Timing param_37 = T;
        float param_38 = trap;
        float param_39 = 0.0;
        bool param_40 = false;
        vec3 g = encYUV(param_34, param_35, param_36, param_37, param_38, param_39, param_40);
        float ne = n - (0.800000011920928955078125 * fs);
        vec2 param_41 = g.yz;
        float param_42 = ne;
        float param_43 = y;
        float param_44 = fs;
        Timing param_45 = T;
        float param_46 = off.x;
        float param_47 = vsw.x;
        comp.x += (0.119999997317790985107421875 * (g.x + chromaAt(param_41, param_42, param_43, param_44, param_45, param_46, param_47)));
        if (nf > 1)
        {
            vec2 param_48 = g.yz;
            float param_49 = ne;
            float param_50 = y;
            float param_51 = fs;
            Timing param_52 = T;
            float param_53 = off.y;
            float param_54 = vsw.y;
            comp.y += (0.119999997317790985107421875 * (g.x + chromaAt(param_48, param_49, param_50, param_51, param_52, param_53, param_54)));
        }
        if (nf > 2)
        {
            vec2 param_55 = g.yz;
            float param_56 = ne;
            float param_57 = y;
            float param_58 = fs;
            Timing param_59 = T;
            float param_60 = off.z;
            float param_61 = vsw.z;
            comp.z += (0.119999997317790985107421875 * (g.x + chromaAt(param_55, param_56, param_57, param_58, param_59, param_60, param_61)));
        }
        float seed = float(params.FrameCount % 997u);
        vec3 param_62 = vec3(n, y, seed);
        float _1939 = hash13(param_62);
        comp += vec3((_1939 - 0.5) * global.LR_NOISE);
        vec3 param_63 = vec3(floor(n / 7.0), y, seed + 3.0);
        float _1956 = hash13(param_63);
        comp += vec3(((_1956 - 0.5) * global.LR_NOISE) * 0.800000011920928955078125);
        float param_64 = y;
        Timing param_65 = T;
        comp += vec3((sin((nativeLine(param_64, param_65) * 0.04500000178813934326171875) + (seed * 0.37000000476837158203125)) * global.LR_NOISE) * 0.119999997317790985107421875);
    }
    else
    {
        if (show)
        {
            bool pal = T.pal > 0.5;
            float param_66 = comp.x;
            bool param_67 = pal;
            float param_68 = comp.y;
            bool param_69 = pal;
            float param_70 = comp.z;
            bool param_71 = pal;
            comp = vec3(whiteClip(param_66, param_67), whiteClip(param_68, param_69), whiteClip(param_70, param_71));
            float param_72 = cable;
            vec2 e = rfEcho(param_72);
            if (e.x > 0.0)
            {
                float ne_1 = n - (e.y * fs);
                float param_73 = ne_1;
                float param_74 = y;
                float param_75 = fs;
                Timing param_76 = T;
                float param_77 = 0.0;
                float param_78 = 0.0;
                bool param_79 = true;
                vec3 g_1 = encYUV(param_73, param_74, param_75, param_76, param_77, param_78, param_79);
                vec2 param_80 = g_1.yz;
                float param_81 = ne_1;
                float param_82 = y;
                float param_83 = fs;
                Timing param_84 = T;
                float param_85 = off.x;
                float param_86 = vsw.x;
                float param_87 = g_1.x + chromaAt(param_80, param_81, param_82, param_83, param_84, param_85, param_86);
                bool param_88 = pal;
                comp.x += (e.x * whiteClip(param_87, param_88));
                if (nf > 1)
                {
                    vec2 param_89 = g_1.yz;
                    float param_90 = ne_1;
                    float param_91 = y;
                    float param_92 = fs;
                    Timing param_93 = T;
                    float param_94 = off.y;
                    float param_95 = vsw.y;
                    float param_96 = g_1.x + chromaAt(param_89, param_90, param_91, param_92, param_93, param_94, param_95);
                    bool param_97 = pal;
                    comp.y += (e.x * whiteClip(param_96, param_97));
                }
                if (nf > 2)
                {
                    vec2 param_98 = g_1.yz;
                    float param_99 = ne_1;
                    float param_100 = y;
                    float param_101 = fs;
                    Timing param_102 = T;
                    float param_103 = off.z;
                    float param_104 = vsw.z;
                    float param_105 = g_1.x + chromaAt(param_98, param_99, param_100, param_101, param_102, param_103, param_104);
                    bool param_106 = pal;
                    comp.z += (e.x * whiteClip(param_105, param_106));
                }
            }
            float seed_1 = float(params.FrameCount % 997u);
            vec3 param_107 = vec3(n, y, seed_1);
            float _2155 = hash13(param_107);
            comp += vec3(((_2155 - 0.5) * global.LR_NOISE) * sqrt(fs / 26.6000003814697265625));
        }
    }
    if (nf < 2)
    {
        comp.y = 0.0;
    }
    if (nf < 3)
    {
        comp.z = 0.0;
    }
    FragColor = vec4(comp, 1.0);
}

