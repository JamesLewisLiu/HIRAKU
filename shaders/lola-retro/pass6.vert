// pass 6: ../shaders/crt.slang, converted by tools/slang2glsl.py
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
flat out vec3 HK_VARY1;
flat out vec4 HK_VARY2;
flat out float HK_VARY3;
flat out float HK_VARY4;

vec3 kelvinRGB(float T)
{
    float t = T / 100.0;
    float _792;
    if (t <= 66.0)
    {
        _792 = 255.0;
    }
    else
    {
        _792 = 329.69873046875 * pow(t - 60.0, -0.133204758167266845703125);
    }
    float r = _792;
    float _808;
    if (t <= 66.0)
    {
        _808 = (99.47080230712890625 * log(t)) - 161.11956787109375;
    }
    else
    {
        _808 = 288.122161865234375 * pow(t - 60.0, -0.075514845550060272216796875);
    }
    float g = _808;
    float _828;
    if (t >= 66.0)
    {
        _828 = 255.0;
    }
    else
    {
        float _835;
        if (t <= 19.0)
        {
            _835 = 0.0;
        }
        else
        {
            _835 = (138.517730712890625 * log(t - 10.0)) - 305.0447998046875;
        }
        _828 = _835;
    }
    float b = _828;
    return clamp(vec3(r, g, b), vec3(1.0), vec3(255.0)) / vec3(255.0);
}

float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool _313 = id < 5.5;
    bool _322;
    if (!_313)
    {
        _322 = (id > 7.5) && (id < 8.5);
    }
    else
    {
        _322 = _313;
    }
    bool tv = _322;
    float base = ((id > 4.5) && (id < 5.5)) ? 480.0 : 240.0;
    bool _335;
    if (id < 0.5)
    {
        _335 = orig.y > 576.0;
    }
    else
    {
        bool _351;
        if (tv)
        {
            _351 = orig.y > (base * 1.5);
        }
        else
        {
            _351 = tv;
        }
        _335 = _351;
    }
    bool scaled = _335;
    if (!scaled)
    {
        return 1.0;
    }
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        bool _374 = lines == floor(lines);
        bool _382;
        if (_374)
        {
            _382 = lines >= (base * 0.829999983310699462890625);
        }
        else
        {
            _382 = _374;
        }
        bool _390;
        if (_382)
        {
            _390 = lines <= (base * 1.21000003814697265625);
        }
        else
        {
            _390 = _382;
        }
        if (_390)
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
    bool _86 = !mon15;
    bool _113;
    if (_86)
    {
        bool _95 = (w == 320.0) && (h == 200.0);
        bool _112;
        if (!_95)
        {
            bool _101 = w == 640.0;
            bool _111;
            if (_101)
            {
                _111 = (h == 350.0) || (h == 400.0);
            }
            else
            {
                _111 = _101;
            }
            _112 = _111;
        }
        else
        {
            _112 = _95;
        }
        _113 = _112;
    }
    else
    {
        _113 = _86;
    }
    if (_113)
    {
        return 7.0;
    }
    bool _124 = (h == 256.0) || (h == 512.0);
    bool _133;
    if (!_124)
    {
        _133 = (w == 640.0) && (h == 200.0);
    }
    else
    {
        _133 = _124;
    }
    if (_133)
    {
        return 6.0;
    }
    bool _146;
    if (mon15)
    {
        _146 = (h == 200.0) || (h == 400.0);
    }
    else
    {
        _146 = mon15;
    }
    bool _154;
    if (_146)
    {
        _154 = (w == 320.0) || (w == 640.0);
    }
    else
    {
        _154 = _146;
    }
    if (_154)
    {
        return 6.0;
    }
    bool _168 = ((w == 360.0) || (w == 720.0)) || (w == 1440.0);
    bool _186;
    if (_168)
    {
        _186 = (((h == 287.0) || (h == 574.0)) || (h == 242.0)) || (h == 484.0);
    }
    else
    {
        _186 = _168;
    }
    if (_186)
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
    float _422;
    if (global.LR_CONSOLE < 0.5)
    {
        vec2 param_1 = nat;
        _422 = autoConsole(param_1);
    }
    else
    {
        _422 = global.LR_CONSOLE;
    }
    float id = _422;
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

float phosphorSet(Timing t)
{
    if (global.LR_PHOS > 0.5)
    {
        return global.LR_PHOS;
    }
    bool _715 = t.scan > 0.5;
    bool _721;
    if (_715)
    {
        _721 = t.scan < 1.5;
    }
    else
    {
        _721 = _715;
    }
    if (_721)
    {
        return 1.0;
    }
    if (t.id > 7.5)
    {
        return 4.0;
    }
    return (t.pal > 0.5) ? 3.0 : 2.0;
}

float activeShare(Timing t, vec2 orig)
{
    bool _678 = t.scan < 0.5;
    bool _684;
    if (_678)
    {
        _684 = global.LR_MON_MM > 0.5;
    }
    else
    {
        _684 = _678;
    }
    if (!_684)
    {
        return 1.0;
    }
    return min((orig.x / t.pclk) / ((t.pal > 0.5) ? 52.0 : 52.59999847412109375), 1.0);
}

mat3 phosphorMatrix(float p)
{
    if (p < 2.5)
    {
        return mat3(vec3(0.939541995525360107421875, 0.0177720002830028533935546875, -0.00162200001068413257598876953125), vec3(0.0501810014247894287109375, 0.96579301357269287109375, -0.0043700002133846282958984375), vec3(0.010277000255882740020751953125, 0.01643499918282032012939453125, 1.0059909820556640625));
    }
    if (p < 3.5)
    {
        return mat3(vec3(1.04404294490814208984375, 0.0, 0.0), vec3(-0.0440430007874965667724609375, 1.0, 0.011792999692261219024658203125), vec3(0.0, 0.0, 0.98820698261260986328125));
    }
    return mat3(vec3(1.08194196224212646484375, 0.0380319990217685699462890625, -0.0074809999205172061920166015625), vec3(-0.088426999747753143310546875, 0.959990978240966796875, 0.015169999562203884124755859375), vec3(0.0064849997870624065399169921875, 0.0019769999198615550994873046875, 0.992311000823974609375));
}

void main()
{
    gl_Position = global.MVP * Position;
    HK_VARY0 = TexCoord;
    float param = global.LR_WHITE;
    float param_1 = 6500.0;
    vec3 c = pow(kelvinRGB(param) / kelvinRGB(param_1), vec3(2.2000000476837158203125));
    HK_VARY1 = c / vec3(dot(c, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)));
    vec2 param_2 = params.OriginalSize.xy;
    uint param_3 = params.FrameCount;
    Timing T = getTiming(param_2, param_3);
    HK_VARY2 = vec4(T.scan, T.pal, T.interlace, T.scale);
    Timing param_4 = T;
    HK_VARY3 = phosphorSet(param_4);
    Timing param_5 = T;
    vec2 param_6 = params.OriginalSize.xy;
    HK_VARY4 = activeShare(param_5, param_6);
    if (HK_VARY3 > 1.5)
    {
        float param_7 = HK_VARY3;
        HK_VARY1 = inverse(phosphorMatrix(param_7)) * HK_VARY1;
    }
}

