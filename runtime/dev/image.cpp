// SPDX-License-Identifier: GPL-3.0-or-later
// See image.h. The CD layout is MAME's (util/cdrom.cpp: a frame is 2352 data + 96 subcode bytes,
// the data first; tracks padded to 4 frames; audio stored big-endian) and the cue sheet and .bin
// are chdman's (tools/chdman.cpp, do_extract_cd and output_track_metadata, cue/bin mode).
#include "image.h"

#include <libchdr/chd.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#include "shell/overlay.h"
#include "shell/sys.h"

namespace hk::image {

namespace {

bool ends_with_ci(const std::string &s, const char *e) {
    size_t n = strlen(e);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, e) == 0;
}

std::string base_name(const std::string &path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

constexpr uint32_t CD_FRAME = 2448;   // 2352 data + 96 subcode, as a CHD stores a CD frame

struct Track {
    std::string type;      // as chdman's cue writes it: MODE1/2048, MODE2/2352, AUDIO, ...
    uint32_t datasize = 0;
    bool audio = false;
    uint32_t frames = 0, pregap = 0, postgap = 0, pgdatasize = 0;
    uint32_t chdframe = 0;   // the track's first frame among the CHD's frames
    uint64_t binoff = 0;     // where its frames start in the extracted .bin
};

// MAME's type strings (cdrom_file::get_info_from_type_string): the data size
uint32_t data_size(const char *t) {
    static const struct { const char *name; uint32_t size; } types[] = {
        {"MODE1", 2048}, {"MODE1/2048", 2048}, {"MODE1_RAW", 2352}, {"MODE1/2352", 2352},
        {"MODE2", 2336}, {"MODE2/2336", 2336}, {"MODE2_FORM1", 2048}, {"MODE2/2048", 2048},
        {"MODE2_FORM2", 2324}, {"MODE2/2324", 2324}, {"MODE2_FORM_MIX", 2336}, {"MODE2_RAW", 2352},
        {"MODE2/2352", 2352}, {"CDI/2352", 2352}, {"AUDIO", 2352},
    };
    for (auto &e : types)
        if (!strcmp(t, e.name)) return e.size;
    return 0;
}

class Chd {
public:
    ~Chd() {
        if (worker_.joinable()) {
            {
                std::lock_guard<std::mutex> lock(cache_mu_);
                stop_ = true;
            }
            cv_.notify_all();
            worker_.join();
        }
        if (wchd_) chd_close(wchd_);
        if (chd_) chd_close(chd_);
    }

    bool open(const std::string &path) {
        path_ = path;
        if (chd_open(path.c_str(), CHD_OPEN_READ, nullptr, &chd_) != CHDERR_NONE) {
            chd_ = nullptr;
            return false;
        }
        const chd_header *h = chd_get_header(chd_);
        hunkbytes_ = h->hunkbytes;
        logical_ = h->logicalbytes;
        if (read_cd_tracks()) {
            kind_ = "cd";
            size_ = 0;
            for (auto &t : tracks_) size_ += (uint64_t)t.frames * t.datasize;
        } else {
            char buf[16];
            uint32_t len = 0;
            kind_ = chd_get_metadata(chd_, CHD_MAKE_TAG('D', 'V', 'D', ' '), 0, buf, sizeof buf, &len, nullptr, nullptr) ==
                            CHDERR_NONE ? "dvd" : "hd";
            size_ = logical_;
        }
        cap_hunks_ = std::max<size_t>(64, (32u << 20) / hunkbytes_);
        ahead_hunks_ = std::max<uint32_t>(8, (4u << 20) / hunkbytes_);
        return true;
    }

    const std::string &kind() const { return kind_; }
    uint64_t size() const { return size_; }

    // n bytes at off of the image as extracted
    ssize_t pread(void *buf, size_t n, uint64_t off) {
        std::lock_guard<std::mutex> lock(mu_);
        if (off >= size_) return 0;
        n = (size_t)std::min<uint64_t>(n, size_ - off);
        uint8_t *out = (uint8_t *)buf;
        size_t done = 0;
        while (done < n) {
            size_t got = kind_ == "cd" ? read_cd(out + done, n - done, off + done) : read_logical(out + done, n - done, off + done);
            if (got == 0) {
                errno = EIO;
                return done ? (ssize_t)done : -1;
            }
            done += got;
        }
        return (ssize_t)done;
    }

    std::string cue() const {
        if (kind_ != "cd") return {};
        auto msf = [](uint32_t f) {
            char s[16];
            snprintf(s, sizeof s, "%02d:%02d:%02d", f / (75 * 60), (f / 75) % 60, f % 75);
            return std::string(s);
        };
        std::string out = "FILE \"" + base_name(path_) + "\" BINARY\n";
        uint32_t frameoffs = 0;
        for (size_t k = 0; k < tracks_.size(); k++) {
            const Track &t = tracks_[k];
            char line[64];
            snprintf(line, sizeof line, "  TRACK %02d %s\n", (int)k + 1, t.type.c_str());
            out += line;
            if (t.pregap > 0 && t.pgdatasize == 0) {
                out += "    PREGAP " + msf(t.pregap) + "\n";
                out += "    INDEX 01 " + msf(frameoffs) + "\n";
            } else if (t.pregap > 0) {
                out += "    INDEX 00 " + msf(frameoffs) + "\n";
                out += "    INDEX 01 " + msf(frameoffs + t.pregap) + "\n";
            } else {
                out += "    INDEX 01 " + msf(frameoffs) + "\n";
            }
            if (t.postgap > 0) out += "    POSTGAP " + msf(t.postgap) + "\n";
            frameoffs += t.frames;
        }
        return out;
    }

private:
    // CHT2 (or the older CHTR) metadata, one entry per track
    bool read_cd_tracks() {
        uint32_t chdframe = 0;
        uint64_t binoff = 0;
        for (uint32_t i = 0;; i++) {
            char meta[256] = {};
            uint32_t len = 0;
            int n = 0, frames = 0, pregap = 0, postgap = 0;
            char type[32] = {}, sub[32] = {}, pgtype[32] = {}, pgsub[32] = {};
            if (chd_get_metadata(chd_, CDROM_TRACK_METADATA2_TAG, i, meta, sizeof meta - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
                if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &n, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) != 8)
                    return false;
            } else if (chd_get_metadata(chd_, CDROM_TRACK_METADATA_TAG, i, meta, sizeof meta - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
                if (sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &n, type, sub, &frames) != 4) return false;
            } else {
                break;
            }
            Track t;
            t.datasize = data_size(type);
            if (!t.datasize) return false;
            t.audio = !strcmp(type, "AUDIO");
            // chdman's cue names: MODE1/<size>, MODE2/<size>, AUDIO
            if (t.audio) t.type = "AUDIO";
            else t.type = std::string(strncmp(type, "MODE1", 5) == 0 ? "MODE1/" : "MODE2/") + std::to_string(t.datasize);
            t.frames = (uint32_t)frames;
            t.pregap = (uint32_t)pregap;
            t.postgap = (uint32_t)postgap;
            if (pregap > 0 && pgtype[0] == 'V') t.pgdatasize = data_size(pgtype + 1);
            t.chdframe = chdframe;
            t.binoff = binoff;
            chdframe += (t.frames + 3) / 4 * 4;   // MAME's TRACK_PADDING
            binoff += (uint64_t)t.frames * t.datasize;
            tracks_.push_back(t);
        }
        return !tracks_.empty();
    }

    using Hunk = std::shared_ptr<const std::vector<uint8_t>>;

    // the cache: decompressed hunks, the most recently used first (under cache_mu_)
    Hunk cached(uint32_t h) {
        auto it = index_.find(h);
        if (it == index_.end()) return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second);
        return it->second->second;
    }
    void insert(uint32_t h, Hunk data) {
        if (index_.count(h)) return;
        lru_.emplace_front(h, std::move(data));
        index_[h] = lru_.begin();
        while (lru_.size() > cap_hunks_) {
            index_.erase(lru_.back().first);
            lru_.pop_back();
        }
    }

