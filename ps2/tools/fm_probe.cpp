/* (build 111) One sound through the chip (Nuked-OPN2, fm.c) and through Q,
 * measured side by side: the pitch over time, the partials at a few moments,
 * the loudness envelope, and how alike the two spectra are over time. What
 * docs/audio.md's "Three sounds described" and "The PM voicing" measured.
 *
 *     make fm_probe && ./fm_probe --note 72 --hz 311 --hold 3
 *     ./fm_probe --song step_up --solo fm1 --from 3.3 --seconds 4
 *     ./fm_probe --sfx C1 --seconds 0.8
 *     ./fm_probe --drum 80
 *     ./fm_probe --click 72 --hz 311       # a strike under an audible tail
 *     ./fm_probe --alias 74 --hz 1245      # a high note, bright: images
 *     ./fm_probe --steal B5 --over 92 --chip both   # an effect over a song:
 *                              # the music's loss and the effect's cut
 *
 * Every case is rendered through the chip, then through Q in each voicing
 * asked for (--voicing pm|subtractive|both; both by default), at the game's
 * level (the FM bus and the master; mix.h). --wav STEM writes each file for
 * listening (STEM_chip.wav, STEM_q_pm.wav, ...). The numbers:
 *
 *   pitch     the fundamental a 10 ms step (normalised autocorrelation over
 *             40 ms, octave errors refused by the note's own register pitch
 *             where it is known), its median and its swing in cents
 *   partials  at the moments --at gives (default 0.05, 0.3, 1 s after the
 *             strike): the strongest spectral peaks, frequency and level,
 *             each as a multiple of the fundamental
 *   envelope  the correlation of the two 10 ms loudness envelopes in dB,
 *             where either is within 50 dB of its peak
 *   spectrum  frame by frame (43 ms, 10 ms hop), third-octave bands 50 Hz-
 *             16 kHz: the mean difference in dB over the bands within 50 dB
 *             of either file's loudest band -- 0 is the same spectrum
 *
 * Measurements start at the strike with a flat (rectangular) window where a
 * window meets it, or with 20 ms of pre-roll: a window rising from zero at
 * the strike hides the attack (build 110b's mistake). */

extern "C" {
#include "../src/audio/smps.h"
#include "../src/audio/fm.h"
#include "../src/audio/mix.h"
#include "../src/audio/q/q_synth.h"
#include "../src/audio/patches.h"
#include "../src/audio/synth.h"
#include "../src/game/rom_ids.h"
}

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>   /* (build 129b) SIZE_MAX */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>   /* (build 113) std::numbers::pi: MinGW has no M_PI */
#include <string>
#include <vector>

namespace {

constexpr int    kRate       = 48000;
constexpr double kNativeRate = FM_MASTER_CLOCK / 144.0;
constexpr float  kBusGain    = MIX_MUSIC_GAIN;

using Buf = std::vector<float>;          /* stereo, interleaved */

/* ------------------------------------------------------------------------
 * The paths. */

enum class Path {
    Chip,           /* the Mega Drive's one chip: VOICES LIMITED */
    ChipExtra,      /* (build 129b) VOICES EXTRA: the effects on a second chip */
    QPm,
    QSub,
};

const char* path_name(Path p)
{
    switch (p) {
    case Path::Chip:
        return "chip";
    case Path::ChipExtra:
        return "chip_extra";
    case Path::QPm:
        return "q_pm";
    case Path::QSub:
        return "q_subtractive";
    }
    return "?";
}

/* (build 112) --patches FILE: Q reads the patch file (a path the tool opens
 * itself: ./assets/music/q_patches.json, not assets/..., which is the game's
 * preloaded store). Empty: the ROM's instruments. */
std::string g_patch_file;

/* (build 112) --music-only: Q renders its effects' voices at 0 (the AUDIO
 * page's EFFECTS OFF), so what the music keeps under an effect can be heard
 * and measured apart from the effect -- VOICES EXTRA against LIMITED.
 * (build 129b) The chip too: it is the game's chip backend now, which knows
 * each channel's owner from the hooks. */
bool g_music_only = false;

/* (build 129b) The two buses' levels a render is taken at, as the AUDIO
 * page's MUSIC and EFFECTS would set them: --steal renders the together
 * three times -- both, the music alone (effects 0), the effect alone (music
 * 0) -- to tell what the music loses from what the effect loses. */
struct Mix
{
    float music   = 1.0f;
    float effects = 1.0f;
};

/* One synth, either kind. (build 129b) The chip is the game's own chip
 * backend (synth.c, the table audio.c drives): the same fm.c underneath,
 * written and rendered exactly as before under VOICES LIMITED, but it now
 * takes the sequencer's hooks as the game does -- which channel an effect
 * holds, for the music/effects split -- and under VOICES EXTRA plays the
 * effects on its second chip. */
struct Synth
{
    Path                path;
    Mix                 mix;
    SynthBackend const* be   = nullptr;
    void*               chip = nullptr;
    QBank*              q    = nullptr;

    Synth(Path p, std::string const& assets, Mix m = Mix{}) : path(p), mix(m)
    {
        std::string drums = assets + "/music/drums.json";
        if (p == Path::Chip || p == Path::ChipExtra) {
            be   = synth_backend(SYNTH_CHIP);
            chip = be->create(kRate);
            be->set_option(chip, SYNTH_OPT_VOICES, (p == Path::ChipExtra) ? 0 : 1);
            be->load_drums(chip, drums.c_str());
        } else {
            q = q_bank_create(kRate);
            q_bank_set_voicing(q, (p == Path::QPm) ? Q_VOICE_PM
                                                   : Q_VOICE_SUBTRACTIVE);
            q_bank_load_drums(q, drums.c_str());
            if (!g_patch_file.empty()) {
                SmpsData d{};
                std::string music = assets + "/music";
                static SynthPatch patches[SMPS_INSTRUMENTS];
                if (smps_load(&d, music.c_str())) {
                    int n = patches_load(g_patch_file.c_str(), &d, patches,
                                         SMPS_INSTRUMENTS);
                    q_bank_set_patches(q, patches, n);
                    smps_free(&d);
                }
            }
        }
        write(0, 0x2B, 0x80);
    }
    ~Synth()
    {
        if (q != nullptr) {
            q_bank_destroy(q);
        } else {
            be->shutdown(chip);
        }
    }
    Synth(Synth const&) = delete;
    Synth& operator=(Synth const&) = delete;

