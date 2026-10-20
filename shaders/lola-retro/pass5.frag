// pass 5: ../shaders/beam.slang, converted by tools/slang2glsl.py
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

uniform sampler2D LR_Decoded;

in vec2 HK_VARY0;
flat in float HK_VARY1;
layout(location = 0) out vec4 FragColor;
flat in float HK_VARY2;
flat in vec2 HK_VARY3;
flat in float HK_VARY4;

float hash13(inout vec3 p3)
{
    p3 = fract(p3 * 0.103100001811981201171875);
    p3 += vec3(dot(p3, p3.zyx + vec3(31.31999969482421875)));
    return fract((p3.x + p3.y) * p3.z);
}

bool isPC(float scan)
{
    return (scan > 0.5) && (scan < 1.5);
}

bool inMM(float scan)
{
    float param = scan;
    bool _82 = isPC(param);
    bool _102;
    if (!_82)
    {
        bool _87 = scan < 0.5;
        bool _101;
        if (_87)
        {
            _101 = global.LR_MON_MM > 0.5;
        }
        else
        {
            _101 = _87;
        }
        _102 = _101;
    }
    else
    {
        _102 = _82;
    }
    return _102;
}

vec2 monShown()
{
    return vec2(1.0 - global.LR_OVERSCAN, (1.0 - global.LR_OVERSCAN) - global.LR_OVERSCAN_V);
}

float monCornerGrowth(vec2 c)
{
    return 1.0 + (((global.LR_MON_CORNER - 1.0) * 0.5) * dot(c, c));
}

vec2 monTubeMM()
{
    float d = (global.LR_MON_SIZE - 1.0) * 25.3999996185302734375;
    return vec2(0.800000011920928955078125 * d, 0.60000002384185791015625 * d);
}

vec2 monScanMM()
{
    return monTubeMM() / monShown();
}

vec3 gaussCdf(vec3 z)
{
    vec3 x = abs(z) * 0.707106769084930419921875;
    vec3 t = vec3(1.0) / (vec3(1.0) + (x * 0.3275910913944244384765625));
    vec3 p = ((((((((t * 1.0614054203033447265625) - vec3(1.45315206050872802734375)) * t) + vec3(1.42141377925872802734375)) * t) - vec3(0.284496724605560302734375)) * t) + vec3(0.254829585552215576171875)) * t;
    return vec3(0.5) + ((sign(z) * 0.5) * (vec3(1.0) - (p * exp((-x) * x))));
}

vec3 gaussPdf(vec3 z)
{
    return exp((z * (-0.5)) * z) * 0.398942291736602783203125;
}