    // One hunk, decompressed (the reader's side: under mu_). A read also asks the worker for the
    // hunks after it, so that a run of reads (a game loading a file, a video playing) finds them
    // decompressed: the reader then waits neither for the disk nor for the decompression.
    Hunk hunk(uint32_t h) {
        Hunk p;
        {
            std::lock_guard<std::mutex> lock(cache_mu_);
            p = cached(h);
            if (h != last_hunk_) {
                last_hunk_ = h;
                ahead_from_ = h + 1;
                ahead_to_ = h + 1 + ahead_hunks_;
                if (!worker_.joinable()) worker_ = std::thread([this] { read_ahead(); });
                cv_.notify_one();
            }
        }
        if (p) return p;
        auto data = std::make_shared<std::vector<uint8_t>>(hunkbytes_);
        if (chd_read(chd_, h, data->data()) != CHDERR_NONE) return nullptr;
        std::lock_guard<std::mutex> lock(cache_mu_);
        insert(h, data);
        return data;
    }

    // the worker: its own handle on the CHD, decompressing the hunks asked for into the cache
    void read_ahead() {
        if (chd_open(path_.c_str(), CHD_OPEN_READ, nullptr, &wchd_) != CHDERR_NONE) {
            wchd_ = nullptr;
            return;
        }
        const chd_header *hd = chd_get_header(wchd_);
        std::unique_lock<std::mutex> lock(cache_mu_);
        for (;;) {
            cv_.wait(lock, [this] { return stop_ || ahead_from_ < ahead_to_; });
            if (stop_) return;
            uint32_t h = ahead_from_++;
            if (h >= hd->totalhunks) {
                ahead_from_ = ahead_to_;
                continue;
            }
            if (index_.count(h)) continue;
            lock.unlock();
            auto data = std::make_shared<std::vector<uint8_t>>(hunkbytes_);
            bool ok = chd_read(wchd_, h, data->data()) == CHDERR_NONE;
            lock.lock();
            if (ok) insert(h, data);
        }
    }