    void write(int part, u8 reg, u8 v)
    {
        if (q != nullptr) {
            q_bank_write(q, part, reg, v);
        } else {
            be->write(chip, part, reg, v);
        }
    }
    void track(int t, u8 channel, int part, u8 reg, u8 v, bool to_chip)
    {
        if (q != nullptr) {
            q_bank_track_write(q, t, channel, part, reg, v, to_chip);
        } else {
            be->track_write(chip, t, channel, part, reg, v, to_chip);
        }
    }
    void drum(u8 b)
    {
        if (q != nullptr) {
            q_bank_drum(q, b);
        } else {
            be->drum(chip, b);
        }
    }
    void render(float* out, int frames)
    {
        float music   = kBusGain * mix.music;
        float effects = g_music_only ? 0.0f : kBusGain * mix.effects;
        if (q != nullptr) {
            q_bank_render_split(q, out, frames, music, effects);
        } else {
            be->render(chip, out, frames, music, effects);
        }
    }
};

/* The game's bank, for --note: SetFMIns's 26 writes, taken from the
 * sequencer's own image through FMInsPtrs (the root pointer at $06), so an
 * instrument number here is the one the songs use. (So is the place of
 * each in assets/music/fm_instruments.json, build 136d, read the same way;
 * before build 111 the extractor of the tables/FMInstruments.bin it replaced
 * stopped at the next label, so two instruments whose last bytes run under a
 * label came out short and were dropped, and every number from 71 on was one
 * off.) */
struct Bank
{
    static constexpr u8 kOrder[FM_INS_BYTES] = {
        0xB0, 0xB4, 0x40, 0x48, 0x44, 0x4C, 0x30, 0x38, 0x34, 0x3C,
        0x50, 0x58, 0x54, 0x5C, 0x60, 0x68, 0x64, 0x6C, 0x70, 0x78,
        0x74, 0x7C, 0x80, 0x88, 0x84, 0x8C
    };
    SmpsData const* data = nullptr;

    u32 ptr(u32 at) const
    {
        return u32(data->image[at]) | (u32(data->image[at + 1]) << 8);
    }
    const u8* instrument(int n) const
    {
        return data->image + ptr(ptr(0x06) + u32(n) * 2);
    }
    void upload(Synth& s, int n, int ch) const
    {
        int part = (ch >= 3) ? 1 : 0;
        int slot = ch % 3;
        const u8* ins = instrument(n);
        for (int i = 0; i < FM_INS_BYTES; i++) {
            s.write(part, u8(kOrder[i] + slot), ins[i]);
        }
    }
};

/* A frequency word for a pitch in Hz, block chosen as the driver's table
 * does (F-number in 620-1233 where it can). */
u16 freq_word(double hz)
{
    int block = 0;
    double fnum = 0.0;
    for (block = 0; block < 7; block++) {
        fnum = hz * 1048576.0 / (kNativeRate * std::ldexp(1.0, block - 1));
        if (fnum < 1234.0) {
            break;
        }
    }
    int f = int(std::lround(fnum));
    return u16((block << 11) | (f & 0x7FF));
}

double word_hz(u16 w)
{
    return double(w & 0x7FF) * std::ldexp(1.0, ((w >> 11) & 7) - 1)
         * kNativeRate / 1048576.0;
}

void render_frames(Synth& s, Buf& out, size_t& at, int frames)
{
    if (at + size_t(frames) > out.size() / 2) {
        out.resize((at + size_t(frames)) * 2, 0.0f);
    }
    s.render(&out[at * 2], frames);
    at += size_t(frames);
}

/* Let the chip's write queue empty before timing starts. */
void settle(Synth& s)
{
    Buf scratch(size_t(2 * 256), 0.0f);
    if (s.q == nullptr) {
        while (synth_chip_pending_writes(s.chip) > 0) {
            s.render(scratch.data(), 64);
        }
    } else {
        while (q_bank_pending_writes(s.q) > 0) {
            s.render(scratch.data(), 64);
        }
    }
}

/* ------------------------------------------------------------------------
 * Analysis. */

std::vector<float> mono(Buf const& st, size_t from, size_t n)
{
    std::vector<float> m(n, 0.0f);
    for (size_t i = 0; i < n && from + i < st.size() / 2; i++) {
        m[i] = 0.5f * (st[2 * (from + i)] + st[2 * (from + i) + 1]) * MIX_MASTER_GAIN;
    }
    return m;
}

void fft(std::vector<std::complex<double>>& a)
{
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * std::numbers::pi / double(len);
        std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; k++) {
                auto u = a[i + k];
                auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/* Power spectrum of n samples from `at`: Hann, or flat (rectangular) when
 * the frame starts at a strike. */
std::vector<double> spectrum(std::vector<float> const& m, size_t at, size_t n,
                             bool flat)
{
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; i++) {
        double w = flat ? 1.0 : 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * double(i) / double(n));
        double x = (at + i < m.size()) ? double(m[at + i]) : 0.0;
        a[i] = w * x;
    }
    fft(a);
    std::vector<double> p(n / 2);
    for (size_t k = 0; k < n / 2; k++) {
        p[k] = std::norm(a[k]) / (double(n) * double(n) / 4.0);
    }
    return p;
}

double db(double p)
{
    return 10.0 * std::log10(p + 1e-20);
}

/* The fundamental at a frame: normalised autocorrelation over 40 ms, lags
 * for 40 Hz to 2 kHz, the first peak within 90% of the best. `hint` (Hz, or
 * 0) refuses octave errors: of the candidates, the one nearest it. */
double pitch_at(std::vector<float> const& m, size_t at, double hint)
{
    const size_t n = size_t(0.040 * kRate);
    const int lo = kRate / 2000;
    const int hi = kRate / 40;
    if (at + n + size_t(hi) >= m.size()) {
        return 0.0;
    }
    double e0 = 0.0;
    for (size_t i = 0; i < n; i++) {
        e0 += double(m[at + i]) * m[at + i];
    }
    if (e0 < 1e-12) {
        return 0.0;
    }
    std::vector<double> r(size_t(hi + 2), 0.0);
    for (int lag = lo; lag <= hi + 1; lag++) {
        double c = 0.0;
        double e1 = 0.0;
        for (size_t i = 0; i < n; i++) {
            c  += double(m[at + i]) * m[at + i + size_t(lag)];
            e1 += double(m[at + i + size_t(lag)]) * m[at + i + size_t(lag)];
        }
        r[size_t(lag)] = c / std::sqrt(e0 * e1 + 1e-30);
    }
    double best = 0.0;
    for (int lag = lo; lag <= hi; lag++) {
        best = std::max(best, r[size_t(lag)]);
    }
    if (best < 0.5) {
        return 0.0;
    }
    double pick = 0.0;
    double pick_err = 1e9;
    for (int lag = lo + 1; lag <= hi; lag++) {
        double v = r[size_t(lag)];
        if (v < 0.9 * best || v < r[size_t(lag - 1)] || v < r[size_t(lag + 1)]) {
            continue;
        }
        /* parabolic refinement */
        double a = r[size_t(lag - 1)];
        double b = v;
        double c = r[size_t(lag + 1)];
        double d = 0.5 * (a - c) / (a - 2.0 * b + c + 1e-30);
        double f = double(kRate) / (double(lag) + d);
        if (hint <= 0.0) {
            return f;
        }
        double err = std::fabs(std::log2(f / hint));
        if (err < pick_err) {
            pick_err = err;
            pick = f;
        }
    }
    return pick;
}

