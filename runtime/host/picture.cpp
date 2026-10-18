// SPDX-License-Identifier: GPL-3.0-or-later
#include "picture.h"

#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES 1   // declarations only (for decltype): everything is loaded through SDL
#include <SDL3/SDL_opengl.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <algorithm>
#include <memory>
#include <sstream>
#include <tuple>

namespace hk::host::picture {

namespace {

#define HK_GL_FUNCS(X)                                                                                         \
    X(glGetString) X(glGetError) X(glViewport) X(glClearColor) X(glClear) X(glEnable) X(glDisable)             \
    X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D)                  \
    X(glTexParameteri) X(glGenerateMipmap) X(glPixelStorei) X(glActiveTexture) X(glGenFramebuffers)           \
    X(glDeleteFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D) X(glCheckFramebufferStatus)        \
    X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog)             \
    X(glDeleteShader) X(glCreateProgram) X(glAttachShader) X(glLinkProgram) X(glGetProgramiv)                 \
    X(glGetProgramInfoLog) X(glDeleteProgram) X(glUseProgram) X(glGetUniformLocation) X(glGetActiveUniform)   \
    X(glGetActiveUniformsiv) X(glGetUniformBlockIndex) X(glGetActiveUniformBlockiv) X(glUniformBlockBinding)  \
    X(glUniform1i) X(glUniform1f) X(glUniform1ui) X(glUniform4f) X(glUniformMatrix4fv) X(glGenBuffers)        \
    X(glDeleteBuffers) X(glBindBuffer) X(glBufferData) X(glBufferSubData) X(glBindBufferBase)                 \
    X(glGenVertexArrays) X(glBindVertexArray) X(glDeleteVertexArrays) X(glVertexAttribPointer)                \
    X(glEnableVertexAttribArray) X(glDrawArrays) X(glGenSamplers) X(glDeleteSamplers) X(glBindSampler)        \
    X(glSamplerParameteri) X(glReadPixels) X(glGenQueries) X(glDeleteQueries) X(glBeginQuery)   \
    X(glEndQuery) X(glGetQueryObjectiv) X(glGetQueryObjectui64v) X(glBlendFunc)

struct Gl {
#define X(f) decltype(&::f) f = nullptr;
    HK_GL_FUNCS(X)
#undef X
} gl;

bool load_gl() {
#define X(f)                                                            \
    gl.f = reinterpret_cast<decltype(gl.f)>(SDL_GL_GetProcAddress(#f)); \
    if (!gl.f) { fprintf(stderr, "picture: OpenGL has no %s\n", #f); return false; }
    HK_GL_FUNCS(X)
#undef X
    return true;
}

std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s, const char *ws = " \t\r\"") {
    s.erase(0, s.find_first_not_of(ws));
    s.erase(s.find_last_not_of(ws) + 1);
    return s;
}

// `key = value` lines, # comments (a .slangp, a .params file)
std::map<std::string, std::string> read_keys(const std::string &path, bool *ok = nullptr) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    if (ok) *ok = bool(f);
    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
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
        fprintf(stderr, "picture: %s does not compile:\n%s\n", what.c_str(), log);
        gl.glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const std::string &vs_src, const std::string &fs_src, const std::string &what) {
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src, what + " (vertex)");
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fs_src, what + " (fragment)") : 0;
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
        fprintf(stderr, "picture: %s does not link:\n%s\n", what.c_str(), log);
        gl.glDeleteProgram(p);
        return 0;
    }
    return p;
}

GLenum wrap_mode(const std::string &s) {
    if (s == "clamp_to_edge") return GL_CLAMP_TO_EDGE;
    if (s == "repeat") return GL_REPEAT;
    if (s == "mirrored_repeat") return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_BORDER;   // RetroArch's default (transparent black)
}

// How a texture is sampled; one GL sampler object per kind.
struct Sampling {
    bool linear = true, mip = false;
    GLenum wrap = GL_CLAMP_TO_BORDER;
};

GLuint sampler_for(const Sampling &s) {
    static std::map<std::tuple<bool, bool, GLenum>, GLuint> cache;
    auto key = std::make_tuple(s.linear, s.mip, s.wrap);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    GLuint id = 0;
    gl.glGenSamplers(1, &id);
    gl.glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, s.linear ? GL_LINEAR : GL_NEAREST);
    gl.glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, s.mip ? (s.linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                                                            : (s.linear ? GL_LINEAR : GL_NEAREST));
    gl.glSamplerParameteri(id, GL_TEXTURE_WRAP_S, s.wrap);
    gl.glSamplerParameteri(id, GL_TEXTURE_WRAP_T, s.wrap);
    cache[key] = id;
    return id;
}

