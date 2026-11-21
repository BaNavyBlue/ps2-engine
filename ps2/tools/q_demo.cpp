/* (build 109) The preliminary test of Cycfi's Q as a synth backend; (build
 * 110) the backend itself moved to src/audio/q/q_synth.cpp, which this now
 * links, and the pass that sets its trim.
 *
 * One song's notes, twice: through the Nuked-OPN2 path the game plays, and
 * re-voiced with Q -- band-limited oscillators, envelopes, a state-variable
 * filter -- side by side as two WAVs.
 *
 *     make q_demo                      # Q_DIR=/path/to/Q if not ~/GIT/Q
 *     ./q_demo                         # the title's theme, 60 s
 *     ./q_demo --song my_home --seconds 90
 *     ./q_demo --solo fm1              # one voice alone, in both files
 *     ./q_demo --drums samples         # the ROM's drums through Q
 *     ./q_demo --instrument 66         # one patch, an arpeggio, both ways
 *     ./q_demo --bench                 # CPU time a voice (docs/audio.md)
 *     ./q_demo --trim-pass             # every song's peaks, both ways
 *
 * The notes are decoded from the register writes (docs/audio.md's first
 * place for a note sink): the sequencer runs unchanged, and each write it
 * makes goes to Nuked-OPN2 (fm.c, as in the game) and to the Q bank.
 *
 * C calls into C++ only through extern "C": the sequencer's sink callbacks
 * and the q_bank_* functions. */

extern "C" {
#include "../src/audio/smps.h"
#include "../src/audio/fm.h"
#include "../src/audio/mix.h"
#include "../src/audio/q/q_synth.h"
#include "../src/game/rom_ids.h"
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int    kRate       = 48000;
constexpr double kNativeRate = FM_MASTER_CLOCK / 144.0;   /* 53267 Hz */

/* The game's mix (src/audio/mix.h, as src/core/audio.c uses it): the FM
 * bus, the master (build 109a: 12 dB up), the soft knee. Both files get
 * the same. */
constexpr float kMusicGain  = MIX_MUSIC_GAIN;
constexpr float kMasterGain = MIX_MASTER_GAIN;

}   /* namespace */

/* ------------------------------------------------------------------------
 * The demo: the sequencer, its writes to both, two WAVs. */

namespace {

struct SongName
{
    u8          id;
    const char* name;
};

constexpr SongName kSongs[] = {
    { MUSIC_PHANTASY,           "phantasy" },
    { MUSIC_RESTORATION,        "restoration" },
    { MUSIC_SILENT_ZONE,        "silent_zone" },
    { MUSIC_PLEASURE,           "pleasure" },
    { MUSIC_EXCITING_TOWN,      "exciting_town" },
    { MUSIC_SECRET_WAYS,        "secret_ways" },
    { MUSIC_ADVANCED,           "advanced" },
    { MUSIC_MYSTERY,            "mystery" },
    { MUSIC_VIOLATION,          "violation" },
    { MUSIC_UNDER,              "under" },
    { MUSIC_STEP_UP,            "step_up" },
    { MUSIC_BRACKY_NEWS,        "bracky_news" },
    { MUSIC_MY_HOME,            "my_home" },
    { MUSIC_PRESSURE,           "pressure" },
    { MUSIC_DREAM,              "dream" },
    { MUSIC_POWER,              "power" },
    { MUSIC_EXCLAIM,            "exclaim" },
    { MUSIC_RISE_OR_FALL,       "rise_or_fall" },
    { MUSIC_THE_PLACE_OF_DEATH, "the_place_of_death" },
    { MUSIC_MOVEMENT,           "movement" },
    { MUSIC_OVER,               "over" },
    { MUSIC_MOVEMENT_VER2,      "movement_ver2" },
    { MUSIC_NEVER_DREAM,        "never_dream" },
};

/* (111) The voicing the banks are made with (--voicing; PS2_Q_VOICE too). */
QVoicing g_voicing = Q_VOICE_PM;
bool     g_voicing_given = false;

QBank* make_bank()
{
    QBank* b = q_bank_create(kRate);
    if (g_voicing_given) {
        q_bank_set_voicing(b, g_voicing);
    }
    return b;
}

const char* song_name(u8 id)
{
    for (auto const& s : kSongs) {
        if (s.id == id) {
            return s.name;
        }
    }
    return nullptr;
}

/* Both backends get every write; --solo drops the key-ons (and drum
 * bytes) of every other channel, in both. */
struct Demo
{
    Fm     fm{};
    bool   fm_ok = false;
    QBank* bank = nullptr;
    int    solo = -1;     /* -1 all, 0-5 FM1-FM6, 6 the drums */
};

}   /* namespace */