struct PitchStats
{
    double median_hz = 0.0;
    double lo_cents = 0.0;     /* the swing about the median */
    double hi_cents = 0.0;
    int    frames = 0;
    std::vector<double> track; /* Hz a 10 ms step, 0 unknown */
};

PitchStats pitch_track(std::vector<float> const& m, double hint)
{
    PitchStats s;
    for (size_t at = 0; at + size_t(0.1 * kRate) < m.size(); at += kRate / 100) {
        s.track.push_back(pitch_at(m, at, hint));
    }
    std::vector<double> v;
    for (double f : s.track) {
        if (f > 0.0) {
            v.push_back(f);
        }
    }
    s.frames = int(v.size());
    if (v.empty()) {
        return s;
    }
    std::vector<double> sorted = v;
    std::sort(sorted.begin(), sorted.end());
    s.median_hz = sorted[sorted.size() / 2];
    /* the 5th and 95th percentiles, so one bad frame does not set it */
    double p5  = sorted[sorted.size() * 5 / 100];
    double p95 = sorted[std::min(sorted.size() - 1, sorted.size() * 95 / 100)];
    s.lo_cents = 1200.0 * std::log2(p5 / s.median_hz);
    s.hi_cents = 1200.0 * std::log2(p95 / s.median_hz);
    return s;
}

/* The strongest peaks at a moment: a 4096-point spectrum from `at` (flat
 * when at the strike), local maxima within 50 dB of the top. */
struct Peak
{
    double hz;
    double level_db;
};

std::vector<Peak> peaks_at(std::vector<float> const& m, size_t at, int count,
                           bool flat)
{
    const size_t n = 4096;
    auto p = spectrum(m, at, n, flat);
    double top = 0.0;
    for (double v : p) {
        top = std::max(top, v);
    }
    std::vector<Peak> out;
    for (size_t k = 2; k + 2 < p.size(); k++) {
        if (p[k] > p[k - 1] && p[k] >= p[k + 1] && p[k] > p[k - 2]
            && p[k] >= p[k + 2] && db(p[k]) > db(top) - 50.0) {
            double a = db(p[k - 1]);
            double b = db(p[k]);
            double c = db(p[k + 1]);
            double d = 0.5 * (a - c) / (a - 2.0 * b + c + 1e-30);
            out.push_back(Peak{ (double(k) + d) * kRate / double(n), b });
        }
    }
    std::sort(out.begin(), out.end(), [](Peak const& x, Peak const& y) {
        return x.level_db > y.level_db;
    });
    if (int(out.size()) > count) {
        out.resize(size_t(count));
    }
    std::sort(out.begin(), out.end(), [](Peak const& x, Peak const& y) {
        return x.hz < y.hz;
    });
    return out;
}

/* Third-octave bands, 50 Hz to 16 kHz. */
std::vector<double> bands(std::vector<double> const& p, size_t n)
{
    std::vector<double> b;
    for (double c = 50.0; c <= 16000.0; c *= std::pow(2.0, 1.0 / 3.0)) {
        double lo = c * std::pow(2.0, -1.0 / 6.0);
        double hi = c * std::pow(2.0, 1.0 / 6.0);
        double acc = 0.0;
        for (size_t k = size_t(lo * double(n) / kRate); k <= size_t(hi * double(n) / kRate) && k < p.size(); k++) {
            acc += p[k];
        }
        b.push_back(db(acc));
    }
    return b;
}

struct Compare
{
    double env_corr = 0.0;
    double spec_db = 0.0;       /* mean over frames */
    double spec_db_early = 0.0; /* the first 300 ms */
    double rms_db[2] = { 0.0, 0.0 };
};

double rms_db(std::vector<float> const& m)
{
    double acc = 0.0;
    for (float x : m) {
        acc += double(x) * x;
    }
    return db(acc / double(m.empty() ? 1 : m.size()));
}

Compare compare(std::vector<float> const& a, std::vector<float> const& b)
{
    Compare c;
    c.rms_db[0] = rms_db(a);
    c.rms_db[1] = rms_db(b);
    size_t n = std::min(a.size(), b.size());
    const size_t w = kRate / 100;
    std::vector<double> ea;
    std::vector<double> eb;
    for (size_t at = 0; at + w <= n; at += w) {
        double x = 0.0;
        double y = 0.0;
        for (size_t i = 0; i < w; i++) {
            x += double(a[at + i]) * a[at + i];
            y += double(b[at + i]) * b[at + i];
        }
        ea.push_back(db(x / double(w)));
        eb.push_back(db(y / double(w)));
    }
    double pa = *std::max_element(ea.begin(), ea.end());
    double pb = *std::max_element(eb.begin(), eb.end());
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    int k = 0;
    for (size_t i = 0; i < ea.size(); i++) {
        if (ea[i] < pa - 50.0 && eb[i] < pb - 50.0) {
            continue;
        }
        double x = std::max(ea[i], pa - 60.0);
        double y = std::max(eb[i], pb - 60.0);
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; k++;
    }
    if (k > 2) {
        double cov = sxy / k - (sx / k) * (sy / k);
        double va  = sxx / k - (sx / k) * (sx / k);
        double vb  = syy / k - (sy / k) * (sy / k);
        c.env_corr = cov / std::sqrt(va * vb + 1e-30);
    }
    /* spectra over time */
    const size_t fn = 2048;
    double sum = 0.0;
    double sum_e = 0.0;
    int frames = 0;
    int frames_e = 0;
    for (size_t at = 0; at + fn <= n; at += kRate / 100) {
        bool first = (at == 0);
        auto ba = bands(spectrum(a, at, fn, first), fn);
        auto bb = bands(spectrum(b, at, fn, first), fn);
        double ta = *std::max_element(ba.begin(), ba.end());
        double tb = *std::max_element(bb.begin(), bb.end());
        if (ta < pa - 50.0 && tb < pb - 50.0) {
            continue;
        }
        /* each spectrum by its own loudest band: shape, not level */
        double d = 0.0;
        int nb = 0;
        for (size_t i = 0; i < ba.size(); i++) {
            double x = ba[i] - ta;
            double y = bb[i] - tb;
            if (x < -50.0 && y < -50.0) {
                continue;
            }
            d += std::fabs(std::max(x, -50.0) - std::max(y, -50.0));
            nb++;
        }
        if (nb > 0) {
            sum += d / nb;
            frames++;
            if (at < size_t(0.3 * kRate)) {
                sum_e += d / nb;
                frames_e++;
            }
        }
    }
    c.spec_db = frames ? sum / frames : 0.0;
    c.spec_db_early = frames_e ? sum_e / frames_e : 0.0;
    return c;
}

