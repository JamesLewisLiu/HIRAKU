// pass 2: ../shaders/decode.slang, converted by tools/slang2glsl.py
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

const float _1379[5] = float[](1.0, 0.726149022579193115234375, 0.278037011623382568359375, 0.056134998798370361328125, 0.005975999869406223297119140625);

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
uniform sampler2D Original;

in vec2 HK_VARY0;
layout(location = 0) out vec4 FragColor;

float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool _383 = id < 5.5;
    bool _392;
    if (!_383)
    {
        _392 = (id > 7.5) && (id < 8.5);
    }
    else
    {
        _392 = _383;
    }
    bool tv = _392;
    float base = ((id > 4.5) && (id < 5.5)) ? 480.0 : 240.0;
    bool _405;
    if (id < 0.5)
    {
        _405 = orig.y > 576.0;
    }
    else
    {
        bool _421;
        if (tv)
        {
            _421 = orig.y > (base * 1.5);
        }
        else
        {
            _421 = tv;
        }
        _405 = _421;
    }
    bool scaled = _405;
    if (!scaled)
    {
        return 1.0;
    }
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        bool _444 = lines == floor(lines);
        bool _452;
        if (_444)
        {
            _452 = lines >= (base * 0.829999983310699462890625);
        }
        else
        {
            _452 = _444;
        }
        bool _460;
        if (_452)
        {
            _460 = lines <= (base * 1.21000003814697265625);
        }
        else
        {
            _460 = _452;
        }
        if (_460)
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
    bool _157 = !mon15;
    bool _184;
    if (_157)
    {
        bool _166 = (w == 320.0) && (h == 200.0);
        bool _183;
        if (!_166)
        {
            bool _172 = w == 640.0;
            bool _182;
            if (_172)
            {
                _182 = (h == 350.0) || (h == 400.0);
            }
            else
            {
                _182 = _172;
            }
            _183 = _182;
        }
        else
        {
            _183 = _166;
        }
        _184 = _183;
    }
    else
    {
        _184 = _157;
    }
    if (_184)
    {
        return 7.0;
    }
    bool _195 = (h == 256.0) || (h == 512.0);
    bool _204;
    if (!_195)
    {
        _204 = (w == 640.0) && (h == 200.0);
    }
    else
    {
        _204 = _195;
    }
    if (_204)
    {
        return 6.0;
    }
    bool _217;
    if (mon15)
    {
        _217 = (h == 200.0) || (h == 400.0);
    }
    else
    {
        _217 = mon15;
    }
    bool _225;
    if (_217)
    {
        _225 = (w == 320.0) || (w == 640.0);
    }
    else
    {
        _225 = _217;
    }
    if (_225)
    {
        return 6.0;
    }
    bool _239 = ((w == 360.0) || (w == 720.0)) || (w == 1440.0);
    bool _257;
    if (_239)
    {
        _257 = (((h == 287.0) || (h == 574.0)) || (h == 242.0)) || (h == 484.0);
    }
    else
    {
        _257 = _239;
    }
    if (_257)
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
    float _492;
    if (global.LR_CONSOLE < 0.5)
    {
        vec2 param_1 = nat;
        _492 = autoConsole(param_1);
    }
    else
    {
        _492 = global.LR_CONSOLE;
    }
    float id = _492;
    float param_2 = id;
    vec4 param_3;
    vec4 param_4;
    float param_5;
    consoleTable(param_2, param_3, param_4, param_5);
    vec4 a = param_3;
    vec4 b = param_4;
    float c = param_5;
    t.id = id;
    bool _521 = (id > 4.5) && (id < 5.5);
    bool _528;
    if (_521)
    {
        _528 = nat.x > 700.0;
    }
    else
    {
        _528 = _521;
    }
    if (_528)
    {
        a.z = 720.0;
    }
    t.scan = a.w;
    bool _541 = t.scan < 0.5;
    bool _547;
    if (_541)
    {
        _547 = nat.y > 300.0;
    }
    else
    {
        _547 = _541;
    }
    t.interlace = float(_547);
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
            float _576;
            if (t.interlace > 0.5)
            {
                _576 = nat.y * 0.5;
            }
            else
            {
                _576 = nat.y;
            }
            float lines = _576;
            bool _588 = t.scan < 0.5;
            bool _597;
            if (_588)
            {
                _597 = lines > (amiga ? 249.5 : 240.5);
            }
            else
            {
                _597 = _588;
            }
            t.pal = float(_597);
        }
    }
    float base = mix(a.x, a.y, t.pal);
    t.pclk = (base * orig.x) / a.z;
    float _621;
    if (orig.x < 480.0)
    {
        _621 = 1.0;
    }
    else
    {
        _621 = (orig.x < 960.0) ? 2.0 : 4.0;
    }
    float cls = _621;
    if (amiga)
    {
        t.pclk = (base * (320.0 * cls)) / a.z;
    }
    bool _644 = t.scan > 0.5;
    bool _650;
    if (_644)
    {
        _650 = t.scan < 1.5;
    }
    else
    {
        _650 = _644;
    }
    bool _656;
    if (_650)
    {
        _656 = orig.x > 720.0;
    }
    else
    {
        _656 = _650;
    }
    if (_656)
    {
        t.pclk = ((78.75 * orig.x) * orig.y) / 786432.0;
    }
    float _671;
    if (amiga)
    {
        _671 = cls;
    }
    else
    {
        _671 = orig.x / a.z;
    }
    t.hres = _671;
    t.fsc = (t.pal > 0.5) ? 4.433619022369384765625 : 3.57954502105712890625;
    float param_6 = global.LR_CABLE;
    if (isShow(param_6))
    {
        b = vec4(0.5, 0.75, 0.75, 0.875);
        c = 0.0;
    }
    t.lineFrac = mix(b.x, b.y, t.pal);
    t.fieldStep = mix(b.z, b.w, t.pal);
    float _718;
    if (global.LR_STEADY > 0.5)
    {
        _718 = 0.0;
    }
    else
    {
        _718 = float(frameCount % 24u);
    }
    float fc = _718;
    t.framePhase = fract(fc * t.fieldStep);
    t.vsw = mod(fc * c, 2.0);
    t.vswStep = c;
    return t;
}

