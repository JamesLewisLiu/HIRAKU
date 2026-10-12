// SPDX-License-Identifier: GPL-3.0-or-later
#include "crt.h"

#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES 1   // declarations only (for decltype): everything is loaded through SDL
#include <SDL3/SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace hk::host::crt {

namespace {

#define HK_GL_FUNCS(X)                                                                                         \
    X(glGetString) X(glGetError) X(glViewport) X(glClearColor) X(glClear) X(glDisable) X(glGenTextures)        \
    X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D) X(glTexParameteri)                \
    X(glPixelStorei) X(glActiveTexture) X(glGenFramebuffers) X(glDeleteFramebuffers) X(glBindFramebuffer)     \
    X(glFramebufferTexture2D) X(glCheckFramebufferStatus) X(glCreateShader) X(glShaderSource)                 \
    X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog) X(glDeleteShader) X(glCreateProgram)            \
    X(glAttachShader) X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glDeleteProgram)            \
    X(glUseProgram) X(glGetUniformLocation) X(glUniform1i) X(glUniform1f) X(glUniform1ui) X(glUniform4f)      \
    X(glGenVertexArrays) X(glBindVertexArray) X(glDeleteVertexArrays) X(glDrawArrays) X(glReadPixels)

struct Gl {
#define X(f) decltype(&::f) f = nullptr;
    HK_GL_FUNCS(X)
#undef X
} gl;

bool load_gl() {
#define X(f)                                                         \
    gl.f = reinterpret_cast<decltype(gl.f)>(SDL_GL_GetProcAddress(#f)); \
    if (!gl.f) { fprintf(stderr, "crt: OpenGL has no %s\n", #f); return false; }
    HK_GL_FUNCS(X)
#undef X
    return true;
}

// Texture units: each pass output (and the frame) stays bound to its own.
enum Unit { U_FRAME, U_PRE, U_LIN, U_PASS1, U_BLOOMH, U_BLOOM, U_PASS2, U_LUT1, NUNITS = U_LUT1 + 4 };

const char *const VERTEX = R"(#version 330 core
uniform float uFlip;       // 1: drawing to the window (top row first)
uniform float uTexScale;   // some passes stretch their coordinates by 1.00001, as in the preset
out vec2 vTexCoord;
void main() {
    vec2 tc = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vTexCoord = tc * uTexScale;
    gl_Position = vec4(2.0 * tc.x - 1.0, uFlip > 0.5 ? 1.0 - 2.0 * tc.y : 2.0 * tc.y - 1.0, 0.0, 1.0);
}
)";

// Put in front of every pass (the slang push-constant and UBO semantics, as plain uniforms).
const char *const PRELUDE = R"(#version 330 core
#define COMPAT_TEXTURE(c, d) texture(c, d)
in vec2 vTexCoord;
out vec4 FragColor;
uniform vec4 SourceSize;
uniform vec4 OriginalSize;
uniform vec4 OutputSize;
uniform uint FrameCount;
vec3 plant(vec3 tar, float r) { float t = max(max(tar.r, tar.g), tar.b) + 0.00001; return tar * r / t; }
#line 1
)";

// Sharp pixels (F10): the frame, nearest texel.
const char *const PLAIN = R"(
uniform sampler2D Source;
void main() {
    ivec2 sz = textureSize(Source, 0);
    FragColor = vec4(texelFetch(Source, clamp(ivec2(vTexCoord * vec2(sz)), ivec2(0), sz - 1), 0).rgb, 1.0);
}
)";

struct Target {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
};

struct Pass {
    const char *file;
    float tex_scale;
    Unit out;          // its output (NUNITS: the window)
    Unit source;       // "Source"
    bool fp16;
    GLuint prog = 0;
};

Pass g_passes[] = {
    {"pre-shaders.glsl", 1.0f, U_PRE, U_FRAME, false},
    {"linearize.glsl", 1.00001f, U_LIN, U_PRE, true},
    {"pass1.glsl", 1.00001f, U_PASS1, U_LIN, true},
    {"bloom_horizontal.glsl", 1.0f, U_BLOOMH, U_PASS1, true},
    {"bloom_vertical.glsl", 1.0f, U_BLOOM, U_BLOOMH, true},
    {"pass2.glsl", 1.00001f, U_PASS2, U_BLOOM, true},
    {"deconvergence.glsl", 1.00001f, NUNITS, U_PASS2, false},
};

struct Param {
    float def = 0, value = 0;
};
std::map<std::string, Param> g_params;   // from the shaders' #pragma parameter lines
std::string g_dir, g_params_file;
GLuint g_plain = 0, g_vao = 0;
GLuint g_frame = 0, g_lut[4] = {};
int g_fw = 0, g_fh = 0;
Target g_t[NUNITS];
uint32_t g_frame_count = 0;
bool g_ok = false;
// HK_CRT_SNAP=FRAME,FILE.ppm: write the window's picture at that frame (checking the shader)
uint32_t g_snap_frame = UINT32_MAX;
std::string g_snap_path;

void snap(int win_w, int win_h) {
    std::vector<uint8_t> px((size_t)win_w * win_h * 3);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glReadPixels(0, 0, win_w, win_h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(g_snap_path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", win_w, win_h);
    for (int y = win_h - 1; y >= 0; y--) fwrite(&px[(size_t)y * win_w * 3], 1, (size_t)win_w * 3, f);
    fclose(f);
    fprintf(stderr, "crt: frame %u written to %s\n", g_frame_count, g_snap_path.c_str());
}

std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// "#pragma parameter NAME "label" default min max step": registers NAME, and the line is
// blanked (kept, so the compiler's line numbers match the file).
std::string take_parameters(const std::string &src) {
    std::istringstream in(src);
    std::string line, out;
    while (std::getline(in, line)) {
        if (line.rfind("#pragma parameter", 0) == 0) {
            std::istringstream ls(line.substr(17));
            std::string name;
            ls >> name;
            auto q = line.find('"', 17), q2 = q == std::string::npos ? q : line.find('"', q + 1);
            float def = 0;
            if (q2 != std::string::npos) def = strtof(line.c_str() + q2 + 1, nullptr);
            if (!g_params.count(name)) g_params[name] = {def, def};
            line.clear();
        }
        out += line;
        out += '\n';
    }
    return out;
}

GLuint compile(GLenum type, const std::string &src, const std::string &what) {
    GLuint s = gl.glCreateShader(type);
    const char *p = src.c_str();
    gl.glShaderSource(s, 1, &p, nullptr);
    gl.glCompileShader(s);
    GLint ok = 0;
    gl.glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl.glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "crt: %s does not compile:\n%s\n", what.c_str(), log);
        gl.glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint program(const std::string &frag_src, const std::string &what) {
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX, what + " (vertex)");
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, std::string(PRELUDE) + frag_src, what) : 0;
    if (!fs) {
        if (vs) gl.glDeleteShader(vs);
        return 0;
    }
    GLuint p = gl.glCreateProgram();
    gl.glAttachShader(p, vs);
    gl.glAttachShader(p, fs);
    gl.glLinkProgram(p);
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);
    GLint ok = 0;
    gl.glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl.glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fprintf(stderr, "crt: %s does not link:\n%s\n", what.c_str(), log);
        gl.glDeleteProgram(p);
        return 0;
    }
    return p;
}

