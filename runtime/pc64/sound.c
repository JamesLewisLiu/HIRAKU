// SPDX-License-Identifier: GPL-3.0-or-later
// The game's sound (PLAN.md section 32): WASAPI, shared mode, 10 ms, followed from CoCreateInstance
// to its IAudioClient. Its sound library resamples to the mix format through Windows' resampler
// DMO, which fails under Wine (dmo resampler_init -22) and stalled its sound after 30 ms; the mix
// format is reported at the game's own 44.1 kHz, so nothing is resampled in the game (Wine
// converts).
// Linked to hiraku-pc, the loader is the sound device, as runtime/pc/dsound.c is for pop'n 15-18:
// Wine's stream is set up but never started, the game's IAudioRenderClient is the loader's, and
// what it writes waits in a buffer of BUF_FRAMES (its padding) until hiraku-pc asks for it
// (audio_want: a frame's sound for each frame shown), when it goes into the link's ring and the
// game's event says there is room. The game uses only IAudioClient and IAudioRenderClient.
#define COBJMACROS
#define CINTERFACE
#include "pc64.h"
#include "../pc/link.h"

#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdlib.h>
#include <string.h>

static const GUID float_format = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};   // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT

// The buffer: at least two of hiraku-pc's frames (735 frames each), whatever the game asks (10 ms)
#define BUF_FRAMES 2048u   // a power of two
#define MIN_FRAMES 1024u

static struct {
    int on;                          // the link takes the sound
    int started;
    UINT32 size;                     // the buffer's frames, as the game is told
    UINT32 channels, bytes;          // the game's format: its channels, a sample's bytes
    int is_float;
    HANDLE event;                    // the game's (event-driven streams)
    volatile UINT32 written, played; // frames, since the start
    float fifo[2 * BUF_FRAMES];
    BYTE staging[BUF_FRAMES * 8 * 4];
    CRITICAL_SECTION lock;
} g_s;

static IMMDeviceEnumeratorVtbl r_enumerator;
static IMMDeviceVtbl r_device;
static IAudioClientVtbl r_client;

static HRESULT WINAPI GetMixFormat_(IAudioClient *c, WAVEFORMATEX **f) {
    HRESULT h = r_client.GetMixFormat(c, f);
    if (SUCCEEDED(h) && *f) {
        (*f)->nSamplesPerSec = 44100;
        (*f)->nAvgBytesPerSec = 44100 * (*f)->nBlockAlign;
    }
    return h;
}
static void start_mixer(void);

static HRESULT WINAPI Initialize_(IAudioClient *c, AUDCLNT_SHAREMODE m, DWORD flags, REFERENCE_TIME d, REFERENCE_TIME p, const WAVEFORMATEX *f,
                                  LPCGUID session) {
    HRESULT h = r_client.Initialize(c, m, flags, d, p, f, session);
    if (!f) return h;
    int is_float = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22)
        is_float = IsEqualGUID(&((const WAVEFORMATEXTENSIBLE *)f)->SubFormat, &float_format);
    const int fits = f->nSamplesPerSec == HK_PC_AUDIO_RATE && f->nChannels >= 1 && f->nChannels <= 8 &&
                     ((is_float && f->wBitsPerSample == 32) || (!is_float && (f->wBitsPerSample == 16 || f->wBitsPerSample == 32)));
    if (SUCCEEDED(h) && pc64_link() && fits && !g_s.on) {
        InitializeCriticalSection(&g_s.lock);
        g_s.on = 1;
        g_s.channels = f->nChannels, g_s.bytes = f->wBitsPerSample / 8, g_s.is_float = is_float;
        const UINT32 asked = (UINT32)(d * HK_PC_AUDIO_RATE / 10000000);
        g_s.size = asked < MIN_FRAMES ? MIN_FRAMES : asked > BUF_FRAMES ? BUF_FRAMES : asked;
        start_mixer();
    }
    pc64_log("sound: %s, %u Hz, %u channels, %u bits%s, %lld ms%s: %s", m ? "exclusive" : "shared", (unsigned)f->nSamplesPerSec, f->nChannels,
             f->wBitsPerSample, is_float ? " float" : "", (long long)(d / 10000), flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK ? ", events" : "",
             !SUCCEEDED(h) ? "failed" : g_s.on ? "through hiraku-pc" : "on");
    return h;
}