bool isPC(float scan)
{
    return (scan > 0.5) && (scan < 1.5);
}

vec3 origPix(float xs, float y, float k)
{
    bool _1117 = xs < 0.0;
    bool _1125;
    if (!_1117)
    {
        _1125 = xs >= params.OriginalSize.x;
    }
    else
    {
        _1125 = _1117;
    }
    if (_1125)
    {
        return vec3(0.0);
    }
    float u = (floor(xs) + 0.5) * params.OriginalSize.z;
    vec3 c = vec3(0.0);
    for (float r = 0.0; r < k; r += 1.0)
    {
        c += textureLod(Original, vec2(u, ((y + r) + 0.5) * params.OriginalSize.w), 0.0).xyz;
    }
    return c / vec3(k);
}

float dbSign(int k)
{
    return ((abs(k) & 1) == 1) ? (-1.0) : 1.0;
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
    bool _891 = abs(s - 0.3333333432674407958984375) < 0.00999999977648258209228515625;
    bool _900;
    if (!_891)
    {
        _900 = abs(s - 0.666666686534881591796875) < 0.00999999977648258209228515625;
    }
    else
    {
        _900 = _891;
    }
    if (_900)
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
    bool _948 = t.pal > 0.5;
    bool _954;
    if (_948)
    {
        _954 = t.vswStep > 0.5;
    }
    else
    {
        _954 = _948;
    }
    if (_954)
    {
        vsw.y = 1.0;
        return 2;
    }
    return 1;
}

