/* (build 110b) Each drum struck alone through the chip (Nuked-OPN2, fm.c)
 * and through Q's synthesised drums, at the game's level (the FM bus and the
 * master, Q's trim): its RMS and peak over 300 ms, the time it takes to fall
 * 20 dB, and its energy in four bands (<200 Hz, 200 Hz-2 kHz, 2-6 kHz,
 * 6-12 kHz; a rectangular window from the strike -- a Hann window starting
 * there hides every attack). The second hit of two, a second apart: the
 * chip's DAC holds the last byte of the drum before, so a first hit from
 * silence carries a DC step a song never has. What 110b re-voiced the
 * drums against (docs/audio.md, "The drums re-voiced"). Run from the top:
 *
 *     make drum_hits && ./drum_hits
 */

extern "C" {
#include "../src/audio/fm.h"
#include "../src/audio/drums.h"
#include "../src/audio/mix.h"
#include "../src/audio/q/q_synth.h"
}
#include <cmath>
#include <cstdio>
#include <numbers>   /* (build 113) std::numbers::pi: MinGW has no M_PI */
#include <vector>
#include <complex>
static const int R = 48000;
struct M { double rms, peak, decay_ms, band[4]; };
static M measure(const std::vector<float>& s)       /* mono, from the strike */
{
    M m{};
    double acc = 0.0;
    int n = int(0.3 * R);
    for (int i = 0; i < n; i++) { acc += double(s[i]) * s[i]; m.peak = std::fmax(m.peak, std::fabs(s[i])); }
    m.rms = 10 * std::log10(acc / n + 1e-20);
    m.peak = 20 * std::log10(m.peak + 1e-12);
    int w = R / 200;                                 /* 5 ms windows */
    std::vector<double> e;
    for (int i = 0; i + w <= int(s.size()); i += w) {
        double a = 0.0;
        for (int k = 0; k < w; k++) { a += double(s[i + k]) * s[i + k]; }
        e.push_back(10 * std::log10(a / w + 1e-20));
    }
    int top = 0;
    for (int i = 0; i < int(e.size()); i++) { if (e[i] > e[top]) { top = i; } }
    int j = top;
    while (j < int(e.size()) && e[j] > e[top] - 20.0) { j++; }
    m.decay_ms = (j - top) * 5.0;
    /* four bands: a DFT of the first 300 ms, Hann window, summed per band */
    static const int lo[4] = { 20, 200, 2000, 6000 }, hi[4] = { 200, 2000, 6000, 12000 }, st[4] = { 10, 25, 50, 100 };
    for (int b = 0; b < 4; b++) {
        double band = 0.0;
        for (int f = lo[b]; f < hi[b]; f += st[b]) {
            std::complex<double> z = 0.0;
            for (int i = 0; i < n; i += 2) {
                double h = 1.0;   /* rectangular: a hit starts at the strike and has died by 300 ms */
                z += std::polar(double(s[i]) * h, -2 * std::numbers::pi * f * i / double(R));
            }
            band += std::norm(z) * st[b];
        }
        m.band[b] = 10 * std::log10(band / (double(n) * n) + 1e-20);
    }
    return m;
}
int main()
{
    /* (build 130) by drums.h's names; 110b's report called the kick
     * "short hard (kick?)" */
    const unsigned char drums[] = { DRUM_HIGH_TOM, DRUM_MID_TOM, DRUM_LOW_TOM, DRUM_SNARE, DRUM_KICK, DRUM_OPEN_HAT, DRUM_CLOSED_HAT };
    const float g = MIX_MUSIC_GAIN * MIX_MASTER_GAIN;
    printf("%-20s %-10s %7s %7s %8s %7s %7s %7s %7s\n", "drum", "path", "RMS", "peak", "-20dB ms", "<200", "200-2k", "2-6k", "6-12k");
    for (int d = 0; d < 7; d++) {
        for (int path = 0; path < 2; path++) {
            std::vector<float> st(size_t(R) * 2 * 2, 0.0f), warm(size_t(R) / 2 * 2, 0.0f);
            if (path == 0) {
                Fm f; fm_init(&f, R); fm_load_drums(&f, "assets/music/drums.json");
                fm_write(&f, 0, 0x2B, 0x80); fm_write(&f, 1, 0xB6, 0xC0);
                fm_render(&f, warm.data(), R / 2, g);
                fm_dac_play(&f, drums[d]);          /* settle the DAC's level */
                for (int k = 0; k < 2; k++) { fm_render(&f, warm.data(), R / 2, g); }
                fm_dac_play(&f, drums[d]);
                fm_render(&f, st.data(), R * 2, g);
                fm_shutdown(&f);
            } else {
                QBank* b = q_bank_create(R);
                q_bank_set_drum_mode(b, path == 1 ? Q_DRUMS_SYNTH : Q_DRUMS_SAMPLES);
                q_bank_load_drums(b, "assets/music/drums.json");
                q_bank_write(b, 0, 0x2B, 0x80);
                q_bank_render(b, warm.data(), R / 2, g);
                q_bank_drum(b, drums[d]);
                for (int k = 0; k < 2; k++) { q_bank_render(b, warm.data(), R / 2, g); }
                q_bank_drum(b, drums[d]);
                q_bank_render(b, st.data(), R * 2, g);
                q_bank_destroy(b);
            }
            std::vector<float> mono(size_t(R) * 2);
            for (size_t i = 0; i < mono.size(); i++) { mono[i] = 0.5f * (st[2 * i] + st[2 * i + 1]); }
            M m = measure(mono);
            printf("%-20s %-10s %7.1f %7.1f %8.0f %7.1f %7.1f %7.1f %7.1f\n", path == 0 ? drum_name(drums[d]) : "",
                   path == 0 ? "chip" : "Q synth", m.rms, m.peak, m.decay_ms, m.band[0], m.band[1], m.band[2], m.band[3]);
        }
    }
    return 0;
}