// --- the loader as the device (g_s.on)
static HRESULT WINAPI GetBufferSize_(IAudioClient *c, UINT32 *n) {
    if (!g_s.on) return r_client.GetBufferSize(c, n);
    if (!n) return E_POINTER;
    *n = g_s.size;
    return S_OK;
}
static HRESULT WINAPI GetCurrentPadding_(IAudioClient *c, UINT32 *n) {
    if (!g_s.on) return r_client.GetCurrentPadding(c, n);
    if (!n) return E_POINTER;
    *n = g_s.written - g_s.played;
    return S_OK;
}
static HRESULT WINAPI SetEventHandle_(IAudioClient *c, HANDLE e) {
    if (!g_s.on) return r_client.SetEventHandle(c, e);
    g_s.event = e;
    return S_OK;
}
static HRESULT WINAPI Start_(IAudioClient *c) {
    if (!g_s.on) return r_client.Start(c);
    if (g_s.started) return AUDCLNT_E_NOT_STOPPED;
    g_s.started = 1;
    struct hk_pc_link *l = pc64_link();
    if (!l->sound_on) {
        l->sound_on = 1;
        pc64_log("sound: started, through hiraku-pc (%u frames' buffer)", g_s.size);
    }
    if (g_s.event) SetEvent(g_s.event);
    return S_OK;
}
static HRESULT WINAPI Stop_(IAudioClient *c) {
    if (!g_s.on) return r_client.Stop(c);
    const int was = g_s.started;
    g_s.started = 0;
    return was ? S_OK : S_FALSE;
}
static HRESULT WINAPI Reset_(IAudioClient *c) {
    if (!g_s.on) return r_client.Reset(c);
    if (g_s.started) return AUDCLNT_E_NOT_STOPPED;
    EnterCriticalSection(&g_s.lock);
    g_s.played = g_s.written;
    LeaveCriticalSection(&g_s.lock);
    return S_OK;
}