extern "C" {

static void demo_fm_write(void* ctx, int part, u8 r, u8 value)
{
    Demo* d = static_cast<Demo*>(ctx);

    if (d->solo >= 0 && part == 0 && r == 0x28 && (value & 0xF0)) {
        int id = value & 7;
        int ch = (id < 3) ? id : id - 1;
        if (ch != d->solo) {
            return;
        }
    }
    if (d->fm_ok) {
        fm_write(&d->fm, part, r, value);
    }
    q_bank_write(d->bank, part, r, value);
}

/* (111) The hooks, to the bank as the game hands them: Q voices tracks
 * (effects on voices of their own); the chip takes the register writes. */
static void demo_track_write(void* ctx, int track, u8 channel, int part, u8 r,
                             u8 value, bool to_chip)
{
    Demo* d = static_cast<Demo*>(ctx);

    if (d->solo >= 0 && part == 0 && r == 0x28 && (value & 0xF0)) {
        int id = value & 7;
        int ch = (id < 3) ? id : id - 1;
        if (ch != d->solo) {
            return;
        }
    }
    q_bank_track_write(d->bank, track, channel, part, r, value, to_chip);
}

static void demo_dac_write(void* ctx, u8 drum)
{
    Demo* d = static_cast<Demo*>(ctx);

    if (d->solo >= 0 && d->solo != 6) {
        return;
    }
    if (d->fm_ok) {
        fm_dac_play(&d->fm, drum);
    }
    q_bank_drum(d->bank, drum);
}

/* The PSG plays nothing in this game (docs/audio.md); neither path has
 * one here. */
static void demo_psg_write(void* ctx, u8 value)
{
    (void)ctx;
    (void)value;
}

}   /* extern "C" */

namespace {

bool write_wav(const std::string& path, std::vector<float> const& buf)
{
    FILE* fh = std::fopen(path.c_str(), "wb");
    if (fh == nullptr) {
        std::fprintf(stderr, "q_demo: cannot write %s\n", path.c_str());
        return false;
    }
    u32 frames = u32(buf.size() / 2);
    u32 data   = frames * 4;
    u8  h[44]  = {};
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
        float c = mix_soft_limit(s * kMasterGain);
        int v = int(std::lround(c * 32767.0f));
        u8 le[2] = { u8(v & 0xFF), u8((v >> 8) & 0xFF) };
        std::fwrite(le, 1, 2, fh);
    }
    std::fclose(fh);
    return true;
}

double rms_db(std::vector<float> const& buf)
{
    double acc = 0.0;
    for (float s : buf) {
        acc += double(s) * double(s);
    }
    double rms = std::sqrt(acc / double(buf.empty() ? 1 : buf.size()));
    return 20.0 * std::log10((rms > 1e-12) ? rms : 1e-12);
}

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

/* --bench: one voice held for ten seconds per instrument, the Q voice and
 * the whole Nuked chip, CPU time per output sample. */