void set_sampler(GLuint p, const char *name, int unit) {
    GLint l = gl.glGetUniformLocation(p, name);
    if (l >= 0) gl.glUniform1i(l, unit);
}

void set_parameters() {
    for (auto &pass : g_passes) {
        gl.glUseProgram(pass.prog);
        for (auto &[name, prm] : g_params) {
            GLint l = gl.glGetUniformLocation(pass.prog, name.c_str());
            if (l >= 0) gl.glUniform1f(l, prm.value);
        }
    }
}

void read_parameters() {
    for (auto &[name, prm] : g_params) prm.value = prm.def;
    if (g_params_file.empty()) return;
    std::ifstream f(g_params_file);
    if (!f) {
        fprintf(stderr, "crt: cannot read %s: the shader's defaults\n", g_params_file.c_str());
        return;
    }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            const char *ws = " \t\r\"";
            s.erase(0, s.find_first_not_of(ws));
            s.erase(s.find_last_not_of(ws) + 1);
            return s;
        };
        std::string name = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        auto it = g_params.find(name);
        if (it == g_params.end()) continue;   // a preset's other lines (shader0 = ...)
        char *end = nullptr;
        float x = strtof(v.c_str(), &end);
        if (end == v.c_str()) {
            fprintf(stderr, "crt: %s: %s is not a number\n", g_params_file.c_str(), v.c_str());
            continue;
        }
        it->second.value = x;
        n++;
    }
    fprintf(stderr, "crt: %d parameters from %s\n", n, g_params_file.c_str());
}