// The game's render client: GetBuffer gives it the staging area, ReleaseBuffer puts it into the buffer
static HRESULT WINAPI rc_QueryInterface(IAudioRenderClient *r, REFIID iid, void **out) {
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IAudioRenderClient)) {
        *out = r;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rc_AddRef(IAudioRenderClient *r) { return (void)r, 2; }
static ULONG WINAPI rc_Release(IAudioRenderClient *r) { return (void)r, 1; }
static UINT32 g_asked;
static HRESULT WINAPI rc_GetBuffer(IAudioRenderClient *r, UINT32 n, BYTE **data) {
    (void)r;
    if (!data) return E_POINTER;
    if (n > g_s.size - (g_s.written - g_s.played)) return AUDCLNT_E_BUFFER_TOO_LARGE;
    g_asked = n;
    *data = g_s.staging;
    return S_OK;
}
static float sample(const BYTE *p) {
    if (g_s.is_float) return *(const float *)p;
    if (g_s.bytes == 2) return *(const int16_t *)p / 32768.0f;
    return (float)(*(const int32_t *)p / 2147483648.0);
}
static HRESULT WINAPI rc_ReleaseBuffer(IAudioRenderClient *r, UINT32 n, DWORD flags) {
    (void)r;
    if (n > g_asked) return AUDCLNT_E_INVALID_SIZE;
    EnterCriticalSection(&g_s.lock);
    const UINT32 frame = g_s.channels * g_s.bytes;
    for (UINT32 i = 0; i < n; i++) {
        float *o = g_s.fifo + 2 * ((g_s.written + i) & (BUF_FRAMES - 1));
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            o[0] = o[1] = 0.0f;
            continue;
        }
        const BYTE *in = g_s.staging + i * frame;
        o[0] = sample(in);
        o[1] = g_s.channels > 1 ? sample(in + g_s.bytes) : o[0];
    }
    g_s.written += n;
    LeaveCriticalSection(&g_s.lock);
    g_asked = 0;
    if (getenv("HK_PC64_SOUND_LOG")) {   // tests: what the game writes, every 5 seconds
        static float peak;
        static UINT32 said;
        for (UINT32 i = 0; i < n; i++) {
            const float *o = g_s.fifo + 2 * ((g_s.written - n + i) & (BUF_FRAMES - 1));
            if (o[0] > peak || -o[0] > peak) peak = o[0] > 0 ? o[0] : -o[0];
        }
        if (g_s.written - said >= 5 * HK_PC_AUDIO_RATE) {
            struct hk_pc_link *l = pc64_link();
            pc64_log("sound: written %u, played %u, peak %.3f; ring made %u, taken %u, asked %u", g_s.written, g_s.played, (double)peak,
                     l->audio_made, l->audio_taken, l->audio_want);
            said = g_s.written, peak = 0;
        }
    }
    return S_OK;
}
static IAudioRenderClientVtbl g_rc_vtbl = {rc_QueryInterface, rc_AddRef, rc_Release, rc_GetBuffer, rc_ReleaseBuffer};
static IAudioRenderClient g_rc = {&g_rc_vtbl};

static HRESULT WINAPI GetService_(IAudioClient *c, REFIID iid, void **out) {
    if (g_s.on && IsEqualIID(iid, &IID_IAudioRenderClient)) {
        if (!out) return E_POINTER;
        *out = &g_rc;
        return S_OK;
    }
    return r_client.GetService(c, iid, out);
}

// What hiraku-pc asks for (audio_want) and the buffer has, into the link's ring (with the lock
// held). Where the game is two frames behind what is asked, the rest is silence, so its sound
// doesn't fall behind the pictures; a stopped stream is silence. The frames given
static uint32_t to_ring(void) {
    struct hk_pc_link *l = pc64_link();
    float *ring = (float *)((char *)l + HK_PC_AUDIO);
    const uint32_t want = l->audio_want, made = l->audio_made, taken = l->audio_taken;
    if ((int32_t)(want - made) <= 0) return 0;
    uint32_t n = want - made;
    const uint32_t room = HK_PC_AUDIO_FRAMES - (made - taken), have = g_s.written - g_s.played;
    if (n > room) n = room;
    uint32_t from_game = n < have ? n : have, quiet = 0;
    if (!g_s.started) from_game = 0, quiet = n;
    else if (n - from_game > 2 * (HK_PC_AUDIO_RATE / 60)) quiet = n - from_game - HK_PC_AUDIO_RATE / 60;
    for (uint32_t i = 0; i < from_game + quiet; i++) {
        const uint32_t at = (made + i) & (HK_PC_AUDIO_FRAMES - 1);
        if (i < from_game) {
            const float *f = g_s.fifo + 2 * ((g_s.played + i) & (BUF_FRAMES - 1));
            ring[2 * at] = f[0], ring[2 * at + 1] = f[1];
        } else {
            ring[2 * at] = ring[2 * at + 1] = 0.0f;
        }
    }
    g_s.played += from_game;
    MemoryBarrier();
    l->audio_made = made + from_game + quiet;
    if (from_game && g_s.event) SetEvent(g_s.event);   // room in the buffer
    return from_game + quiet;
}