/* ------------------------------------------------------------------------
 * The cases. Each renders one Buf per path, the strike at `strike` frames. */

struct Case
{
    std::string title;
    double hint_hz = 0.0;       /* the register pitch, when there is one */
    size_t strike = 0;          /* frames */
};

bool write_wav(std::string const& path, Buf const& buf)
{
    FILE* fh = std::fopen(path.c_str(), "wb");
    if (fh == nullptr) {
        return false;
    }
    u32 frames = u32(buf.size() / 2);
    u32 data   = frames * 4;
    u8 h[44] = {};
    auto le32 = [&h](int at, u32 v) {
        for (int i = 0; i < 4; i++) {
            h[at + i] = u8(v >> (8 * i));
        }
    };
    std::memcpy(h, "RIFF", 4);
    le32(4, 36 + data);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    le32(16, 16);
    h[20] = 1;
    h[22] = 2;
    le32(24, kRate);
    le32(28, kRate * 4);
    h[32] = 4;
    h[34] = 16;
    std::memcpy(h + 36, "data", 4);
    le32(40, data);
    std::fwrite(h, 1, sizeof h, fh);
    for (float s : buf) {
        float c = mix_soft_limit(s * MIX_MASTER_GAIN);
        int v = int(std::lround(c * 32767.0f));
        u8 le[2] = { u8(v & 0xFF), u8((v >> 8) & 0xFF) };
        std::fwrite(le, 1, 2, fh);
    }
    std::fclose(fh);
    return true;
}

/* --note: one instrument on FM1, one note held, released. */
Buf render_note(Path p, std::string const& assets, Bank const& bank, int ins,
                double hz, double hold, double tail, size_t& strike)
{
    Synth s(p, assets);
    Buf out;
    size_t at = 0;
    bank.upload(s, ins, 0);
    u16 w = freq_word(hz);
    s.write(0, 0xA4, u8(w >> 8));
    s.write(0, 0xA0, u8(w));
    settle(s);
    render_frames(s, out, at, kRate / 50);            /* pre-roll */
    strike = at;
    s.write(0, 0x28, 0xF0);
    render_frames(s, out, at, int(hold * kRate));
    s.write(0, 0x28, 0x00);
    render_frames(s, out, at, int(tail * kRate));
    return out;
}

/* --click: the note struck, keyed off at `hold` as the driver's early
 * release does, struck again 50 ms later under its tail -- the second strike
 * is the one measured. */
Buf render_click(Path p, std::string const& assets, Bank const& bank, int ins,
                 double hz, double hold, size_t& strike, bool first = true)
{
    Synth s(p, assets);
    Buf out;
    size_t at = 0;
    bank.upload(s, ins, 0);
    u16 w = freq_word(hz);
    s.write(0, 0xA4, u8(w >> 8));
    s.write(0, 0xA0, u8(w));
    settle(s);
    render_frames(s, out, at, kRate / 50);
    if (first) {
        s.write(0, 0x28, 0xF0);
    }
    render_frames(s, out, at, int(hold * kRate));
    s.write(0, 0x28, 0x00);
    render_frames(s, out, at, int(0.050 * kRate));
    /* the driver's strike: off, then on, in one tick */
    s.write(0, 0x28, 0x00);
    s.write(0, 0x28, 0xF0);
    strike = at;
    render_frames(s, out, at, int(1.0 * kRate));
    return out;
}

/* --song / --sfx: the sequencer, its writes to one synth. */
struct SeqCtx
{
    Synth* s;
    int    solo;    /* -1 all, 0-5 FM1-FM6, 6 drums */
};

extern "C" void seq_fm(void* ctx, int part, u8 r, u8 v)
{
    SeqCtx* c = static_cast<SeqCtx*>(ctx);
    if (c->solo >= 0 && part == 0 && r == 0x28 && (v & 0xF0)) {
        int id = v & 7;
        int ch = (id < 3) ? id : id - 1;
        if (ch != c->solo) {
            return;
        }
    }
    c->s->write(part, r, v);
}

/* (111) The hooks: Q voices tracks from them, as in the game. (build 129b)
 * The chip takes them too, as the game's chip backend does: under LIMITED
 * which channel an effect holds, under EXTRA every channel's writes.
 * --solo drops the others' key-ons here too. */
extern "C" void seq_track(void* ctx, int track, u8 channel, int part, u8 r,
                          u8 v, bool to_chip)
{
    SeqCtx* c = static_cast<SeqCtx*>(ctx);
    if (c->solo >= 0 && part == 0 && r == 0x28 && (v & 0xF0)) {
        int id = v & 7;
        int ch = (id < 3) ? id : id - 1;
        if (ch != c->solo) {
            return;
        }
    }
    c->s->track(track, channel, part, r, v, to_chip);
}

extern "C" void seq_dac(void* ctx, u8 d)
{
    SeqCtx* c = static_cast<SeqCtx*>(ctx);
    if (c->solo >= 0 && c->solo != 6) {
        return;
    }
    c->s->drum(d);
}

extern "C" void seq_psg(void* ctx, u8 v)
{
    (void)ctx;
    (void)v;
}

/* (111) --sfx-list: effects at times of their own over the song (the
 * listening files' battle). */
std::vector<std::pair<long, u8>> g_sfx_list;