int bench(Fm& bank_src)
{
    struct Case
    {
        int ins;
        const char* what;
    };
    static const Case cases[] = {
        { 64, "title's bass: algorithm 7, three sines and a saw" },
        { 65, "title's melody: algorithm 4, two filtered carriers" },
        { 66, "title's chord: algorithm 4, two filtered carriers" },
        { 68, "title's melody later: algorithm 1, one carrier" },
    };
    const int frames = kRate * 10;
    std::vector<float> buf(size_t(frames) * 2);

    std::printf("instrument  %-50s Q ns/sample  x real time (one core)\n",
                "what");
    for (auto const& c : cases) {
        for (int every = 1; every <= 16; every *= 16) {
            QBank* qb = make_bank();
            const u8* ins = &bank_src.instruments[size_t(c.ins) * FM_INS_BYTES];

            q_bank_set_cutoff_every(qb, every);
            for (int i = 0; i < FM_INS_BYTES; i++) {
                q_bank_write(qb, 0, bank_src.reg_order[i], ins[i]);
            }
            q_bank_write(qb, 0, 0xA4, 0x22);     /* block 4 */
            q_bank_write(qb, 0, 0xA0, 0x69);
            q_bank_write(qb, 0, 0x28, 0xF0);
            auto t0 = Clock::now();
            std::fill(buf.begin(), buf.end(), 0.0f);
            q_bank_render(qb, buf.data(), frames, 1.0f);
            double s = seconds_since(t0);
            q_bank_destroy(qb);
            std::printf("%10d  %-50s %11.1f  %8.0fx   (filter retuned every "
                        "%d sample%s)\n", c.ins, c.what, s * 1e9 / frames,
                        10.0 / s, every, (every == 1) ? "" : "s");
        }
    }

    Fm chip;
    if (fm_init(&chip, kRate)) {
        auto t0 = Clock::now();
        std::fill(buf.begin(), buf.end(), 0.0f);
        fm_render(&chip, buf.data(), frames, 1.0f);
        double s = seconds_since(t0);
        std::printf("Nuked-OPN2, the whole chip (six channels), idle: "
                    "%.1f ns/sample, %.0fx real time\n",
                    s * 1e9 / frames, 10.0 / s);
        fm_shutdown(&chip);
    }
    return 0;
}

/* --instrument N: one instrument from the bank, held notes up an arpeggio
 * (C, E, G, C an octave apart from middle C), through both paths -- the
 * quickest way to hear one patch re-voiced. The writes are SetFMIns's, as
 * fm_demo makes them. */
int play_instrument(Fm& chip, int ins_index, std::string out_q,
                    std::string out_opn2)
{
    if (ins_index < 0 || ins_index >= chip.instrument_count) {
        std::fprintf(stderr, "q_demo: no instrument %d (0-%d)\n", ins_index,
                     chip.instrument_count - 1);
        return 1;
    }
    static const double notes[4] = { 261.63, 329.63, 392.00, 523.25 };
    const int hold = int(1.5 * kRate);
    const int rest = int(0.6 * kRate);
    const size_t total = size_t(4 * (hold + rest));
    std::vector<float> q_out(total * 2, 0.0f);
    std::vector<float> n_out(total * 2, 0.0f);
    QBank* qb = make_bank();
    const u8* ins = &chip.instruments[size_t(ins_index) * FM_INS_BYTES];
    size_t at = 0;

    fm_set_instrument(&chip, 0, ins_index);
    for (int i = 0; i < FM_INS_BYTES; i++) {
        q_bank_write(qb, 0, chip.reg_order[i], ins[i]);
    }
    while (fm_pending_writes(&chip) > 0) {
        std::vector<float> scratch(64 * 2);
        fm_render(&chip, scratch.data(), 64, 0.0f);
    }
    for (double hz : notes) {
        int block = 0;
        double fnum = 0.0;
        for (block = 0; block < 7; block++) {
            fnum = hz * 1048576.0 / (kNativeRate * std::ldexp(1.0, block - 1));
            if (fnum < 2048.0) {
                break;
            }
        }
        while (block > 0 && fnum < 1024.0) {
            block--;
            fnum *= 2.0;
        }
        int f = int(std::lround(fnum));
        u8 a4 = u8((block << 3) | ((f >> 8) & 7));
        u8 a0 = u8(f & 0xFF);
        fm_write(&chip, 0, 0xA4, a4);
        fm_write(&chip, 0, 0xA0, a0);
        fm_write(&chip, 0, 0x28, 0xF0);
        q_bank_write(qb, 0, 0xA4, a4);
        q_bank_write(qb, 0, 0xA0, a0);
        q_bank_write(qb, 0, 0x28, 0xF0);
        fm_render(&chip, &n_out[at * 2], hold, kMusicGain);
        q_bank_render(qb, &q_out[at * 2], hold, kMusicGain);
        at += size_t(hold);
        fm_write(&chip, 0, 0x28, 0x00);
        q_bank_write(qb, 0, 0x28, 0x00);
        fm_render(&chip, &n_out[at * 2], rest, kMusicGain);
        q_bank_render(qb, &q_out[at * 2], rest, kMusicGain);
        at += size_t(rest);
    }
    std::string stem = "instrument_" + std::to_string(ins_index);
    if (out_q.empty()) {
        out_q = stem + "_q.wav";
    }
    if (out_opn2.empty()) {
        out_opn2 = stem + "_opn2.wav";
    }
    q_bank_destroy(qb);
    bool ok = write_wav(out_q, q_out) && write_wav(out_opn2, n_out);
    std::printf("instrument %d: %s, %s\n", ins_index, out_q.c_str(),
                out_opn2.c_str());
    return ok ? 0 : 1;
}