void texture_params(GLint filter) {
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);   // border: transparent black
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

// The colour LUTs (TNTC 1-4): 1024 x 32 PNGs, read only when a pass needs them.
void load_luts() {
    static const char *names[4] = {"trinitron-lut.png", "inv-trinitron-lut.png", "nec-lut.png", "ntsc-lut.png"};
    for (int k = 0; k < 4; k++) {
        if (g_lut[k]) continue;
        std::string path = g_dir + "/lut/" + names[k];
        SDL_Surface *s = SDL_LoadPNG(path.c_str());
        SDL_Surface *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888) : nullptr;   // R, G, B, A bytes
        if (s) SDL_DestroySurface(s);
        if (!c) {
            fprintf(stderr, "crt: cannot read %s (%s): no LUT colours\n", path.c_str(), SDL_GetError());
            continue;
        }
        gl.glGenTextures(1, &g_lut[k]);
        gl.glActiveTexture(GL_TEXTURE0 + U_LUT1 + k);
        gl.glBindTexture(GL_TEXTURE_2D, g_lut[k]);
        texture_params(GL_LINEAR);
        gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, c->pitch / 4);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c->w, c->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, c->pixels);
        gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        SDL_DestroySurface(c);
    }
}

void ensure_target(Unit u, int w, int h, bool fp16) {
    Target &t = g_t[u];
    if (t.tex && t.w == w && t.h == h) return;
    if (!t.tex) {
        gl.glGenTextures(1, &t.tex);
        gl.glGenFramebuffers(1, &t.fbo);
    }
    gl.glActiveTexture(GL_TEXTURE0 + u);
    gl.glBindTexture(GL_TEXTURE_2D, t.tex);
    texture_params(GL_LINEAR);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, fp16 ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, fp16 ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE,
                    nullptr);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "crt: a %dx%d %s target is not usable here\n", w, h, fp16 ? "fp16" : "8-bit");
    t.w = w;
    t.h = h;
}

void size_uniform(GLuint p, const char *name, int w, int h) {
    GLint l = gl.glGetUniformLocation(p, name);
    if (l >= 0) gl.glUniform4f(l, (float)w, (float)h, 1.0f / w, 1.0f / h);
}

void draw_quad(GLuint p, float flip, float tex_scale) {
    gl.glUniform1f(gl.glGetUniformLocation(p, "uFlip"), flip);
    gl.glUniform1f(gl.glGetUniformLocation(p, "uTexScale"), tex_scale);
    gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

}  // namespace

bool init(const std::string &dir, const std::string &params) {
    if (!load_gl()) return false;
    g_dir = dir;
    g_params_file = params;
    for (auto &pass : g_passes) {
        std::string path = dir + "/" + pass.file;
        std::string src = read_file(path);
        if (src.empty()) {
            fprintf(stderr, "crt: cannot read %s\n", path.c_str());
            return false;
        }
        pass.prog = program(take_parameters(src), pass.file);
        if (!pass.prog) return false;
        gl.glUseProgram(pass.prog);
        set_sampler(pass.prog, "Source", pass.source);
        set_sampler(pass.prog, "StockPass", U_FRAME);
        set_sampler(pass.prog, "PrePass", U_PRE);
        set_sampler(pass.prog, "LinearizePass", U_LIN);
        set_sampler(pass.prog, "Pass1", U_PASS1);
        set_sampler(pass.prog, "BloomPass", U_BLOOM);
        for (int k = 0; k < 4; k++) set_sampler(pass.prog, ("SamplerLUT" + std::to_string(k + 1)).c_str(), U_LUT1 + k);
    }
    g_plain = program(PLAIN, "sharp pixels");
    if (!g_plain) return false;
    gl.glUseProgram(g_plain);
    set_sampler(g_plain, "Source", U_FRAME);
    gl.glGenVertexArrays(1, &g_vao);
    gl.glBindVertexArray(g_vao);
    gl.glDisable(GL_FRAMEBUFFER_SRGB);
    gl.glDisable(GL_BLEND);
    read_parameters();
    if (g_params["TNTC"].value >= 0.5f) load_luts();
    set_parameters();
    fprintf(stderr, "crt: crt-guest-advanced-fast, OpenGL %s on %s\n", (const char *)gl.glGetString(GL_VERSION),
            (const char *)gl.glGetString(GL_RENDERER));
    if (const char *e = getenv("HK_CRT_SNAP")) {
        g_snap_frame = (uint32_t)strtoul(e, nullptr, 10);
        if (const char *c = strchr(e, ',')) g_snap_path = c + 1;
    }
    g_ok = true;
    return true;
}