Buf render_sequence(Path p, std::string const& assets, SmpsData* data,
                    u8 music, u8 sfx, int solo, double from, double seconds,
                    size_t& strike, Mix mix = Mix{})
{
    Synth s(p, assets, mix);
    SeqCtx c{ &s, solo };
    static Smps smps;
    SmpsSink sink;
    sink.fm_write  = seq_fm;
    sink.dac_write = seq_dac;
    sink.psg_write = seq_psg;
    sink.ctx       = &c;
    sink.track_write = seq_track;
    smps_init(&smps, data, &sink);
    if (music != 0) {
        smps_play(&smps, music);
    }
    Buf out;
    size_t at = 0;
    double owed = 0.0;
    long tick = 0;
    long sfx_tick = (sfx != 0) ? long(from * TICK_HZ) : -1;
    double end = from + seconds;
    strike = 0;
    while (double(at) / kRate < end + 0.05) {
        if (tick == sfx_tick) {
            smps_play(&smps, sfx);
            strike = at;
        }
        for (auto const& e : g_sfx_list) {
            if (e.first == tick) {
                smps_play(&smps, e.second);
            }
        }
        smps_tick(&smps);
        tick++;
        owed += double(kRate) / TICK_HZ;
        int n = int(owed);
        owed -= n;
        render_frames(s, out, at, n);
    }
    if (sfx == 0) {
        strike = size_t(from * kRate);
    }
    return out;
}

Buf render_drum(Path p, std::string const& assets, u8 drum, size_t& strike)
{
    Synth s(p, assets);
    Buf out;
    size_t at = 0;
    s.write(1, 0xB6, 0xC0);
    render_frames(s, out, at, kRate / 2);
    s.drum(drum);                        /* settle the DAC's level */
    render_frames(s, out, at, kRate);
    strike = at;
    s.drum(drum);
    render_frames(s, out, at, kRate / 2);
    return out;
}

/* ------------------------------------------------------------------------ */

struct Options
{
    std::string assets = "assets";
    std::string wav;
    std::vector<Path> paths{ Path::Chip, Path::QPm, Path::QSub };
    std::vector<double> at{ 0.05, 0.3, 1.0 };
    int    peaks = 10;
    bool   track = false;          /* print the pitch track */
};

void report(Case const& c, Options const& o, std::vector<Buf> const& bufs,
            std::vector<size_t> const& strikes, double seconds)
{
    std::printf("== %s\n", c.title.c_str());
    std::vector<std::vector<float>> m;
    for (size_t i = 0; i < bufs.size(); i++) {
        m.push_back(mono(bufs[i], strikes[i], size_t(seconds * kRate)));
    }
    PitchStats ref;
    for (size_t i = 0; i < bufs.size(); i++) {
        PitchStats ps = pitch_track(m[i], c.hint_hz);
        if (i == 0) {
            ref = ps;
        }
        std::printf("  %-14s RMS %6.1f dB  pitch %8.2f Hz (%+6.1f c against "
                    "the register's), swing %+6.1f..%+6.1f cents over %d frames\n",
                    path_name(o.paths[i]), rms_db(m[i]), ps.median_hz,
                    (c.hint_hz > 0 && ps.median_hz > 0)
                        ? 1200.0 * std::log2(ps.median_hz / c.hint_hz) : 0.0,
                    ps.lo_cents, ps.hi_cents, ps.frames);
        if (o.track) {
            std::printf("    track (Hz, 10 ms):");
            for (size_t k = 0; k < ps.track.size(); k += 2) {
                std::printf(" %.1f", ps.track[k]);
            }
            std::printf("\n");
        }
        if (i > 0) {
            Compare cm = compare(m[0], m[i]);
            int agree = 0;
            int both = 0;
            for (size_t k = 0; k < std::min(ref.track.size(), ps.track.size()); k++) {
                if (ref.track[k] > 0 && ps.track[k] > 0) {
                    both++;
                    if (std::fabs(1200.0 * std::log2(ps.track[k] / ref.track[k])) < 25.0) {
                        agree++;
                    }
                }
            }
            std::printf("  %-14s against the chip: envelope %.2f, spectrum %.1f dB "
                        "(first 300 ms %.1f), pitch within 25 c %d of %d frames\n",
                        "", cm.env_corr, cm.spec_db, cm.spec_db_early, agree, both);
        }
    }
    for (double t : o.at) {
        if (t >= seconds) {
            continue;
        }
        std::printf("  partials at %.2f s (Hz dB, xf0 against the chip's pitch):\n", t);
        for (size_t i = 0; i < bufs.size(); i++) {
            auto pk = peaks_at(m[i], size_t(t * kRate), o.peaks, t < 0.06);
            std::printf("    %-12s", path_name(o.paths[i]));
            for (auto const& p : pk) {
                double f0 = (ref.median_hz > 0) ? ref.median_hz : 1.0;
                std::printf(" %.0f %.0f (%.2f)", p.hz, p.level_db, p.hz / f0);
            }
            std::printf("\n");
        }
    }
    if (!o.wav.empty()) {
        for (size_t i = 0; i < bufs.size(); i++) {
            write_wav(o.wav + "_" + path_name(o.paths[i]) + ".wav", bufs[i]);
        }
    }
}

/* The click at a strike under a tail. Each path is rendered twice, the
 * second strike with the first note's tail under it and without it, and the
 * one taken from the other: what is left is the tail -- continuous where it
 * rings on untouched beside the new note, broken where the strike zeroes its
 * phases and restarts its envelope as it sounds. Reported: the tail's level
 * over the 5 ms before the strike, its own sample-to-sample steps there
 * (RMS), and the largest step of what is left in the first half
 * millisecond from the strike, against them. */
void click_report(Options const& o, std::vector<Buf> const& with,
                  std::vector<Buf> const& without,
                  std::vector<size_t> const& strikes)
{
    std::printf("  the second strike, under the first's tail (what the tail "
                "adds, the strike alone taken away):\n");
    for (size_t i = 0; i < with.size(); i++) {
        const size_t w = size_t(0.005 * kRate);
        auto a = mono(with[i], strikes[i] - w, 2 * w);
        auto b = mono(without[i], strikes[i] - w, 2 * w);
        std::vector<double> m(a.size());
        for (size_t k = 0; k < m.size(); k++) {
            m[k] = double(a[k]) - double(b[k]);
        }
        double lev = 0.0;
        double steps = 0.0;
        for (size_t k = 1; k < w; k++) {
            lev += m[k] * m[k];
            steps += (m[k] - m[k - 1]) * (m[k] - m[k - 1]);
        }
        lev = db(lev / double(w));
        steps = std::sqrt(steps / double(w - 1));
        double jump = 0.0;
        for (size_t k = w; k < w + size_t(0.0005 * kRate); k++) {
            jump = std::max(jump, std::fabs(m[k] - m[k - 1]));
        }
        std::printf("    %-14s the tail %6.1f dB; its steps %.5f, at the strike "
                    "%.5f (%+5.1f dB)\n", path_name(o.paths[i]), lev, steps,
                    jump, 20.0 * std::log10((jump + 1e-12) / (steps + 1e-12)));
    }
}