/* One song through both paths: the sequencer ticked once a field, then that
 * field's samples from each backend -- the order the audio thread sees them.
 * `fm` is initialised with the drums loaded; the bank is made here. */
struct SongRun
{
    long   ticks = 0;
    double q_secs = 0.0;
    double n_secs = 0.0;
    double voice_samples = 0.0;
};

bool render_song(Demo& demo, SmpsData* data, u8 song, double seconds,
                 std::vector<float>& q_out, std::vector<float>& n_out,
                 SongRun& run)
{
    /* As audio_init: the DAC on from boot, FM6 the drum channel. */
    fm_write(&demo.fm, 0, 0x2B, 0x80);
    q_bank_write(demo.bank, 0, 0x2B, 0x80);

    static Smps smps;
    SmpsSink sink;
    sink.fm_write  = demo_fm_write;
    sink.dac_write = demo_dac_write;
    sink.psg_write = demo_psg_write;
    sink.ctx       = &demo;
    sink.track_write = demo_track_write;
    smps_init(&smps, data, &sink);
    smps_play(&smps, song);

    const size_t total = size_t(seconds * kRate);
    q_out.assign(total * 2, 0.0f);
    n_out.assign(total * 2, 0.0f);
    double per_tick = double(kRate) / TICK_HZ;
    double owed = 0.0;
    size_t at = 0;

    while (at < total) {
        smps_tick(&smps);
        run.ticks++;
        owed += per_tick;
        int n = int(owed);
        owed -= n;
        if (at + size_t(n) > total) {
            n = int(total - at);
        }
        auto t0 = Clock::now();
        q_bank_render(demo.bank, &q_out[at * 2], n, kMusicGain);
        run.q_secs += seconds_since(t0);
        t0 = Clock::now();
        fm_render(&demo.fm, &n_out[at * 2], n, kMusicGain);
        run.n_secs += seconds_since(t0);
        at += size_t(n);
    }
    run.voice_samples = double(q_bank_voice_samples(demo.bank));
    return true;
}

/* The peak at the game's master, before the soft knee, in dBFS. */
double peak_db(std::vector<float> const& buf)
{
    float m = 0.0f;
    for (float s : buf) {
        m = std::max(m, std::fabs(s * kMasterGain));
    }
    return 20.0 * std::log10((m > 1e-9f) ? double(m) : 1e-9);
}

/* (build 110) --trim-pass: every song through both paths with the bank's
 * trim at 1, the peaks at the game's master; the trim that puts Q's peaks
 * with the chip's -- the median of the songs' differences, so a typical song
 * peaks alike -- and where the loudest song then lands against the knee. */