void main()
{
    float i = floor(HK_VARY0.y * params.OutputSize.y);
    if (i >= (params.OriginalSize.y / HK_VARY1))
    {
        FragColor = vec4(0.0);
        return;
    }
    float x = HK_VARY0.x - (0.5 * params.OutputSize.z);
    float _259;
    if (global.LR_CABLE < 0.5)
    {
        _259 = 0.5 + (global.LR_NOISE * 4.0);
    }
    else
    {
        _259 = 0.0;
    }
    float jitter = _259;
    float seed = float(params.FrameCount % 997u);
    float _281;
    if (jitter > 0.0)
    {
        vec3 param = vec3(i, seed, 5.0);
        float _289 = hash13(param);
        _281 = ((_289 - 0.5) * jitter) * params.OriginalSize.z;
    }
    else
    {
        _281 = 0.0;
    }
    float jit = _281;
    vec2 tc = vec2(x + jit, (i + 0.5) * params.OriginalSize.w);
    float param_1 = HK_VARY2;
    bool pc = isPC(param_1);
    float param_2 = HK_VARY2;
    bool mm = inMM(param_2);
    float cx = (((x - 0.5) * 2.0) * HK_VARY3.x) / monShown().x;
    float cy = ((((((i + 0.5) * HK_VARY1) * params.OriginalSize.w) - 0.5) * 2.0) * HK_VARY3.y) / monShown().y;
    vec2 param_3 = vec2(cx, cy);
    float mmUnit = (0.4246608912944793701171875 * monCornerGrowth(param_3)) / (monScanMM().x * HK_VARY3.x);
    vec3 acc = vec3(0.0);
    vec3 dacc = vec3(0.0);
    vec3 inv = vec3(0.0);
    float spot = 1.0;
    float sLo;
    float sHi;
    if (pc)
    {
        float W = params.OriginalSize.x;
        sLo = (global.LR_MON_SPOT_LO * mmUnit) * W;
        sHi = (max(global.LR_MON_SPOT_LO, global.LR_MON_SPOT_HI) * mmUnit) * W;
        float px = tc.x * W;
        int j0 = int(floor(px - (3.0 * sHi)));
        int j1 = int(floor(px + (3.0 * sHi)));
        for (int k = 0; k < 24; k++)
        {
            int j = j0 + k;
            if (j > j1)
            {
                break;
            }
            float fj = float(j);
            vec3 b = pow(clamp(textureLod(LR_Decoded, vec2((fj + 0.5) / W, tc.y), 0.0).xyz, vec3(0.0), vec3(1.0)), vec3(0.5 * global.LR_GAMMA));
            vec3 sg = mix(vec3(sLo), vec3(sHi), b);
            vec3 zl = vec3(fj - px) / sg;
            vec3 zr = vec3((fj + 1.0) - px) / sg;
            vec3 param_4 = zr;
            vec3 param_5 = zl;
            vec3 l = (b * b) * (gaussCdf(param_4) - gaussCdf(param_5));
            acc += l;
            inv += (l / sg);
            vec3 param_6 = zl;
            vec3 param_7 = zr;
            dacc += (((b * b) * (gaussPdf(param_6) - gaussPdf(param_7))) / sg);
        }
        dacc *= W;
    }
    else
    {
        float f = (2.0 * max(global.LR_HFOCUS, 0.0500000007450580596923828125)) / 320.0;
        float _531;
        if (mm)
        {
            _531 = global.LR_MON_SPOT_LO * mmUnit;
        }
        else
        {
            _531 = global.LR_SIG_LO * f;
        }
        sLo = _531;
        float _546;
        if (mm)
        {
            _546 = max(global.LR_MON_SPOT_LO, global.LR_MON_SPOT_HI) * mmUnit;
        }
        else
        {
            _546 = max(global.LR_SIG_LO, global.LR_SIG_HI) * f;
        }
        sHi = _546;
        float dt = sLo;
        int nt = int(min(ceil((3.0 * sHi) / dt), 16.0));
        int _579 = -nt;
        for (int k_1 = _579; k_1 <= nt; k_1++)
        {
            float o = float(k_1) * dt;
            vec3 b_1 = pow(clamp(textureLod(LR_Decoded, tc + vec2(o, 0.0), 0.0).xyz, vec3(0.0), vec3(1.0)), vec3(0.5 * global.LR_GAMMA));
            vec3 is = vec3(1.0) / mix(vec3(sLo), vec3(sHi), b_1);
            vec3 g = (exp((is * (((-0.5) * o) * o)) * is) * is) * (0.398942291736602783203125 * dt);
            acc += ((b_1 * b_1) * g);
            inv += (((b_1 * b_1) * g) * is);
            dacc += (((((b_1 * b_1) * g) * o) * is) * is);
        }
    }
    if (mm)
    {
        float lsum = (acc.x + acc.y) + acc.z;
        float isum = (inv.x + inv.y) + inv.z;
        float _692;
        if ((isum > 9.999999717180685365747194737196e-10) && (sHi > sLo))
        {
            _692 = clamp(((lsum / isum) - sLo) / (sHi - sLo), 0.0, 1.0);
        }
        else
        {
            _692 = 0.0;
        }
        spot = _692;
    }
    float conv = clamp(((global.LR_CONV * (0.3499999940395355224609375 + ((0.64999997615814208984375 * cx) * abs(cx)))) * HK_VARY4) / 320.0, -1.0, 1.0);
    float sh = conv * params.OriginalSize.z;
    acc.x -= (dacc.x * sh);
    acc.z += (dacc.z * sh);
    FragColor = vec4(sqrt(max(acc, vec3(0.0))), spot);
}