/* (111) --alias: every partial FM makes of a held note lies on a grid of
 * half the register's pitch (multiples 0.5-15, the detune a fraction of a
 * hertz off it). What lies off that grid -- more than 40 Hz from every line
 * of it -- is what folded back past the rate, the images: its share of the
 * whole, over the first half second, 8192-point frames. */
void alias_report(Options const& o, std::vector<Buf> const& bufs,
                  std::vector<size_t> const& strikes, double grid_hz)
{
    std::printf("  off the grid of %.2f Hz (folded-back images), share of the "
                "energy:\n", grid_hz);
    for (size_t i = 0; i < bufs.size(); i++) {
        auto m = mono(bufs[i], strikes[i], size_t(0.5 * kRate) + 8192);
        const size_t n = 8192;
        double on = 0.0;
        double off = 0.0;
        for (size_t at = 0; at + n <= m.size(); at += n / 2) {
            auto p = spectrum(m, at, n, false);
            for (size_t k = 1; k < p.size(); k++) {
                double f = double(k) * kRate / double(n);
                double line = std::round(f / grid_hz) * grid_hz;
                if (std::fabs(f - line) > 40.0) {
                    off += p[k];
                } else {
                    on += p[k];
                }
            }
        }
        std::printf("    %-14s %6.1f dB\n", path_name(o.paths[i]),
                    db(off / (on + off + 1e-30)));
    }
}

/* (111) --steal: an effect over a song, three ways a path -- together, the
 * song alone, the effect alone. (111b) By energy, 20 ms at a time, over the
 * effect's time: the together against the song's energy plus the effect's --
 * where the effect takes a music channel (the chip) the song's notes there
 * drop out and the together falls short; where it has voices of its own (Q)
 * nothing is missing. Energy, not the waveform taken away: since 111b Q keeps
 * the chip's pace, so an effect's writes delay the song's that follow by a
 * slot or two, as on the chip, and a waveform difference would count that.
 *
 * (build 129b) That one number mixes two losses: notes the music drops and
 * whatever of the effect is cut. The together is rendered twice more, split
 * as the AUDIO page's MUSIC and EFFECTS split it -- its music with the
 * effects at 0, its effect with the music at 0 -- so each is measured
 * against itself alone:
 *
 *   music's loss  the together's music against the song alone, over the
 *                 20 ms windows where the song is within 60 dB of its
 *                 loudest: mean and worst in dB
 *   effect's cut  the together's effect against the effect alone: where
 *                 each ends (its last 20 ms within 40 dB of the effect
 *                 alone's loudest) -- a tail the music takes back ends
 *                 sooner; the energy it keeps, all of it against all of
 *                 the effect alone's; and, while it still plays in the
 *                 mix, the mean and worst 20 ms against the effect alone
 *                 (whether it sounds as it does alone while it lasts)
 *   parts         the two parts' sum against the together, sample by
 *                 sample: the split is only a split if they add back up
 *
 * A per-window mean is no measure of a cut tail: past the cut every window
 * reads -60 dB or worse. Hence the end and the energy for the cut, and the
 * windows before it for the rest. */
struct Energies
{
    std::vector<double> e;
    double peak = 0.0;
};

Energies energies(std::vector<float> const& m)
{
    const size_t w = size_t(0.020 * kRate);
    Energies r;
    for (size_t at = 0; at + w <= m.size(); at += w) {
        double sum = 0.0;
        for (size_t k = at; k < at + w; k++) {
            sum += double(m[k]) * m[k];
        }
        r.e.push_back(sum);
        r.peak = std::max(r.peak, sum);
    }
    return r;
}

struct Loss
{
    double mean  = 0.0;
    double worst = 0.0;
    int    frames = 0;
};

Loss loss(Energies const& got, Energies const& ref, size_t limit = SIZE_MAX)
{
    Loss l;
    double sum = 0.0;
    for (size_t i = 0; i < ref.e.size() && i < got.e.size() && i < limit; i++) {
        if (db(ref.e[i]) < db(ref.peak) - 60.0) {
            continue;
        }
        double d = db(got.e[i]) - db(ref.e[i]);
        sum += d;
        l.worst = std::min(l.worst, d);
        l.frames++;
    }
    l.mean = l.frames ? sum / l.frames : 0.0;
    return l;
}

/* How many 20 ms windows from the strike to the last within 40 dB of
 * `peak`: the end, in windows. */
size_t end_windows(Energies const& e, double peak)
{
    size_t last = 0;
    for (size_t i = 0; i < e.e.size(); i++) {
        if (db(e.e[i]) >= db(peak) - 40.0) {
            last = i + 1;
        }
    }
    return last;
}

/* The first sample within 60 dB of `peak` (an amplitude): where a sound
 * starts, in samples from the strike. */
size_t start_sample(std::vector<float> const& m, double peak)
{
    double floor = peak * 1e-3;
    for (size_t k = 0; k < m.size(); k++) {
        if (std::fabs(m[k]) >= floor) {
            return k;
        }
    }
    return m.size();
}

double total(Energies const& e)
{
    double sum = 0.0;
    for (double v : e.e) {
        sum += v;
    }
    return sum;
}