// mat4, column-major: the pass's quad (0..1) to clip space; `flip`: top row first (the window)
void mvp(float m[16], bool flip) {
    static const float base[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
    memcpy(m, base, sizeof base);
    if (flip) {
        m[5] = -2;
        m[13] = 1;
    }
}

struct Param {
    std::string name;
    float def = 0, value = 0;
};

// A texture a pass can read: the frame, a pass output, a preset texture.
enum Src { S_ORIGINAL, S_PASS, S_LUT };

struct Bind {
    enum Kind { MVP, SIZE, FRAMECOUNT, FRAMEDIR, PARAM } kind;
    bool in_ubo;
    GLint loc = -1, offset = 0;
    GLenum type = GL_FLOAT;
    Src src = S_ORIGINAL;   // SIZE: of which texture (S_PASS with idx -1: this pass's output,
    int idx = 0;            //       -2: the final viewport); PARAM: index into params
};

struct SamplerUse {
    int unit;
    Src src;
    int idx;
};

struct Target {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    GLenum fmt = GL_RGBA8;
    bool mip = false;
};

struct Pass {
    std::string alias, format;
    bool srgb = false, fp = false, mipmap_input = false;
    bool has_linear = false, linear = true;
    std::string wrap = "clamp_to_border";
    std::string type_x, type_y;   // source, viewport, absolute
    float scale_x = 1, scale_y = 1;
    int frame_count_mod = 0;
    GLuint prog = 0, ubo = 0;
    GLint ubo_size = 0;
    std::vector<uint8_t> ubo_data;
    std::vector<Bind> binds;
    std::vector<SamplerUse> samplers;
    Target out;
    int w = 0, h = 0;   // this frame's size
};

struct Lut {
    std::string name;
    GLuint tex = 0;
    int w = 0, h = 0;
    Sampling sampling;
};

GLuint g_vao = 0, g_vbo = 0, g_plain = 0, g_over = 0, g_over_tex = 0;
// Each window draws its own picture (draw's view: Keyboardmania's two monitors in two windows):
// its frame's texture and size here, its preset chains (their pass sizes, FrameCount) in g_chain
struct Frame {
    GLuint tex = 0;
    int w = 0, h = 0;
};
Frame g_frames[2];
int g_view = 0;
Frame *g_cur = &g_frames[0];
std::string g_shaders_dir;
std::vector<std::string> g_config_dirs;
bool g_ok = false;
uint32_t g_draws = 0;
uint32_t g_snap_frame = UINT32_MAX;
std::string g_snap_path;

class Chain {
public:
    std::string name;
    bool load(const std::string &dir);
    void draw(int w, int h, int x, int y, int vw, int vh, int win_w, int win_h);
    void check_params();   // read the parameter files again if one changed
    ~Chain();

private:
    std::string m_dir;
    std::vector<Pass> m_passes;
    std::vector<Lut> m_luts;
    std::vector<Param> m_params;
    std::vector<std::pair<std::string, time_t>> m_param_files;   // and their mtimes
    uint32_t m_frame = 0;
    int m_vw = 0, m_vh = 0;

    bool resolve(const std::string &name, Src &src, int &idx, int pass) const;
    void read_params();
    Sampling sampling_of(Src src, int idx) const;
    void size_of(Src src, int idx, int pass, int &w, int &h) const;
    bool link_pass(int i, const std::string &vs, const std::string &fs);
};

Chain::~Chain() {
    for (auto &p : m_passes) {
        if (p.prog) gl.glDeleteProgram(p.prog);
        if (p.ubo) gl.glDeleteBuffers(1, &p.ubo);
        if (p.out.tex) gl.glDeleteTextures(1, &p.out.tex);
        if (p.out.fbo) gl.glDeleteFramebuffers(1, &p.out.fbo);
    }
    for (auto &l : m_luts)
        if (l.tex) gl.glDeleteTextures(1, &l.tex);
}

// A sampler or size name: Original, Source, PassOutputN, an alias, a preset texture.
bool Chain::resolve(const std::string &name, Src &src, int &idx, int pass) const {
    if (name == "Original" || name == "OriginalHistory0") { src = S_ORIGINAL; return true; }
    if (name == "Source") {
        if (pass == 0) src = S_ORIGINAL;
        else { src = S_PASS; idx = pass - 1; }
        return true;
    }
    if (name.rfind("PassOutput", 0) == 0 && name.size() > 10 && isdigit((unsigned char)name[10])) {
        src = S_PASS;
        idx = atoi(name.c_str() + 10);
        return idx < pass;
    }
    for (int i = 0; i < pass; i++)
        if (!m_passes[i].alias.empty() && m_passes[i].alias == name) { src = S_PASS; idx = i; return true; }
    for (size_t k = 0; k < m_luts.size(); k++)
        if (m_luts[k].name == name) { src = S_LUT; idx = (int)k; return true; }
    return false;
}

// RetroArch: a pass's output (and the frame) is read with the filter, wrap mode and mipmaps
// of the pass after it.
Sampling Chain::sampling_of(Src src, int idx) const {
    if (src == S_LUT) return m_luts[idx].sampling;
    int next = src == S_ORIGINAL ? 0 : idx + 1;
    Sampling s;
    if (next < (int)m_passes.size()) {
        const Pass &p = m_passes[next];
        s.linear = p.has_linear ? p.linear : true;
        s.mip = p.mipmap_input;
        s.wrap = wrap_mode(p.wrap);
    }
    return s;
}

void Chain::size_of(Src src, int idx, int pass, int &w, int &h) const {
    switch (src) {
    case S_ORIGINAL: w = g_cur->w; h = g_cur->h; break;
    case S_LUT: w = m_luts[idx].w; h = m_luts[idx].h; break;
    case S_PASS:
        if (idx == -2) { w = m_vw; h = m_vh; }
        else { const Pass &p = m_passes[idx < 0 ? pass : idx]; w = p.w; h = p.h; }
        break;
    }
}

bool Chain::link_pass(int i, const std::string &vs, const std::string &fs) {
    Pass &p = m_passes[i];
    p.prog = link(vs, fs, name + " pass " + std::to_string(i));
    if (!p.prog) return false;
    gl.glUseProgram(p.prog);
    GLuint block = gl.glGetUniformBlockIndex(p.prog, "UBO");
    if (block != GL_INVALID_INDEX) {
        gl.glGetActiveUniformBlockiv(p.prog, block, GL_UNIFORM_BLOCK_DATA_SIZE, &p.ubo_size);
        gl.glUniformBlockBinding(p.prog, block, 0);
        p.ubo_data.assign(p.ubo_size, 0);
        gl.glGenBuffers(1, &p.ubo);
        gl.glBindBuffer(GL_UNIFORM_BUFFER, p.ubo);
        gl.glBufferData(GL_UNIFORM_BUFFER, p.ubo_size, nullptr, GL_DYNAMIC_DRAW);
    }
    GLint n = 0;
    gl.glGetProgramiv(p.prog, GL_ACTIVE_UNIFORMS, &n);
    int unit = 0;
    for (GLuint u = 0; u < (GLuint)n; u++) {
        char full[256];
        GLint size = 0, blk = -1, off = 0;
        GLenum type = 0;
        gl.glGetActiveUniform(p.prog, u, sizeof full, nullptr, &size, &type, full);
        gl.glGetActiveUniformsiv(p.prog, 1, &u, GL_UNIFORM_BLOCK_INDEX, &blk);
        gl.glGetActiveUniformsiv(p.prog, 1, &u, GL_UNIFORM_OFFSET, &off);
        std::string member = full;
        if (auto dot = member.rfind('.'); dot != std::string::npos) member = member.substr(dot + 1);
        Bind b{};
        b.in_ubo = blk >= 0;
        b.offset = off;
        b.type = type;
        if (!b.in_ubo) b.loc = gl.glGetUniformLocation(p.prog, full);
        if (type == GL_SAMPLER_2D) {
            Src src;
            int idx = 0;
            if (!resolve(member, src, idx, i)) {
                fprintf(stderr, "picture: %s pass %d reads %s, which HIRAKU doesn't provide: black\n", name.c_str(), i, member.c_str());
                src = S_LUT;
                idx = -1;
            }
            gl.glUniform1i(b.loc, unit);
            p.samplers.push_back({unit++, src, idx});
            continue;
        }
        if (member == "MVP") b.kind = Bind::MVP;
        else if (member == "FrameCount") b.kind = Bind::FRAMECOUNT;
        else if (member == "FrameDirection") b.kind = Bind::FRAMEDIR;
        else if (member.size() > 4 && member.compare(member.size() - 4, 4, "Size") == 0) {
            std::string base = member.substr(0, member.size() - 4);
            b.kind = Bind::SIZE;
            if (base == "Output") { b.src = S_PASS; b.idx = -1; }
            else if (base == "FinalViewport") { b.src = S_PASS; b.idx = -2; }
            else if (!resolve(base, b.src, b.idx, i)) {
                fprintf(stderr, "picture: %s pass %d: unknown size %s\n", name.c_str(), i, member.c_str());
                continue;
            }
        } else {
            b.kind = Bind::PARAM;
            b.idx = -1;
            for (size_t k = 0; k < m_params.size(); k++)
                if (m_params[k].name == member) b.idx = (int)k;
            if (b.idx < 0) continue;   // declared but not a parameter: 0, as in RetroArch
        }
        p.binds.push_back(b);
    }
    return true;
}

void Chain::read_params() {
    for (auto &prm : m_params) prm.value = prm.def;
    std::vector<std::string> files = {m_dir + "/hiraku.params"};
    for (const auto &d : g_config_dirs) files.push_back(d + "/" + name + ".params");
    m_param_files.clear();
    for (const auto &f : files) {
        struct stat st {};
        m_param_files.push_back({f, stat(f.c_str(), &st) == 0 ? st.st_mtime : 0});
        bool ok = false;
        auto keys = read_keys(f, &ok);
        if (!ok) continue;
        for (auto &prm : m_params) {
            auto it = keys.find(prm.name);
            if (it == keys.end()) continue;
            char *end = nullptr;
            float v = strtof(it->second.c_str(), &end);
            if (end != it->second.c_str()) prm.value = v;
        }
    }
}

void Chain::check_params() {
    for (const auto &[f, mtime] : m_param_files) {
        struct stat st {};
        time_t now = stat(f.c_str(), &st) == 0 ? st.st_mtime : 0;
        if (now != mtime) {
            read_params();
            fprintf(stderr, "picture: %s settings read again\n", name.c_str());
            return;
        }
    }
}

bool Chain::load(const std::string &dir) {
    m_dir = dir;
    bool ok = false;
    auto preset = read_keys(dir + "/preset.slangp", &ok);
    if (!ok) {
        fprintf(stderr, "picture: cannot read %s/preset.slangp\n", dir.c_str());
        return false;
    }
    {   // parameters.txt: name default min max step label
        std::ifstream f(dir + "/parameters.txt");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            Param prm;
            ls >> prm.name >> prm.def;
            prm.value = prm.def;
            m_params.push_back(prm);
        }
    }
    auto get = [&](const std::string &k, const std::string &def = "") {
        auto it = preset.find(k);
        return it == preset.end() ? def : it->second;
    };
    // the preset's textures
    std::string textures = get("textures");
    for (size_t s = 0; s < textures.size();) {
        size_t e = textures.find(';', s);
        if (e == std::string::npos) e = textures.size();
        Lut l;
        l.name = textures.substr(s, e - s);
        s = e + 1;
        if (l.name.empty()) continue;
        l.sampling.linear = get(l.name + "_linear") == "true";
        l.sampling.mip = get(l.name + "_mipmap") == "true";
        l.sampling.wrap = wrap_mode(get(l.name + "_wrap_mode"));
        std::string path = dir + "/" + get(l.name);
        SDL_Surface *img = SDL_LoadPNG(path.c_str());
        SDL_Surface *c = img ? SDL_ConvertSurface(img, SDL_PIXELFORMAT_ABGR8888) : nullptr;   // R, G, B, A bytes
        if (img) SDL_DestroySurface(img);
        if (!c) {
            fprintf(stderr, "picture: cannot read %s (%s)\n", path.c_str(), SDL_GetError());
            return false;
        }
        gl.glGenTextures(1, &l.tex);
        gl.glBindTexture(GL_TEXTURE_2D, l.tex);
        gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, c->pitch / 4);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c->w, c->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, c->pixels);
        gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if (l.sampling.mip) gl.glGenerateMipmap(GL_TEXTURE_2D);
        else gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        l.w = c->w;
        l.h = c->h;
        SDL_DestroySurface(c);
        m_luts.push_back(l);
    }
    int n = atoi(get("shaders", "0").c_str());
    if (n <= 0) return false;
    m_passes.resize(n);
    for (int i = 0; i < n; i++) {
        Pass &p = m_passes[i];
        std::string k = std::to_string(i);
        p.alias = get("alias" + k);
        p.format = get("format" + k);
        p.srgb = get("srgb_framebuffer" + k) == "true";
        p.fp = get("float_framebuffer" + k) == "true";
        p.mipmap_input = get("mipmap_input" + k) == "true";
        p.has_linear = preset.count("filter_linear" + k) != 0;
        p.linear = get("filter_linear" + k) == "true";
        p.wrap = get("wrap_mode" + k, "clamp_to_border");
        p.frame_count_mod = atoi(get("frame_count_mod" + k, "0").c_str());
        std::string both = get("scale_type" + k);
        bool last = i == n - 1;
        p.type_x = get("scale_type_x" + k, both.empty() ? (last ? "viewport" : "source") : both);
        p.type_y = get("scale_type_y" + k, both.empty() ? (last ? "viewport" : "source") : both);
        std::string sc = get("scale" + k, "1.0");
        p.scale_x = (float)atof(get("scale_x" + k, sc).c_str());
        p.scale_y = (float)atof(get("scale_y" + k, sc).c_str());
        std::string vs = read_file(dir + "/" + get("shader" + k) + ".vert");
        std::string fs = read_file(dir + "/" + get("shader" + k) + ".frag");
        if (vs.empty() || fs.empty()) {
            fprintf(stderr, "picture: cannot read %s/%s.vert/.frag\n", dir.c_str(), get("shader" + k).c_str());
            return false;
        }
        if (!link_pass(i, vs, fs)) return false;
    }
    if (m_passes.back().type_x != "viewport" || m_passes.back().type_y != "viewport" || m_passes.back().scale_x != 1 ||
        m_passes.back().scale_y != 1)
        fprintf(stderr, "picture: %s: the last pass is drawn at the viewport size\n", name.c_str());
    read_params();
    return true;
}