void reload_params() {
    if (!g_ok) return;
    read_parameters();
    if (g_params["TNTC"].value >= 0.5f) load_luts();
    set_parameters();
}

void draw(const uint32_t *rgb, int w, int h, int x, int y, int vw, int vh, int win_w, int win_h, bool crt) {
    if (!g_ok || w <= 0 || h <= 0 || vw <= 0 || vh <= 0) return;
    gl.glActiveTexture(GL_TEXTURE0 + U_FRAME);
    if (!g_frame || w != g_fw || h != g_fh) {
        if (!g_frame) gl.glGenTextures(1, &g_frame);
        gl.glBindTexture(GL_TEXTURE_2D, g_frame);
        texture_params(GL_LINEAR);
        GLint one = GL_ONE;
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, one);   // XRGB: the X byte is not alpha
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
        g_fw = w;
        g_fh = h;
    }
    gl.glBindTexture(GL_TEXTURE_2D, g_frame);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, rgb);

    if (crt) {
        // the preset's sizes (header comment)
        const int size[NUNITS][2] = {
            {w, h}, {w, h}, {w, h}, {vw, h}, {640, h}, {640, 480}, {vw, vh},
        };
        for (const auto &pass : g_passes) {
            if (pass.out == NUNITS) break;
            ensure_target(pass.out, size[pass.out][0], size[pass.out][1], pass.fp16);
        }
        for (const auto &pass : g_passes) {
            int ow = pass.out == NUNITS ? vw : size[pass.out][0], oh = pass.out == NUNITS ? vh : size[pass.out][1];
            if (pass.out == NUNITS) {
                gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
                gl.glViewport(0, 0, win_w, win_h);
                gl.glClearColor(0, 0, 0, 1);
                gl.glClear(GL_COLOR_BUFFER_BIT);
                gl.glViewport(x, win_h - y - vh, vw, vh);
            } else {
                gl.glBindFramebuffer(GL_FRAMEBUFFER, g_t[pass.out].fbo);
                gl.glViewport(0, 0, ow, oh);
            }
            gl.glUseProgram(pass.prog);
            size_uniform(pass.prog, "SourceSize", size[pass.source][0], size[pass.source][1]);
            size_uniform(pass.prog, "OriginalSize", w, h);
            size_uniform(pass.prog, "OutputSize", ow, oh);
            GLint l = gl.glGetUniformLocation(pass.prog, "FrameCount");
            if (l >= 0) gl.glUniform1ui(l, g_frame_count);
            // bind every output to its unit (the targets may have been reallocated)
            for (int u = U_PRE; u < U_LUT1; u++) {
                gl.glActiveTexture(GL_TEXTURE0 + u);
                gl.glBindTexture(GL_TEXTURE_2D, g_t[u].tex);
            }
            draw_quad(pass.prog, pass.out == NUNITS ? 1.0f : 0.0f, pass.tex_scale);
        }
    } else {
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl.glViewport(0, 0, win_w, win_h);
        gl.glClearColor(0, 0, 0, 1);
        gl.glClear(GL_COLOR_BUFFER_BIT);
        gl.glViewport(x, win_h - y - vh, vw, vh);
        gl.glUseProgram(g_plain);
        draw_quad(g_plain, 1.0f, 1.0f);
    }
    if (g_frame_count == g_snap_frame && !g_snap_path.empty()) snap(win_w, win_h);
    g_frame_count++;
}

void shutdown() {
    if (!g_ok) return;
    for (auto &pass : g_passes)
        if (pass.prog) gl.glDeleteProgram(pass.prog);
    if (g_plain) gl.glDeleteProgram(g_plain);
    for (auto &t : g_t) {
        if (t.tex) gl.glDeleteTextures(1, &t.tex);
        if (t.fbo) gl.glDeleteFramebuffers(1, &t.fbo);
    }
    if (g_frame) gl.glDeleteTextures(1, &g_frame);
    gl.glDeleteTextures(4, g_lut);
    if (g_vao) gl.glDeleteVertexArrays(1, &g_vao);
    g_ok = false;
}

}  // namespace hk::host::crt