vec4 sig(float s, float r)
{
    if (r < 0.0)
    {
        return vec4(0.0);
    }
    return textureLod(Source, vec2((s + 0.5) * params.SourceSize.z, (r + 0.5) * params.SourceSize.w), 0.0);
}

float nativeLine(float y, Timing t)
{
    return floor(y / t.scale);
}

float physLine(float y, Timing t)
{
    float param = y;
    Timing param_1 = t;
    float l = nativeLine(param, param_1);
    float _761;
    if (t.interlace > 0.5)
    {
        _761 = floor(l * 0.5);
    }
    else
    {
        _761 = l;
    }
    return _761;
}

bool oddField(float y, Timing t)
{
    bool _774 = t.interlace > 0.5;
    bool _784;
    if (_774)
    {
        float param = y;
        Timing param_1 = t;
        _784 = mod(nativeLine(param, param_1), 2.0) > 0.5;
    }
    else
    {
        _784 = _774;
    }
    return _784;
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

float vSign(float y, Timing t)
{
    float param = y;
    Timing param_1 = t;
    float param_2 = t.vsw;
    return vSignW(param, param_1, param_2);
}

float phaseAtOff(float tUs, float y, Timing t, float off)
{
    float param = y;
    Timing param_1 = t;
    float _793;
    if (oddField(param, param_1))
    {
        _793 = t.fieldStep;
    }
    else
    {
        _793 = 0.0;
    }
    float fo = _793;
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

vec2 rotate(vec2 e, vec2 r)
{
    return vec2((e.x * r.x) - (e.y * r.y), (e.y * r.x) + (e.x * r.y));
}

float decoderType(Timing t, float cable)
{
    if (global.LR_DEMOD > 0.5)
    {
        return global.LR_DEMOD;
    }
    bool _978 = t.pal < 0.5;
    bool _990;
    if (_978)
    {
        bool _982 = cable < 2.5;
        bool _989;
        if (!_982)
        {
            float param = cable;
            _989 = isShow(param);
        }
        else
        {
            _989 = _982;
        }
        _990 = _989;
    }
    else
    {
        _990 = _978;
    }
    return _990 ? 2.0 : 1.0;
}

vec3 decodeRGB(float Y, vec2 uv, float dec)
{
    if (dec < 1.5)
    {
        return vec3(Y + (1.13982999324798583984375 * uv.y), (Y - (0.3946500122547149658203125 * uv.x)) - (0.5806000232696533203125 * uv.y), Y + (2.0321099758148193359375 * uv.x));
    }
    bool us = dec < 2.5;
    float ar = radians(us ? 112.0 : 95.0);
    float gr = us ? 0.829999983310699462890625 : 0.7799999713897705078125;
    float ag = radians(us ? 252.0 : 240.0);
    float gg = 0.300000011920928955078125;
    float ry = (((uv.x * cos(ar)) + (uv.y * sin(ar))) * gr) / 0.4920000135898590087890625;
    float gy = (((uv.x * cos(ag)) + (uv.y * sin(ag))) * gg) / 0.4920000135898590087890625;
    return vec3(Y + ry, Y + gy, Y + (uv.x / 0.4920000135898590087890625));
}

void main()
{
    vec2 param = params.OriginalSize.xy;
    uint param_1 = params.FrameCount;
    Timing T = getTiming(param, param_1);
    float SW = params.SourceSize.x;
    float fs = (T.pclk * SW) / params.OriginalSize.x;
    float n = floor(HK_VARY0.x * SW);
    float r = floor(HK_VARY0.y * params.SourceSize.y);
    float y = r * T.scale;
    float st = ((T.interlace > 0.5) ? 2.0 : 1.0) * T.scale;
    float sr = st / T.scale;
    float cable = global.LR_CABLE;
    float param_2 = cable;
    bool show = isShow(param_2);
    if (y >= params.OriginalSize.y)
    {
        FragColor = vec4(0.0);
        return;
    }
    float Y;
    vec2 uv;
    if ((cable > 2.5) && (!show))
    {
        float xs = ((n + 0.5) * T.pclk) / fs;
        float param_3 = T.scan;
        float sigPx = (0.132499992847442626953125 / (isPC(param_3) ? 85.0 : 6.5)) * T.pclk;
        float db = global.LR_DBLEND;
        float sp = T.scale;
        float j0 = floor(xs / sp);
        vec3 alt[5];
        for (int i = 0; i < 5; i++)
        {
            alt[i] = vec3(0.0);
        }
        if (db > 0.0)
        {
            vec3 p[13];
            for (int i_1 = 0; i_1 < 13; i_1++)
            {
                float param_4 = ((j0 + float(i_1 - 6)) + 0.5) * sp;
                float param_5 = y;
                float param_6 = T.scale;
                p[i_1] = origPix(param_4, param_5, param_6);
            }
            for (int i_2 = 0; i_2 < 5; i_2++)
            {
                vec3 a = vec3(0.0);
                for (int k = -4; k <= 4; k++)
                {
                    int param_7 = k;
                    a += ((p[(i_2 + 4) + k] - p[i_2 + 4]) * (_1379[abs(k)] * dbSign(param_7)));
                }
                alt[i_2] = (a * db) / vec3(3.12913608551025390625);
            }
        }
        vec3 acc = vec3(0.0);
        float ws = 0.0;
        for (int k_1 = -3; k_1 <= 3; k_1++)
        {
            float w = exp((-0.5) * float(k_1 * k_1));
            float xt = xs + (float(k_1) * sigPx);
            float param_8 = xt;
            float param_9 = y;
            float param_10 = T.scale;
            vec3 v = origPix(param_8, param_9, param_10);
            if (db > 0.0)
            {
                v -= alt[clamp(int(floor(xt / sp) - j0) + 2, 0, 4)];
            }
            acc += (v * w);
            ws += w;
        }
        vec3 c = acc / vec3(ws);
        Y = dot(c, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
        uv = vec2(0.4920000135898590087890625 * (c.z - Y), 0.87699997425079345703125 * (c.x - Y));
    }
    else
    {
        bool pal = T.pal > 0.5;
        bool sv = (cable > 1.5) && (!show);
        bool _1526 = (((global.LR_COMB > 0.5) && (!sv)) && (!pal)) && (cable > 0.5);
        bool _1535;
        if (_1526)
        {
            _1535 = abs(T.lineFrac - 0.5) < 0.0500000007450580596923828125;
        }
        else
        {
            _1535 = _1526;
        }
        bool comb = _1535;
        float fcY;
        float notch;
        float fcC;
        if (cable < 0.5)
        {
            fcY = pal ? 3.599999904632568359375 : 2.7999999523162841796875;
            notch = 0.949999988079071044921875;
            fcC = 0.5;
        }
        else
        {
            if (!sv)
            {
                float _1554;
                if (comb)
                {
                    _1554 = 4.19999980926513671875;
                }
                else
                {
                    _1554 = pal ? 4.19999980926513671875 : 3.2999999523162841796875;
                }
                fcY = _1554;
                notch = comb ? 0.0 : 0.920000016689300537109375;
                fcC = pal ? 0.800000011920928955078125 : 0.60000002384185791015625;
            }
            else
            {
                fcY = pal ? 5.0 : 4.599999904632568359375;
                notch = 0.0;
                fcC = pal ? 1.0 : 0.89999997615814208984375;
            }
        }
        float tn = n / fs;
        float sh = global.LR_SHARP * 1.39999997615814208984375;
        Timing param_11 = T;
        vec3 param_12;
        vec3 param_13;
        int _1593 = steadyFrames(param_11, param_12, param_13);
        vec3 off = param_12;
        vec3 vsw = param_13;
        int nf = _1593;
        bvec3 _1602 = bvec3(nf == 3);
        vec3 lw = vec3(_1602.x ? vec3(0.3333333432674407958984375).x : vec3(0.5, 0.5, 0.0).x, _1602.y ? vec3(0.3333333432674407958984375).y : vec3(0.5, 0.5, 0.0).y, _1602.z ? vec3(0.3333333432674407958984375).z : vec3(0.5, 0.5, 0.0).z);
        vec2 r1 = vec2(cos(6.283185482025146484375 * off.y), sin(6.283185482025146484375 * off.y));
        vec2 r2 = vec2(cos(6.283185482025146484375 * off.z), sin(6.283185482025146484375 * off.z));
        float s1 = 0.132499992847442626953125 / fcY;
        float s2 = s1 * 2.5;
        float mY = max(1.0, ceil((0.25 * s2) * fs));
        float nY = min(ceil(((3.0 * s2) * fs) / mY), 12.0);
        float c1 = (((-0.5) * mY) * mY) / (((s1 * s1) * fs) * fs);
        float c2 = (((-0.5) * mY) * mY) / (((s2 * s2) * fs) * fs);
        float a1 = 0.0;
        float a2 = 0.0;
        float w1s = 0.0;
        float w2s = 0.0;
        int nYi = int(nY);
        int _1684 = -nYi;
        float _1727;
        float _1752;
        for (int k_2 = _1684; k_2 <= nYi; k_2++)
        {
            float u = float(k_2);
            float w1 = exp((c1 * u) * u);
            float w2 = exp((c2 * u) * u);
            float sp_1 = n + (u * mY);
            float param_14 = sp_1;
            float param_15 = r;
            vec4 v_1 = sig(param_14, param_15);
            if ((nf == 1) || sv)
            {
                _1727 = v_1.x;
            }
            else
            {
                _1727 = dot(v_1.xyz, lw);
            }
            float s0 = _1727;
            if (comb)
            {
                float param_16 = sp_1;
                float param_17 = r - sr;
                vec4 vq = sig(param_16, param_17);
                if (nf == 1)
                {
                    _1752 = vq.x;
                }
                else
                {
                    _1752 = dot(vq.xyz, lw);
                }
                s0 = 0.5 * (s0 + _1752);
            }
            a1 += (w1 * s0);
            a2 += (w2 * s0);
            w1s += w1;
            w2s += w2;
        }
        Y = (((1.0 + sh) * a1) / w1s) - ((sh * a2) / w2s);
        float sc = 0.132499992847442626953125 / fcC;
        float mC = max(1.0, ceil((0.1500000059604644775390625 * sc) * fs));
        float nC = min(ceil(((3.0 * sc) * fs) / mC), 20.0);
        float cw = (((-0.5) * mC) * mC) / (((sc * sc) * fs) * fs);
        float param_18 = y;
        Timing param_19 = T;
        float vs = vSign(param_18, param_19);
        float param_20 = y - st;
        Timing param_21 = T;
        float vsp = vSign(param_20, param_21);
        float _1845;
        if (pal)
        {
            _1845 = 0.0;
        }
        else
        {
            _1845 = radians(global.LR_TINT);
        }
        float tint = _1845;
        if (cable < 0.5)
        {
            float ea = ((-6.283185482025146484375) * T.fsc) * 0.800000011920928955078125;
            tint += atan(0.119999997317790985107421875 * sin(ea), 1.0 + (0.119999997317790985107421875 * cos(ea)));
        }
        else
        {
            bool _1882;
            if (show)
            {
                _1882 = global.LR_GHOST > 0.0;
            }
            else
            {
                _1882 = show;
            }
            if (_1882)
            {
                float ea_1 = ((-6.283185482025146484375) * T.fsc) * 1.2000000476837158203125;
                tint += atan(global.LR_GHOST * sin(ea_1), 1.0 + (global.LR_GHOST * cos(ea_1)));
            }
        }
        float param_22 = tn;
        float param_23 = y;
        Timing param_24 = T;
        float ph0 = phaseAt(param_22, param_23, param_24) + tint;
        float da = ((6.283185482025146484375 * T.fsc) * mC) / fs;
        vec2 rp = vec2(cos(da), sin(da));
        vec2 e0 = vec2(cos(ph0), sin(ph0));
        float a0 = ph0 - (nC * da);
        a0 -= (6.283185482025146484375 * floor((a0 / 6.283185482025146484375) + 0.5));
        vec2 e = vec2(cos(a0), sin(a0));
        vec2 acc_1 = vec2(0.0);
        vec2 accp = vec2(0.0);
        vec2 acc1 = vec2(0.0);
        vec2 accp1 = vec2(0.0);
        vec2 acc2 = vec2(0.0);
        vec2 accp2 = vec2(0.0);
        float wcs = 0.0;
        int nCi = int(nC);
        int _1967 = -nCi;
        vec3 _2000;
        vec4 _2013;
        float _2050;
        vec3 _2077;
        for (int k_3 = _1967; k_3 <= nCi; k_3++)
        {
            float u_1 = float(k_3);
            float w_1 = exp((cw * u_1) * u_1);
            float sp_2 = n + (u_1 * mC);
            float param_25 = sp_2;
            float param_26 = r;
            vec4 v0 = sig(param_25, param_26);
            if (sv)
            {
                _2000 = v0.yzw;
            }
            else
            {
                _2000 = v0.xyz;
            }
            vec3 c3 = _2000;
            if (pal || comb)
            {
                float param_27 = sp_2;
                float param_28 = r - sr;
                _2013 = sig(param_27, param_28);
            }
            else
            {
                _2013 = vec4(0.0);
            }
            vec4 vp = _2013;
            if (comb)
            {
                c3 = (v0.xyz - vp.xyz) * 0.5;
            }
            float s0_1 = c3.x;
            acc_1 += (e.yx * (w_1 * s0_1));
            if (pal)
            {
                if (sv)
                {
                    _2050 = vp.y;
                }
                else
                {
                    _2050 = vp.x;
                }
                accp += (e.yx * (w_1 * _2050));
            }
            if (nf > 1)
            {
                vec2 param_29 = e;
                vec2 param_30 = r1;
                vec2 e1 = rotate(param_29, param_30);
                if (sv)
                {
                    _2077 = vp.yzw;
                }
                else
                {
                    _2077 = vp.xyz;
                }
                vec3 cp = _2077;
                acc1 += (e1.yx * (w_1 * c3.y));
                if (pal)
                {
                    accp1 += (e1.yx * (w_1 * cp.y));
                }
                if (nf > 2)
                {
                    vec2 param_31 = e;
                    vec2 param_32 = r2;
                    vec2 e2 = rotate(param_31, param_32);
                    acc2 += (e2.yx * (w_1 * c3.z));
                    if (pal)
                    {
                        accp2 += (e2.yx * (w_1 * cp.z));
                    }
                }
            }
            wcs += w_1;
            vec2 param_33 = e;
            vec2 param_34 = rp;
            e = rotate(param_33, param_34);
        }
        vec2 cs = vec2(acc_1.x, vs * acc_1.y) * 2.0;
        uv = cs / vec2(wcs);
        float hf = ((1.0 + sh) * exp(((((-19.739200592041015625) * s1) * s1) * T.fsc) * T.fsc)) - (sh * exp(((((-19.739200592041015625) * s2) * s2) * T.fsc) * T.fsc));
        bool trapY = ((!sv) && (!comb)) && (notch > 0.0);
        float param_35 = tn;
        float param_36 = y - st;
        Timing param_37 = T;
        float dl = phaseAt(param_35, param_36, param_37) - (ph0 - tint);
        vec2 rd = vec2(cos(dl), sin(dl));
        if (nf == 1)
        {
            if (trapY)
            {
                Y -= ((notch * hf) * ((uv.x * e0.y) + ((vs * uv.y) * e0.x)));
            }
            if (pal)
            {
                vec2 csp = vec2((accp.x * rd.x) + (accp.y * rd.y), vsp * ((accp.y * rd.x) - (accp.x * rd.y))) * 2.0;
                uv = (uv + (csp / vec2(wcs))) * 0.5;
            }
        }
        else
        {
            vec2 uvs = vec2(0.0);
            float trapSum = 0.0;
            vec2 _2303;
            vec2 _2319;
            vec2 _2356;
            for (int k_4 = 0; k_4 < 3; k_4++)
            {
                if (k_4 >= nf)
                {
                    break;
                }
                if (k_4 == 0)
                {
                    _2303 = acc_1;
                }
                else
                {
                    bvec2 _2313 = bvec2(k_4 == 1);
                    _2303 = vec2(_2313.x ? acc1.x : acc2.x, _2313.y ? acc1.y : acc2.y);
                }
                vec2 a_1 = _2303;
                if (k_4 == 0)
                {
                    _2319 = accp;
                }
                else
                {
                    bvec2 _2328 = bvec2(k_4 == 1);
                    _2319 = vec2(_2328.x ? accp1.x : accp2.x, _2328.y ? accp1.y : accp2.y);
                }
                vec2 ap = _2319;
                float param_38 = y;
                Timing param_39 = T;
                float param_40 = vsw[k_4];
                float vk = vSignW(param_38, param_39, param_40);
                float param_41 = y - st;
                Timing param_42 = T;
                float param_43 = vsw[k_4];
                float vkp = vSignW(param_41, param_42, param_43);
                if (k_4 == 0)
                {
                    _2356 = e0;
                }
                else
                {
                    bvec2 _2365 = bvec2(k_4 == 1);
                    vec2 param_44 = e0;
                    vec2 param_45 = vec2(_2365.x ? r1.x : r2.x, _2365.y ? r1.y : r2.y);
                    _2356 = rotate(param_44, param_45);
                }
                vec2 ek = _2356;
                vec2 u_2 = (vec2(a_1.x, vk * a_1.y) * 2.0) / vec2(wcs);
                trapSum += ((u_2.x * ek.y) + ((vk * u_2.y) * ek.x));
                if (pal)
                {
                    vec2 csp_1 = vec2((ap.x * rd.x) + (ap.y * rd.y), vkp * ((ap.y * rd.x) - (ap.x * rd.y))) * 2.0;
                    u_2 = (u_2 + (csp_1 / vec2(wcs))) * 0.5;
                }
                uvs += u_2;
            }
            if (trapY)
            {
                Y -= (((notch * hf) * trapSum) / float(nf));
            }
            uv = uvs / vec2(float(nf));
        }
        if (cable < 0.5)
        {
            float ea_2 = ((-6.283185482025146484375) * T.fsc) * 0.800000011920928955078125;
            Y /= 1.12000000476837158203125;
            uv /= vec2(length(vec2(1.0 + (0.119999997317790985107421875 * cos(ea_2)), 0.119999997317790985107421875 * sin(ea_2))));
        }
        else
        {
            bool _2490;
            if (show)
            {
                _2490 = global.LR_GHOST > 0.0;
            }
            else
            {
                _2490 = show;
            }
            if (_2490)
            {
                float ea_3 = ((-6.283185482025146484375) * T.fsc) * 1.2000000476837158203125;
                Y /= (1.0 + global.LR_GHOST);
                uv /= vec2(length(vec2(1.0 + (global.LR_GHOST * cos(ea_3)), global.LR_GHOST * sin(ea_3))));
            }
        }
    }
    uv *= (global.LR_SAT * global.LR_CONTRAST);
    Y = (Y * global.LR_CONTRAST) + global.LR_BRIGHT;
    Timing param_46 = T;
    float param_47 = cable;
    float param_48 = Y;
    vec2 param_49 = uv;
    float param_50 = decoderType(param_46, param_47);
    vec3 rgb = decodeRGB(param_48, param_49, param_50);
    FragColor = vec4(clamp(rgb, vec3(0.0), vec3(1.0)), 1.0);
}