GLenum target_format(const Pass &p) {
    static const std::map<std::string, GLenum> formats = {
        {"R8G8B8A8_UNORM", GL_RGBA8},        {"R8G8B8A8_SRGB", GL_SRGB8_ALPHA8},
        {"A2B10G10R10_UNORM_PACK32", GL_RGB10_A2}, {"R16G16B16A16_SFLOAT", GL_RGBA16F},
        {"R32G32B32A32_SFLOAT", GL_RGBA32F}, {"R16G16B16A16_UNORM", GL_RGBA16},
    };
    if (!p.format.empty()) {
        auto it = formats.find(p.format);
        if (it != formats.end()) return it->second;
        fprintf(stderr, "picture: format %s: 8 bits instead\n", p.format.c_str());
    }
    return p.srgb ? GL_SRGB8_ALPHA8 : p.fp ? GL_RGBA16F : GL_RGBA8;
}

void Chain::draw(int w, int h, int x, int y, int vw, int vh, int win_w, int win_h) {
    m_vw = vw;
    m_vh = vh;
    int n = (int)m_passes.size();
    // sizes, and targets for every pass but the last
    for (int i = 0; i < n; i++) {
        Pass &p = m_passes[i];
        int sw = i ? m_passes[i - 1].w : w, sh = i ? m_passes[i - 1].h : h;
        auto axis = [](const std::string &t, float s, int src, int view) {
            float v = t == "absolute" ? s : t == "viewport" ? view * s : src * s;
            return std::max(1, (int)std::lround(v));
        };
        p.w = i == n - 1 ? vw : axis(p.type_x, p.scale_x, sw, vw);
        p.h = i == n - 1 ? vh : axis(p.type_y, p.scale_y, sh, vh);
        if (i == n - 1) break;
        bool mip = m_passes[i + 1].mipmap_input;
        GLenum fmt = target_format(p);
        Target &t = p.out;
        if (t.tex && t.w == p.w && t.h == p.h && t.fmt == fmt && t.mip == mip) continue;
        if (!t.tex) {
            gl.glGenTextures(1, &t.tex);
            gl.glGenFramebuffers(1, &t.fbo);
        }
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, t.tex);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, fmt, p.w, p.h, 0, GL_RGBA, fmt == GL_RGBA16F || fmt == GL_RGBA32F ? GL_FLOAT : GL_UNSIGNED_BYTE,
                        nullptr);
        if (mip) gl.glGenerateMipmap(GL_TEXTURE_2D);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip ? 1000 : 0);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "picture: %s pass %d: a %dx%d target is not usable here\n", name.c_str(), i, p.w, p.h);
        t.w = p.w;
        t.h = p.h;
        t.fmt = fmt;
        t.mip = mip;
    }
    for (int i = 0; i < n; i++) {
        Pass &p = m_passes[i];
        bool last = i == n - 1;
        if (p.mipmap_input && i > 0) {
            gl.glActiveTexture(GL_TEXTURE0);
            gl.glBindTexture(GL_TEXTURE_2D, m_passes[i - 1].out.tex);
            gl.glGenerateMipmap(GL_TEXTURE_2D);
        }
        if (last) {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.glDisable(GL_FRAMEBUFFER_SRGB);
            gl.glViewport(0, 0, win_w, win_h);
            gl.glClearColor(0, 0, 0, 1);
            gl.glClear(GL_COLOR_BUFFER_BIT);
            gl.glViewport(x, win_h - y - vh, vw, vh);
        } else {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, p.out.fbo);
            if (p.out.fmt == GL_SRGB8_ALPHA8) gl.glEnable(GL_FRAMEBUFFER_SRGB);
            else gl.glDisable(GL_FRAMEBUFFER_SRGB);
            gl.glViewport(0, 0, p.w, p.h);
        }
        gl.glUseProgram(p.prog);
        for (const SamplerUse &s : p.samplers) {
            GLuint tex = 0;
            if (s.src == S_ORIGINAL) tex = g_cur->tex;
            else if (s.src == S_PASS) tex = m_passes[s.idx].out.tex;
            else if (s.idx >= 0) tex = m_luts[s.idx].tex;
            gl.glActiveTexture(GL_TEXTURE0 + s.unit);
            gl.glBindTexture(GL_TEXTURE_2D, tex);
            gl.glBindSampler(s.unit, s.idx >= 0 || s.src != S_LUT ? sampler_for(sampling_of(s.src, s.idx)) : 0);
        }
        uint32_t frame = p.frame_count_mod > 0 ? m_frame % (uint32_t)p.frame_count_mod : m_frame;
        for (const Bind &b : p.binds) {
            float v4[16];
            int nf = 0;
            switch (b.kind) {
            case Bind::MVP: mvp(v4, last); nf = 16; break;
            case Bind::SIZE: {
                int sw = 0, sh = 0;
                size_of(b.src, b.idx, i, sw, sh);
                v4[0] = (float)sw; v4[1] = (float)sh; v4[2] = 1.0f / sw; v4[3] = 1.0f / sh;
                nf = 4;
                break;
            }
            case Bind::PARAM: v4[0] = m_params[b.idx].value; nf = 1; break;
            case Bind::FRAMECOUNT:
            case Bind::FRAMEDIR: {
                uint32_t u = b.kind == Bind::FRAMECOUNT ? frame : 1u;
                if (b.in_ubo) {
                    if (b.type == GL_FLOAT) { float f = (float)u; memcpy(&p.ubo_data[b.offset], &f, 4); }
                    else memcpy(&p.ubo_data[b.offset], &u, 4);
                } else if (b.type == GL_UNSIGNED_INT) gl.glUniform1ui(b.loc, u);
                else if (b.type == GL_INT) gl.glUniform1i(b.loc, (GLint)u);
                else gl.glUniform1f(b.loc, (float)u);
                continue;
            }
            }
            if (b.in_ubo) memcpy(&p.ubo_data[b.offset], v4, nf * 4);   // std140: mat4 = 4 vec4 columns
            else if (nf == 16) gl.glUniformMatrix4fv(b.loc, 1, GL_FALSE, v4);
            else if (nf == 4) gl.glUniform4f(b.loc, v4[0], v4[1], v4[2], v4[3]);
            else gl.glUniform1f(b.loc, v4[0]);
        }
        if (p.ubo) {
            gl.glBindBuffer(GL_UNIFORM_BUFFER, p.ubo);
            gl.glBufferSubData(GL_UNIFORM_BUFFER, 0, p.ubo_size, p.ubo_data.data());
            gl.glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.ubo);
        }
        gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    for (int u = 0; u < 16; u++) gl.glBindSampler(u, 0);
    m_frame++;
}