    size_t read_logical(uint8_t *out, size_t n, uint64_t off) {
        Hunk hp = hunk((uint32_t)(off / hunkbytes_));
        if (!hp) return 0;
        const uint8_t *p = hp->data();
        size_t at = (size_t)(off % hunkbytes_), len = std::min(n, (size_t)hunkbytes_ - at);
        memcpy(out, p + at, len);
        return len;
    }

    // a part of one frame of the extracted .bin
    size_t read_cd(uint8_t *out, size_t n, uint64_t off) {
        size_t k = 0;
        while (k + 1 < tracks_.size() && off >= tracks_[k + 1].binoff) k++;
        const Track &t = tracks_[k];
        const uint64_t rel = off - t.binoff;
        const uint32_t frame = (uint32_t)(rel / t.datasize), at = (uint32_t)(rel % t.datasize);
        const uint64_t pos = (uint64_t)(t.chdframe + frame) * CD_FRAME;
        Hunk hp = hunk((uint32_t)(pos / hunkbytes_));
        if (!hp) return 0;
        const uint8_t *f = hp->data() + pos % hunkbytes_;   // a hunk holds whole frames
        size_t len = std::min(n, (size_t)(t.datasize - at));
        if (!t.audio) {
            memcpy(out, f + at, len);
        } else {
            // the CHD keeps CD audio big-endian; extractcd writes it little-endian
            for (size_t i = 0; i < len; i++) out[i] = f[(at + i) ^ 1];
        }
        return len;
    }

    std::string path_, kind_;
    chd_file *chd_ = nullptr;    // the reader's handle (under mu_)
    chd_file *wchd_ = nullptr;   // the read-ahead worker's
    uint32_t hunkbytes_ = 0;
    uint64_t logical_ = 0, size_ = 0;
    std::vector<Track> tracks_;
    std::mutex mu_;              // one reader at a time
    // the cache (32 MiB of hunks) and the read-ahead (4 MiB past the last read), under cache_mu_
    std::mutex cache_mu_;
    std::condition_variable cv_;
    std::list<std::pair<uint32_t, Hunk>> lru_;
    std::unordered_map<uint32_t, std::list<std::pair<uint32_t, Hunk>>::iterator> index_;
    size_t cap_hunks_ = 64;
    uint32_t ahead_hunks_ = 8, ahead_from_ = 0, ahead_to_ = 0, last_hunk_ = UINT32_MAX;
    bool stop_ = false;
    std::thread worker_;
};

// a CHD's stream: the CHD and the stream's position
struct Cookie {
    std::shared_ptr<Chd> chd;
    uint64_t pos = 0;
};

// never destroyed: a stream may be closed by another static's destructor at exit (a game's disk)
std::mutex &g_streams_mu = *new std::mutex;
std::map<FILE *, Cookie *> &streams() {
    static auto *s = new std::map<FILE *, Cookie *>;
    return *s;
}

#ifndef _WIN32
ssize_t c_read(void *c, char *buf, size_t n) {
    Cookie *k = (Cookie *)c;
    ssize_t r = k->chd->pread(buf, n, k->pos);
    if (r > 0) k->pos += (uint64_t)r;
    return r;
}

int c_seek(void *c, off64_t *off, int whence) {
    Cookie *k = (Cookie *)c;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)k->pos : (int64_t)k->chd->size();
    int64_t to = base + *off;
    if (to < 0) return -1;
    k->pos = (uint64_t)to;
    *off = to;
    return 0;
}

#endif

int c_close(void *c) {
    Cookie *k = (Cookie *)c;
    {
        std::lock_guard<std::mutex> lock(g_streams_mu);
        for (auto it = streams().begin(); it != streams().end(); ++it)
            if (it->second == k) {
                streams().erase(it);
                break;
            }
    }
    delete k;
    return 0;
}