int trim_pass(std::string const& assets, double seconds, QDrumMode mode)
{
    SmpsData data;
    std::string sound = assets + "/music";
    std::string drums = assets + "/music/drums.json";
    if (!smps_load(&data, sound.c_str())) {
        std::fprintf(stderr, "q_demo: cannot build the sound image from %s\n",
                     sound.c_str());
        return 1;
    }
    std::vector<double> diffs;
    double chip_max = -200.0;
    double q_max_raw = -200.0;

    std::printf("%-20s %10s %10s %8s %10s %10s\n", "song", "chip peak",
                "Q peak", "Q-chip", "chip RMS", "Q RMS");
    for (auto const& s : kSongs) {
        Demo demo;
        std::vector<float> q_out;
        std::vector<float> n_out;
        SongRun run;

        demo.fm_ok = fm_init(&demo.fm, kRate);
        if (!demo.fm_ok) {
            std::fprintf(stderr, "q_demo: no Nuked-OPN2\n");
            return 1;
        }
        fm_load_drums(&demo.fm, drums.c_str());
        demo.bank = make_bank();
        q_bank_set_drum_mode(demo.bank, mode);
        q_bank_load_drums(demo.bank, drums.c_str());
        q_bank_set_trim(demo.bank, 1.0f);
        render_song(demo, &data, s.id, seconds, q_out, n_out, run);
        double pc = peak_db(n_out);
        double pq = peak_db(q_out);
        std::printf("%-20s %10.1f %10.1f %+8.1f %10.1f %10.1f\n", s.name, pc,
                    pq, pq - pc, rms_db(n_out) + 20.0 * std::log10(kMasterGain),
                    rms_db(q_out) + 20.0 * std::log10(kMasterGain));
        diffs.push_back(pq - pc);
        chip_max = std::max(chip_max, pc);
        q_max_raw = std::max(q_max_raw, pq);
        q_bank_destroy(demo.bank);
        fm_shutdown(&demo.fm);
    }
    std::sort(diffs.begin(), diffs.end());
    double median = diffs[diffs.size() / 2];
    double trim = std::pow(10.0, -median / 20.0);
    std::printf("median Q-chip %+.2f dB: trim %.3f (%+.2f dB)\n", median, trim,
                -median);
    std::printf("loudest song's peak: chip %.1f dBFS, Q at that trim %.1f "
                "dBFS (the knee at -3.0)\n", chip_max, q_max_raw - median);
    smps_free(&data);
    return 0;
}

int usage(const char* argv0)
{
    std::fprintf(stderr,
        "usage: %s [--song NAME|ID] [--seconds N] [--solo fm1..fm6|drums]\n"
        "          [--instrument N] [--q FILE] [--opn2 FILE] [--assets DIR]\n"
        "          [--drums synth|samples] [--voicing pm|subtractive]\n"
        "          [--bench] [--trim-pass]\n",
        argv0);
    return 1;
}

}   /* namespace */