const char *const DIRS[] = {"", "crt-guest-advanced-fast", "crt-royale"};

// GPU time per frame, per mode (timer queries, read a few frames late so nothing waits)
struct GpuTime {
    GLuint q[4] = {};
    int mode[4] = {};
    uint32_t issued = 0, read = 0;
    double sum_ms[3] = {}, max_ms[3] = {};
    uint64_t n[3] = {};
} g_gpu;

void gpu_collect(bool all) {
    while (g_gpu.read < g_gpu.issued) {
        GLuint q = g_gpu.q[g_gpu.read % 4];
        GLint ready = 0;
        if (!all) gl.glGetQueryObjectiv(q, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!all && !ready) return;
        GLuint64 ns = 0;
        gl.glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
        int m = g_gpu.mode[g_gpu.read % 4];
        double ms = ns / 1e6;
        if (g_gpu.n[m]++ > 30) {   // not the first frames: shader compilation, first allocations
            g_gpu.sum_ms[m] += ms;
            g_gpu.max_ms[m] = std::max(g_gpu.max_ms[m], ms);
        }
        g_gpu.read++;
    }
}
std::unique_ptr<Chain> g_chain[2][3];
bool g_failed[3] = {};

const char *const PLAIN_VS = R"(#version 330
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
out vec2 vTexCoord;
void main() { vTexCoord = TexCoord; gl_Position = vec4(2.0 * Position.x - 1.0, 1.0 - 2.0 * Position.y, 0.0, 1.0); }
)";
const char *const PLAIN_FS = R"(#version 330
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D Source;
void main() {
    ivec2 sz = textureSize(Source, 0);
    FragColor = vec4(texelFetch(Source, clamp(ivec2(vTexCoord * vec2(sz)), ivec2(0), sz - 1), 0).rgb, 1.0);
}
)";