std::shared_ptr<Chd> open_chd(const std::string &path) {
    auto c = std::make_shared<Chd>();
    if (!c->open(path)) return nullptr;
    return c;
}

}  // namespace

bool is_chd(const std::string &path) { return ends_with_ci(path, ".chd"); }

std::string chd_kind(const std::string &path) {
    if (!is_chd(path)) return {};
    auto c = open_chd(path);
    return c ? c->kind() : std::string();
}

namespace {
// mods' replaced byte ranges (shell/overlay.h), by stream: laid over what pread() reads
std::mutex g_overlay_mu;
std::map<FILE *, int> &overlays() {
    static std::map<FILE *, int> m;
    return m;
}
FILE *with_overlay(FILE *f, const std::string &path) {
    if (f)
        if (const int id = overlay::find(path)) {
            std::lock_guard<std::mutex> lock(g_overlay_mu);
            overlays()[f] = id;
        }
    return f;
}
}  // namespace

FILE *fopen_image(const std::string &path) {
    return with_overlay(fopen_image_plain(path), path);
}

namespace {
// mods find files on discs through this (shell/overlay.h, read()): a CHD as extracted too
bool overlay_reader(const std::string &path, uint64_t off, void *buf, size_t n) {
    FILE *f = fopen_image_plain(path);
    if (!f) return false;
    const bool ok = pread(f, buf, n, off) == (ssize_t)n;
    close(f);
    return ok;
}
const bool g_reader_set = (overlay::set_reader(overlay_reader), true);
}  // namespace

FILE *fopen_image_plain(const std::string &path) {
    if (!is_chd(path)) return fopen(path.c_str(), "rb");
    auto c = open_chd(path);
    if (!c) return nullptr;
    Cookie *k = new Cookie{c, 0};
#ifdef _WIN32
    // no fopencookie: a stand-in stream (the null device's), which pread() and size() know
    FILE *f = fopen("NUL", "rb");
#else
    cookie_io_functions_t io = {c_read, nullptr, c_seek, c_close};
    FILE *f = fopencookie(k, "rb", io);
#endif
    if (!f) {
        delete k;
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_streams_mu);
    streams()[f] = k;
    return f;
}

std::string chd_cue_text(const std::string &path) {
    if (!is_chd(path)) return {};
    auto c = open_chd(path);
    return c ? c->cue() : std::string();
}

FILE *fopen_cue(const std::string &path) {
    if (!is_chd(path)) return fopen(path.c_str(), "r");
    std::string text = chd_cue_text(path);
    if (text.empty()) return nullptr;
    FILE *f = tmpfile();   // a stream of its own, freed by fclose
    if (!f) return nullptr;
    fwrite(text.data(), 1, text.size(), f);
    rewind(f);
    return f;
}

ssize_t pread(FILE *f, void *buf, size_t n, uint64_t off) {
    Cookie *k = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_streams_mu);
        auto it = streams().find(f);
        if (it != streams().end()) k = it->second;
    }
    const ssize_t r = k ? k->chd->pread(buf, n, off) : sys::pread(fileno(f), buf, n, off);
    if (r > 0) {
        int id = 0;
        {
            std::lock_guard<std::mutex> lock(g_overlay_mu);
            if (!overlays().empty())
                if (auto it = overlays().find(f); it != overlays().end()) id = it->second;
        }
        if (id) overlay::apply(id, off, buf, (size_t)r);
    }
    return r;
}

int64_t chd_size(FILE *f) {
    std::lock_guard<std::mutex> lock(g_streams_mu);
    auto it = streams().find(f);
    return it == streams().end() ? -1 : (int64_t)it->second->chd->size();
}

int64_t size(FILE *f) {
    if (int64_t n = chd_size(f); n >= 0) return n;
#ifdef _WIN32
    struct _stat64 st;
    return _fstat64(fileno(f), &st) == 0 ? (int64_t)st.st_size : -1;
#else
    struct stat st;
    return fstat(fileno(f), &st) == 0 ? (int64_t)st.st_size : -1;
#endif
}

void close(FILE *f) {
    if (!f) return;
    {
        std::lock_guard<std::mutex> lock(g_overlay_mu);
        overlays().erase(f);
    }
#ifdef _WIN32
    Cookie *k = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_streams_mu);
        auto it = streams().find(f);
        if (it != streams().end()) k = it->second;
    }
    if (k) c_close(k);
#endif
    fclose(f);   // a CHD's, elsewhere: c_close
}

}  // namespace hk::image