int main(int argc, char** argv)
{
    std::string assets = "assets";
    std::string out_q;
    std::string out_opn2;
    u8  song = MUSIC_PHANTASY;
    double seconds = 60.0;
    int solo = -1;
    int instrument = -1;
    bool do_bench = false;
    bool do_trim = false;
    QDrumMode drum_mode = Q_DRUMS_SYNTH;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        bool more = i + 1 < argc;

        if (a == "--song" && more) {
            std::string v = argv[++i];
            bool found = false;
            for (auto const& s : kSongs) {
                if (v == s.name) {
                    song = s.id;
                    found = true;
                }
            }
            if (!found) {
                song = u8(std::strtol(v.c_str(), nullptr, 16));
            }
        } else if (a == "--seconds" && more) {
            seconds = std::atof(argv[++i]);
        } else if (a == "--solo" && more) {
            std::string v = argv[++i];
            if (v == "drums") {
                solo = 6;
            } else if (v.size() == 3 && v.compare(0, 2, "fm") == 0
                       && v[2] >= '1' && v[2] <= '6') {
                solo = v[2] - '1';
            } else {
                return usage(argv[0]);
            }
        } else if (a == "--q" && more) {
            out_q = argv[++i];
        } else if (a == "--opn2" && more) {
            out_opn2 = argv[++i];
        } else if (a == "--assets" && more) {
            assets = argv[++i];
        } else if (a == "--instrument" && more) {
            instrument = std::atoi(argv[++i]);
        } else if (a == "--bench") {
            do_bench = true;
        } else if (a == "--trim-pass") {
            do_trim = true;
        } else if (a == "--voicing" && more) {
            std::string v = argv[++i];
            g_voicing_given = true;
            if (v == "subtractive") {
                g_voicing = Q_VOICE_SUBTRACTIVE;
            } else if (v == "pm") {
                g_voicing = Q_VOICE_PM;
            } else {
                return usage(argv[0]);
            }
        } else if (a == "--drums" && more) {
            std::string v = argv[++i];
            if (v == "samples") {
                drum_mode = Q_DRUMS_SAMPLES;
            } else if (v == "synth") {
                drum_mode = Q_DRUMS_SYNTH;
            } else {
                return usage(argv[0]);
            }
        } else {
            return usage(argv[0]);
        }
    }

    if (do_trim) {
        return trim_pass(assets, seconds, drum_mode);
    }

    Demo demo;
    demo.fm_ok = fm_init(&demo.fm, kRate);
    if (!demo.fm_ok) {
        std::fprintf(stderr, "q_demo: no Nuked-OPN2 (src/audio/ym3438.c)\n");
        return 1;
    }
    std::string insts = assets + "/music/fm_instruments.json";
    if (do_bench || instrument >= 0) {
        if (!fm_load_instruments(&demo.fm, insts.c_str())) {
            return 1;
        }
        int rc = do_bench ? bench(demo.fm)
                          : play_instrument(demo.fm, instrument, out_q, out_opn2);
        fm_shutdown(&demo.fm);
        return rc;
    }

    const char* name = song_name(song);
    if (name == nullptr) {
        std::fprintf(stderr, "q_demo: $%02X is not a song\n", song);
        return 1;
    }
    std::string stem = name;
    if (solo >= 0) {
        stem += (solo == 6) ? std::string("_drums")
                            : "_fm" + std::to_string(solo + 1);
    }
    if (out_q.empty()) {
        out_q = stem + ((drum_mode == Q_DRUMS_SAMPLES) ? "_q_samples.wav"
                                                       : "_q.wav");
    }
    if (out_opn2.empty()) {
        out_opn2 = stem + "_opn2.wav";
    }

    SmpsData data;
    std::string sound = assets + "/music";
    std::string drums = assets + "/music/drums.json";
    if (!smps_load(&data, sound.c_str())) {
        std::fprintf(stderr, "q_demo: cannot build the sound image from %s\n",
                     sound.c_str());
        return 1;
    }
    if (!fm_load_drums(&demo.fm, drums.c_str())) {
        std::fprintf(stderr, "q_demo: no drums in %s\n", drums.c_str());
    }
    demo.bank = make_bank();
    demo.solo = solo;
    q_bank_set_drum_mode(demo.bank, drum_mode);
    q_bank_load_drums(demo.bank, drums.c_str());

    std::vector<float> q_out;
    std::vector<float> n_out;
    SongRun run;
    render_song(demo, &data, song, seconds, q_out, n_out, run);
    const size_t total = q_out.size() / 2;
    long ticks = run.ticks;
    double q_secs = run.q_secs;
    double n_secs = run.n_secs;

    bool ok = write_wav(out_q, q_out) && write_wav(out_opn2, n_out);
    double vs = run.voice_samples;
    std::printf("%s ($%02X), %.1f s, %ld ticks%s\n", name, song, seconds,
                ticks, (solo >= 0) ? ", solo" : "");
    std::printf("  %-26s RMS %6.1f dB\n", out_q.c_str(), rms_db(q_out));
    std::printf("  %-26s RMS %6.1f dB\n", out_opn2.c_str(), rms_db(n_out));
    std::printf("  Q: %.0f ns/sample, %.2f FM voices sounding on average, "
                "%.0f ns a voice-sample (drums included in the total)\n",
                q_secs * 1e9 / double(total), vs / double(total),
                (vs > 0.0) ? q_secs * 1e9 / vs : 0.0);
    std::printf("  Nuked-OPN2: %.0f ns/sample (the chip, drums included)\n",
                n_secs * 1e9 / double(total));

    q_bank_destroy(demo.bank);
    smps_free(&data);
    fm_shutdown(&demo.fm);
    return ok ? 0 : 1;
}