// the overlay: its texels as they are (alpha included), times Alpha
const char *const OVERLAY_FS = R"(#version 330
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D Source;
uniform float Alpha;
void main() {
    ivec2 sz = textureSize(Source, 0);
    vec4 c = texelFetch(Source, clamp(ivec2(vTexCoord * vec2(sz)), ivec2(0), sz - 1), 0);
    FragColor = vec4(c.rgb, c.a * Alpha);
}
)";

void snap(int win_w, int win_h) {
    std::vector<uint8_t> px((size_t)win_w * win_h * 3);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glReadPixels(0, 0, win_w, win_h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(g_snap_path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", win_w, win_h);
    for (int y = win_h - 1; y >= 0; y--) fwrite(&px[(size_t)y * win_w * 3], 1, (size_t)win_w * 3, f);
    fclose(f);
    fprintf(stderr, "picture: frame %u written to %s\n", g_draws, g_snap_path.c_str());
}

}  // namespace

const char *mode_name(Mode m) { return m == Mode::light ? "light" : m == Mode::full ? "full" : "off"; }

const char *mode_label(Mode m) {
    return m == Mode::light ? "CRT light (crt-guest-advanced-fast)" : m == Mode::full ? "CRT full (crt-royale)" : "sharp pixels";
}

bool parse_mode(const std::string &s, Mode &m) {
    if (s == "off" || s == "sharp") m = Mode::off;
    else if (s == "light" || s == "crt") m = Mode::light;
    else if (s == "full" || s == "royale") m = Mode::full;
    else return false;
    return true;
}

bool init(const std::string &shaders_dir, const std::vector<std::string> &config_dirs) {
    if (!load_gl()) return false;
    g_shaders_dir = shaders_dir;
    g_config_dirs = config_dirs;
    g_plain = link(PLAIN_VS, PLAIN_FS, "sharp pixels");
    if (!g_plain) return false;
    gl.glUseProgram(g_plain);
    gl.glUniform1i(gl.glGetUniformLocation(g_plain, "Source"), 0);
    // the quad: Position (x, y, 0, 1) and TexCoord (x, y), x and y in 0..1
    static const float quad[] = {0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1};
    gl.glGenVertexArrays(1, &g_vao);
    gl.glBindVertexArray(g_vao);
    gl.glGenBuffers(1, &g_vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    gl.glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(4 * sizeof(float)));
    gl.glEnableVertexAttribArray(0);
    gl.glEnableVertexAttribArray(1);
    gl.glDisable(GL_BLEND);
    gl.glGenQueries(4, g_gpu.q);
    if (const char *e = getenv("HK_PICTURE_SNAP")) {
        g_snap_frame = (uint32_t)strtoul(e, nullptr, 10);
        if (const char *c = strchr(e, ',')) g_snap_path = c + 1;
    }
    fprintf(stderr, "picture: OpenGL %s on %s\n", (const char *)gl.glGetString(GL_VERSION),
            (const char *)gl.glGetString(GL_RENDERER));
    g_ok = true;
    return true;
}

bool prepare(Mode m) {
    int k = (int)m;
    if (m == Mode::off || g_chain[g_view][k]) return true;
    if (g_failed[k] || !g_ok) return false;
    auto c = std::make_unique<Chain>();
    c->name = DIRS[k];
    uint64_t t0 = SDL_GetTicksNS();
    if (!c->load(g_shaders_dir + "/" + DIRS[k])) {
        fprintf(stderr, "picture: no %s here: sharp pixels instead\n", mode_label(m));
        g_failed[k] = true;
        return false;
    }
    fprintf(stderr, "picture: %s ready (%.0f ms)\n", mode_label(m), (SDL_GetTicksNS() - t0) / 1e6);
    g_chain[g_view][k] = std::move(c);
    return true;
}

Mode draw(const uint32_t *rgb, int w, int h, int x, int y, int vw, int vh, int win_w, int win_h, Mode m, int view) {
    if (!g_ok || w <= 0 || h <= 0 || vw <= 0 || vh <= 0) return Mode::off;
    g_view = view & 1;
    g_cur = &g_frames[g_view];
    gl.glActiveTexture(GL_TEXTURE0);
    if (!g_cur->tex || w != g_cur->w || h != g_cur->h) {
        if (!g_cur->tex) gl.glGenTextures(1, &g_cur->tex);
        gl.glBindTexture(GL_TEXTURE_2D, g_cur->tex);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);   // XRGB: the X byte is not alpha
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
        g_cur->w = w;
        g_cur->h = h;
    }
    gl.glBindTexture(GL_TEXTURE_2D, g_cur->tex);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, rgb);

    if (!prepare(m)) m = Mode::off;
    int k = (int)m;
    gpu_collect(false);
    bool timed = g_gpu.issued - g_gpu.read < 4;
    if (timed) {
        g_gpu.mode[g_gpu.issued % 4] = k;
        gl.glBeginQuery(GL_TIME_ELAPSED, g_gpu.q[g_gpu.issued % 4]);
    }

    if (m == Mode::off) {
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl.glDisable(GL_FRAMEBUFFER_SRGB);
        gl.glViewport(0, 0, win_w, win_h);
        gl.glClearColor(0, 0, 0, 1);
        gl.glClear(GL_COLOR_BUFFER_BIT);
        gl.glViewport(x, win_h - y - vh, vw, vh);
        gl.glUseProgram(g_plain);
        gl.glBindSampler(0, 0);
        gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        if (g_draws % 30 == 0) g_chain[g_view][k]->check_params();
        g_chain[g_view][k]->draw(w, h, x, y, vw, vh, win_w, win_h);
    }
    if (timed) {
        gl.glEndQuery(GL_TIME_ELAPSED);
        g_gpu.issued++;
    }
    if (g_draws == g_snap_frame && !g_snap_path.empty()) snap(win_w, win_h);
    g_draws++;
    return m;
}