// A picture counted: the sound up to its frame's end asked for (as dsound.c's), and given
void pc64_sound_picture(void) {
    struct hk_pc_link *l = pc64_link();
    const uint32_t end = l ? l->audio_frame_end : 0;
    if (!g_s.on || !end) return;
    for (;;) {   // audio_want moved on to `end` (hiraku-pc may move it too, never back)
        const uint32_t want = l->audio_want;
        if ((int32_t)(end - want) <= 0 ||
            (uint32_t)InterlockedCompareExchange((volatile LONG *)&l->audio_want, (LONG)end, (LONG)want) == want)
            break;
    }
    EnterCriticalSection(&g_s.lock);
    to_ring();
    LeaveCriticalSection(&g_s.lock);
}

// As far as hiraku-pc asks; a nap when it has all it asked for
static DWORD WINAPI mixer(void *unused) {
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        EnterCriticalSection(&g_s.lock);
        const uint32_t n = to_ring();
        LeaveCriticalSection(&g_s.lock);
        if (!n) pc64_nap();
    }
    return 0;
}
static void start_mixer(void) { CreateThread(NULL, 0, mixer, NULL, 0, NULL); }

static void hook_device(IMMDevice *d);
static HRESULT WINAPI Activate_(IMMDevice *d, REFIID iid, DWORD ctx, PROPVARIANT *p, void **out) {
    HRESULT h = r_device.Activate(d, iid, ctx, p, out);
    if (SUCCEEDED(h) && IsEqualIID(iid, &IID_IAudioClient) && !r_client.Initialize) {
        IAudioClientVtbl *v = ((IAudioClient *)*out)->lpVtbl;
        r_client = *v;
        pc64_patch(&v->GetMixFormat, GetMixFormat_);
        pc64_patch(&v->Initialize, Initialize_);
        pc64_patch(&v->GetBufferSize, GetBufferSize_);
        pc64_patch(&v->GetCurrentPadding, GetCurrentPadding_);
        pc64_patch(&v->SetEventHandle, SetEventHandle_);
        pc64_patch(&v->Start, Start_);
        pc64_patch(&v->Stop, Stop_);
        pc64_patch(&v->Reset, Reset_);
        pc64_patch(&v->GetService, GetService_);
    }
    return h;
}
static void hook_device(IMMDevice *d) {
    if (r_device.Activate) return;
    IMMDeviceVtbl *v = d->lpVtbl;
    r_device = *v;
    pc64_patch(&v->Activate, Activate_);
}
static HRESULT WINAPI GetDefaultAudioEndpoint_(IMMDeviceEnumerator *e, EDataFlow f, ERole r, IMMDevice **out) {
    HRESULT h = r_enumerator.GetDefaultAudioEndpoint(e, f, r, out);
    if (SUCCEEDED(h)) hook_device(*out);
    return h;
}
static HRESULT WINAPI GetDevice_(IMMDeviceEnumerator *e, LPCWSTR id, IMMDevice **out) {
    HRESULT h = r_enumerator.GetDevice(e, id, out);
    if (SUCCEEDED(h)) hook_device(*out);
    return h;
}
static HRESULT(WINAPI *r_CoCreateInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
static HRESULT WINAPI CoCreateInstance_(REFCLSID c, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out) {
    HRESULT h = r_CoCreateInstance(c, outer, ctx, iid, out);
    if (SUCCEEDED(h) && IsEqualCLSID(c, &CLSID_MMDeviceEnumerator) && !r_enumerator.GetDefaultAudioEndpoint) {
        IMMDeviceEnumeratorVtbl *v = ((IMMDeviceEnumerator *)*out)->lpVtbl;
        r_enumerator = *v;
        pc64_patch(&v->GetDefaultAudioEndpoint, GetDefaultAudioEndpoint_);
        pc64_patch(&v->GetDevice, GetDevice_);
    }
    return h;
}

void pc64_sound_install(HMODULE game) {
    r_CoCreateInstance = pc64_import(game, "ole32.dll", "CoCreateInstance", (void *)CoCreateInstance_);
    if (!r_CoCreateInstance) pc64_log("the game doesn't import CoCreateInstance: its sound as it is");
}