void steal_report(Options const& o, std::vector<Buf> const& with,
                  std::vector<Buf> const& song, std::vector<Buf> const& alone,
                  std::vector<Buf> const& with_music,
                  std::vector<Buf> const& with_effect,
                  size_t strike, double seconds)
{
    std::printf("  what the song loses under the effect (20 ms energy: the "
                "together against song + effect, the mean shortfall):\n");
    for (size_t i = 0; i < with.size(); i++) {
        size_t n = size_t(seconds * kRate);
        auto a = mono(with[i], strike, n);
        auto b = mono(song[i], strike, n);
        auto c = mono(alone[i], strike, n);
        const size_t w = size_t(0.020 * kRate);
        double sum = 0.0;
        double worst = 0.0;
        int frames = 0;
        for (size_t at = 0; at + w <= n; at += w) {
            double ea = 0.0;
            double eb = 0.0;
            double ec = 0.0;
            for (size_t k = at; k < at + w; k++) {
                ea += double(a[k]) * a[k];
                eb += double(b[k]) * b[k];
                ec += double(c[k]) * c[k];
            }
            double d = db(ea) - db(eb + ec);
            sum += d;
            worst = std::min(worst, d);
            frames++;
        }
        std::printf("    %-14s %+5.1f dB (the worst 20 ms %+5.1f)\n",
                    path_name(o.paths[i]), frames ? sum / frames : 0.0, worst);
    }

    std::printf("  (129b) the split: the music's loss, 20 ms energy mean / "
                "worst; the effect's cut: where it ends in the mix / alone, "
                "the energy it keeps, and while it plays 20 ms mean / worst; "
                "the parts' sum against the together:\n");
    for (size_t i = 0; i < with.size(); i++) {
        size_t n = size_t(seconds * kRate);
        auto a  = mono(with[i], strike, n);
        auto s  = mono(song[i], strike, n);
        auto x  = mono(alone[i], strike, n);
        auto wm = mono(with_music[i], strike, n);
        auto we = mono(with_effect[i], strike, n);

        Energies es  = energies(s);
        Energies ex  = energies(x);
        Energies ewm = energies(wm);
        Energies ewe = energies(we);
        Loss   music  = loss(ewm, es);
        size_t end_in = end_windows(ewe, ex.peak);
        size_t end_x  = end_windows(ex, ex.peak);
        Loss   effect = loss(ewe, ex, end_in);
        double keeps  = db(total(ewe)) - db(total(ex));
        double xpeak  = 0.0;
        for (float v : x) {
            xpeak = std::max(xpeak, double(std::fabs(v)));
        }
        double late_ms = (double(start_sample(we, xpeak))
                          - double(start_sample(x, xpeak))) * 1000.0 / kRate;

        double diff = 0.0;
        double top  = 0.0;
        for (size_t k = 0; k < n; k++) {
            diff = std::max(diff, double(std::fabs(wm[k] + we[k] - a[k])));
            top  = std::max(top, double(std::fabs(a[k])));
        }
        std::printf("    %-14s music %+5.1f / %+5.1f dB   effect starts %+5.1f "
                    "ms, ends %4zu / %4zu ms, keeps %+5.1f dB, while it plays "
                    "%+5.1f / %+5.1f dB   parts %s\n",
                    path_name(o.paths[i]), music.mean, music.worst, late_ms,
                    end_in * 20, end_x * 20, keeps, effect.mean, effect.worst,
                    (diff <= top * 1e-4) ? "add up" : "DO NOT add up");
        if (diff > top * 1e-4) {
            std::printf("      the parts' sum is off by %.1f dB of the "
                        "together's peak\n", 20.0 * std::log10(diff / (top + 1e-30)));
        }
    }
}

int usage()
{
    std::fprintf(stderr,
        "usage: fm_probe CASE [options]\n"
        "  CASE: --note INS [--hz F] [--hold S] [--tail S]\n"
        "        --click INS [--hz F] [--hold S]\n"
        "        --alias INS [--hz F]      (the images a held note folds back)\n"
        "        --song NAME|ID [--solo fm1..fm6|drums] [--from S] [--seconds S]\n"
        "        --sfx ID [--over SONG] [--seconds S]\n"
        "        --steal ID --over SONG [--from S] [--seconds S]\n"
        "        (with --song) --sfx-list ID@S,ID@S,...  effects at times\n"
        "        --drum BYTE\n"
        "  options: --voicing pm|subtractive|both|none  --at T,T,...  --wav STEM\n"
        "           --chip limited|extra|both  (129b: the chip's VOICES; limited,\n"
        "                            the one chip, by default)\n"
        "           --peaks N  --track  --assets DIR\n"
        "           --patches FILE  (112: Q reads the patch file, e.g.\n"
        "                            ./assets/music/q_patches.json)\n"
        "           --music-only    (112: the effects at 0, the music alone;\n"
        "                            129b: the chip's too)\n");
    return 1;
}

u8 song_id(std::string const& v)
{
    static const struct { const char* n; u8 id; } names[] = {
        { "phantasy", MUSIC_PHANTASY }, { "secret_ways", MUSIC_SECRET_WAYS },
        { "step_up", MUSIC_STEP_UP }, { "my_home", MUSIC_MY_HOME },
        { "restoration", MUSIC_RESTORATION }, { "power", MUSIC_POWER },
        { "never_dream", MUSIC_NEVER_DREAM }, { "pressure", MUSIC_PRESSURE },
        { "bracky_news", MUSIC_BRACKY_NEWS }, { "advanced", MUSIC_ADVANCED },
    };
    for (auto const& n : names) {
        if (v == n.n) {
            return n.id;
        }
    }
    return u8(std::strtol(v.c_str(), nullptr, 16));
}

}   /* namespace */