void overlay(const uint32_t *argb, int w, int h, int x, int y, int dw, int dh, float alpha, int win_w, int win_h) {
    if (!g_ok || w <= 0 || h <= 0) return;
    if (!g_over) {
        g_over = link(PLAIN_VS, OVERLAY_FS, "overlay");
        if (!g_over) return;
        gl.glUseProgram(g_over);
        gl.glUniform1i(gl.glGetUniformLocation(g_over, "Source"), 0);
        gl.glGenTextures(1, &g_over_tex);
        gl.glBindTexture(GL_TEXTURE_2D, g_over_tex);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, g_over_tex);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.glDisable(GL_FRAMEBUFFER_SRGB);
    gl.glViewport(x, win_h - y - dh, dw, dh);
    gl.glUseProgram(g_over);
    gl.glUniform1f(gl.glGetUniformLocation(g_over, "Alpha"), alpha);
    gl.glBindSampler(0, 0);
    gl.glBindVertexArray(g_vao);
    gl.glEnable(GL_BLEND);
    gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.glDisable(GL_BLEND);
    gl.glViewport(0, 0, win_w, win_h);
}

void shutdown() {
    if (!g_ok) return;
    gpu_collect(true);
    for (int m = 0; m < 3; m++)
        if (g_gpu.n[m] > 31)
            fprintf(stderr, "picture: %s: GPU %.2f ms per frame (worst %.2f) over %llu frames\n", mode_label((Mode)m),
                    g_gpu.sum_ms[m] / (g_gpu.n[m] - 31), g_gpu.max_ms[m], (unsigned long long)g_gpu.n[m]);
    gl.glDeleteQueries(4, g_gpu.q);
    for (auto &v : g_chain)
        for (auto &c : v) c.reset();
    if (g_plain) gl.glDeleteProgram(g_plain);
    if (g_over) gl.glDeleteProgram(g_over);
    if (g_over_tex) gl.glDeleteTextures(1, &g_over_tex);
    for (Frame &f : g_frames)
        if (f.tex) gl.glDeleteTextures(1, &f.tex);
    if (g_vbo) gl.glDeleteBuffers(1, &g_vbo);
    if (g_vao) gl.glDeleteVertexArrays(1, &g_vao);
    g_ok = false;
}

}  // namespace hk::host::picture
