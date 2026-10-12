// pass 3: shaders/guest/fast/linearize.slang, converted by tools/slang2glsl.py
#version 330

layout(std140) uniform UBO
{
    vec4 SourceSize;
    mat4 MVP;
} global;

struct Push
{
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float GAMMA_INPUT;
    float inter;
    float interm;
    float iscan;
    float intres;
    float iscans;
    float downsample_levelx;
    float downsample_levely;
    float gamma_out;
    float vga_mode;
    float hiscan;
};

uniform Push params;

uniform sampler2D PrePass;

in vec2 HK_VARY0;
layout(location = 0) out vec4 FragColor;

vec3 fetch_pixel(vec2 coord)
{
    vec2 dx = vec2(global.SourceSize.z, 0.0) * params.downsample_levelx;
    vec2 dy = vec2(0.0, global.SourceSize.w) * params.downsample_levely;
    vec2 d1 = dx + dy;
    vec2 d2 = dx - dy;
    float sum = 15.0;
    vec3 result = ((((((((texture(PrePass, coord).xyz * 3.0) + (texture(PrePass, coord + dx).xyz * 2.0)) + (texture(PrePass, coord - dx).xyz * 2.0)) + (texture(PrePass, coord + dy).xyz * 2.0)) + (texture(PrePass, coord - dy).xyz * 2.0)) + texture(PrePass, coord + d1).xyz) + texture(PrePass, coord - d1).xyz) + texture(PrePass, coord + d2).xyz) + texture(PrePass, coord - d2).xyz;
    return result / vec3(sum);
}

vec3 plant(vec3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06;
    return (tar * r) / vec3(t);
}

void main()
{
    vec3 c1 = texture(PrePass, HK_VARY0).xyz;
    vec3 c2 = texture(PrePass, HK_VARY0 + vec2(0.0, params.OriginalSize.w)).xyz;
    if ((params.downsample_levelx + params.downsample_levely) > 0.02500000037252902984619140625)
    {
        vec2 param = HK_VARY0;
        c1 = fetch_pixel(param);
        vec2 param_1 = HK_VARY0 + vec2(0.0, params.OriginalSize.w);
        c2 = fetch_pixel(param_1);
    }
    vec3 c = c1;
    float intera = 1.0;
    float gamma_in = params.GAMMA_INPUT;
    float m1 = max(max(c1.x, c1.y), c1.z);
    float m2 = max(max(c2.x, c2.y), c2.z);
    vec3 df = abs(c1 - c2);
    float d = max(max(df.x, df.y), df.z);
    if (params.interm == 2.0)
    {
        d = mix(0.100000001490116119384765625 * d, 10.0 * d, step(m1 / (m2 + 9.9999997473787516355514526367188e-05), m2 / (m1 + 9.9999997473787516355514526367188e-05)));
    }
    float r = m1;
    float yres_div = 1.0;
    if (params.intres > 1.25)
    {
        yres_div = params.intres;
    }
    bool hscan = params.hiscan > 0.5;
    bool _287 = params.inter <= (params.OriginalSize.y / yres_div);
    bool _293;
    if (_287)
    {
        _293 = params.interm > 0.5;
    }
    else
    {
        _293 = _287;
    }
    bool _299;
    if (_293)
    {
        _299 = params.intres != 1.0;
    }
    else
    {
        _299 = _293;
    }
    bool _305;
    if (_299)
    {
        _305 = params.intres != 0.5;
    }
    else
    {
        _305 = _299;
    }
    bool _312;
    if (_305)
    {
        _312 = params.vga_mode < 0.5;
    }
    else
    {
        _312 = _305;
    }
    if (_312)
    {
        intera = 0.25;
        float line_no = floor(mod(params.OriginalSize.y * HK_VARY0.y, 2.0));
        float frame_no = floor(mod(float(params.FrameCount), 2.0));
        float ii = abs(line_no - frame_no);
        bool _341 = params.interm < 3.5;
        bool _349;
        if (!_341)
        {
            _349 = params.interm > 5.5;
        }
        else
        {
            _349 = _341;
        }
        if (_349)
        {
            if (params.interm == 6.0)
            {
                c = mix(c2, c1, vec3(ii));
            }
            else
            {
                vec3 param_2 = mix(c2, c2 * c2, vec3(params.iscans));
                float param_3 = max(max(c2.x, c2.y), c2.z);
                c2 = plant(param_2, param_3);
                r = max(m1 * ii, (1.0 - params.iscan) * min(m1, m2));
                vec3 param_4 = mix(mix(c1, c2, vec3(min(mix(m1, 1.0 - m2, min(m1, 1.0 - m1)) / (d + 9.9999997473787516355514526367188e-06), 1.0))), c1, vec3(ii));
                float param_5 = r;
                c = plant(param_4, param_5);
                if (params.interm == 3.0)
                {
                    c = mix(c2, c1, vec3(ii)) * (1.0 - (0.5 * params.iscan));
                }
            }
        }
        if (params.interm == 4.0)
        {
            vec3 param_6 = mix(c, c * c, vec3(0.5 * params.iscans));
            float param_7 = max(max(c.x, c.y), c.z);
            c = plant(param_6, param_7) * (1.0 - (0.5 * params.iscan));
        }
        if (params.interm == 5.0)
        {
            c = mix(c2, c1, vec3(0.5));
            vec3 param_8 = mix(c, c * c, vec3(0.5 * params.iscans));
            float param_9 = max(max(c.x, c.y), c.z);
            c = plant(param_8, param_9) * (1.0 - (0.5 * params.iscan));
        }
        if (hscan)
        {
            c = c1;
        }
    }
    if (params.vga_mode > 0.5)
    {
        c = c1;
        if (params.inter <= params.OriginalSize.y)
        {
            intera = 0.75;
        }
        else
        {
            intera = 0.5;
        }
    }
    c = pow(c, vec3(gamma_in));
    if (HK_VARY0.x > 0.5)
    {
        gamma_in = intera;
    }
    else
    {
        gamma_in = 1.0 / gamma_in;
    }
    FragColor = vec4(c, gamma_in);
}