int main(int argc, char** argv)
{
    Options o;
    std::string kind;
    int ins = -1;
    double hz = 311.13;
    double hold = 2.0;
    double tail = 0.5;
    u8 music = 0;
    u8 sfx = 0;
    u8 drum = 0;
    int solo = -1;
    double from = 0.0;
    double seconds = 2.0;
    bool seconds_given = false;
    bool alias = false;
    std::string chip_voices = "limited";

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        bool more = i + 1 < argc;
        if ((a == "--note" || a == "--click" || a == "--alias") && more) {
            kind = a.substr(2);
            ins = std::atoi(argv[++i]);
        } else if (a == "--song" && more) {
            kind = "song";
            music = song_id(argv[++i]);
        } else if (a == "--sfx-list" && more) {
            std::string v = argv[++i];
            for (size_t p = 0; p < v.size();) {
                u8 id = u8(std::strtol(v.c_str() + p, nullptr, 16));
                size_t atp = v.find('@', p);
                double t = std::atof(v.c_str() + atp + 1);
                g_sfx_list.emplace_back(long(t * TICK_HZ), id);
                size_t c = v.find(',', p);
                p = (c == std::string::npos) ? v.size() : c + 1;
            }
        } else if (a == "--steal" && more) {
            kind = "steal";
            sfx = u8(std::strtol(argv[++i], nullptr, 16));
        } else if (a == "--sfx" && more) {
            kind = "sfx";
            sfx = u8(std::strtol(argv[++i], nullptr, 16));
        } else if (a == "--over" && more) {
            music = song_id(argv[++i]);
        } else if (a == "--drum" && more) {
            kind = "drum";
            drum = u8(std::strtol(argv[++i], nullptr, 16));
        } else if (a == "--hz" && more) {
            hz = std::atof(argv[++i]);
        } else if (a == "--hold" && more) {
            hold = std::atof(argv[++i]);
        } else if (a == "--tail" && more) {
            tail = std::atof(argv[++i]);
        } else if (a == "--solo" && more) {
            std::string v = argv[++i];
            solo = (v == "drums") ? 6 : v[2] - '1';
        } else if (a == "--from" && more) {
            from = std::atof(argv[++i]);
        } else if (a == "--seconds" && more) {
            seconds = std::atof(argv[++i]);
            seconds_given = true;
        } else if (a == "--voicing" && more) {
            std::string v = argv[++i];
            o.paths = { Path::Chip };
            if (v == "pm" || v == "both") {
                o.paths.push_back(Path::QPm);
            }
            if (v == "subtractive" || v == "both") {
                o.paths.push_back(Path::QSub);
            }
        } else if (a == "--at" && more) {
            o.at.clear();
            std::string v = argv[++i];
            for (size_t p = 0; p < v.size();) {
                o.at.push_back(std::atof(v.c_str() + p));
                size_t c = v.find(',', p);
                p = (c == std::string::npos) ? v.size() : c + 1;
            }
        } else if (a == "--wav" && more) {
            o.wav = argv[++i];
        } else if (a == "--peaks" && more) {
            o.peaks = std::atoi(argv[++i]);
        } else if (a == "--track") {
            o.track = true;
        } else if (a == "--assets" && more) {
            o.assets = argv[++i];
        } else if (a == "--patches" && more) {
            g_patch_file = argv[++i];
        } else if (a == "--music-only") {
            g_music_only = true;
        } else if (a == "--chip" && more) {
            chip_voices = argv[++i];
            if (chip_voices != "limited" && chip_voices != "extra"
                && chip_voices != "both") {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (kind.empty()) {
        return usage();
    }
    /* (build 129b) --chip: the one chip (LIMITED), the second chip for the
     * effects (EXTRA), or both, in place of the list's first entry. */
    if (!o.paths.empty() && o.paths.front() == Path::Chip) {
        o.paths.erase(o.paths.begin());
        if (chip_voices == "extra") {
            o.paths.insert(o.paths.begin(), Path::ChipExtra);
        } else if (chip_voices == "both") {
            o.paths.insert(o.paths.begin(), { Path::Chip, Path::ChipExtra });
        } else {
            o.paths.insert(o.paths.begin(), Path::Chip);
        }
    }

    Bank bank;
    SmpsData data;
    std::string music_dir = o.assets + "/music";
    if (!smps_load(&data, music_dir.c_str())) {
        std::fprintf(stderr, "fm_probe: cannot load the music\n");
        return 1;
    }
    bank.data = &data;

    Case c;
    std::vector<Buf> bufs;
    std::vector<size_t> strikes;
    char title[200];

    if (kind == "alias") {
        kind = "note";
        o.at.clear();
        seconds_given = true;
        seconds = 0.6;
        hold = std::max(hold, 0.7);
        alias = true;
    }
    if (kind == "note" || kind == "click") {
        u16 w = freq_word(hz);
        c.hint_hz = word_hz(w);
        std::snprintf(title, sizeof title, "instrument %d, %s at %.2f Hz "
                      "(word $%04X)%s", ins, (kind == "note") ? "held" : "struck twice",
                      c.hint_hz, w, (kind == "note") ? "" : ", the second under the first's tail");
        if (!seconds_given) {
            seconds = (kind == "note") ? hold + tail : 1.0;
        }
    } else if (kind == "song") {
        std::snprintf(title, sizeof title, "song $%02X%s, %.2f s from %.2f s", music,
                      (solo >= 0) ? (solo == 6 ? ", the drums alone" : ", one voice alone") : "",
                      seconds, from);
    } else if (kind == "sfx" || kind == "steal") {
        std::snprintf(title, sizeof title, "sound effect $%02X%s", sfx,
                      music ? " over a song" : " alone");
        from = (kind == "steal") ? std::max(from, 1.0) : 0.2;
    } else {
        std::snprintf(title, sizeof title, "drum $%02X", drum);
        if (!seconds_given) {
            seconds = 0.4;
        }
    }
    c.title = title;

    for (Path p : o.paths) {
        size_t strike = 0;
        if (kind == "note") {
            bufs.push_back(render_note(p, o.assets, bank, ins, hz, hold, tail, strike));
        } else if (kind == "click") {
            bufs.push_back(render_click(p, o.assets, bank, ins, hz, hold, strike));
        } else if (kind == "song" || kind == "sfx" || kind == "steal") {
            bufs.push_back(render_sequence(p, o.assets, &data, music, sfx, solo, from,
                                           seconds, strike));
        } else {
            bufs.push_back(render_drum(p, o.assets, drum, strike));
        }
        strikes.push_back(strike);
    }
    report(c, o, bufs, strikes, seconds);
    if (kind == "click") {
        std::vector<Buf> alone;
        for (Path p : o.paths) {
            size_t strike = 0;
            alone.push_back(render_click(p, o.assets, bank, ins, hz, hold, strike,
                                         false));
        }
        click_report(o, bufs, alone, strikes);
    }
    if (alias) {
        alias_report(o, bufs, strikes, c.hint_hz / 2.0);
    }
    if (kind == "steal") {
        std::vector<Buf> song;
        std::vector<Buf> alone;
        std::vector<Buf> with_music;
        std::vector<Buf> with_effect;
        for (Path p : o.paths) {
            size_t st = 0;
            song.push_back(render_sequence(p, o.assets, &data, music, 0, -1, from,
                                           seconds, st));
            alone.push_back(render_sequence(p, o.assets, &data, 0, sfx, -1, from,
                                            seconds, st));
            with_music.push_back(render_sequence(p, o.assets, &data, music, sfx,
                                                 -1, from, seconds, st,
                                                 Mix{ 1.0f, 0.0f }));
            with_effect.push_back(render_sequence(p, o.assets, &data, music, sfx,
                                                  -1, from, seconds, st,
                                                  Mix{ 0.0f, 1.0f }));
        }
        steal_report(o, bufs, song, alone, with_music, with_effect, strikes[0],
                     seconds);
    }
    smps_free(&data);
    return 0;
}
