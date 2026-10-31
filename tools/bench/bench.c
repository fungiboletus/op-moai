/* Cycle count of one synth engine's render() in the Blackfin simulator (bfin-elf-run). Built with the SDK's plugin
 * flags (-O2 -mcpu=bf524) plus -DOP1_HOST, so the engine reads its pitch / velocity from host arrays.
 * The simulator counts one cycle per instruction packet (no pipeline stalls, no SDRAM / cache latency): the numbers
 * are a lower bound to compare engines and versions, not the OP-1's real load. Prints cycles per sample per voice. */
#include "bench_common.h"
extern const op1_engine_t ENGINE;
__attribute__((weak)) void mm_bench_sat(int v) { (void)v; }   /* moai sets its saturation (other engines: nothing) */
__attribute__((weak)) void mm_bench_mod(int v) { (void)v; }   /* ... and its LO modulation */
__attribute__((weak)) int mm_bench_preset(int p, int16_t *k) { (void)p; (void)k; return 0; }   /* ... its presets' words */
static const struct { const char *name; int16_t k[8]; int midi_inc; int sat, mod; } S[] = {
    /* k: setup, cutoff, emphasis, contour knob, selector, detune, drive, drift */
    { "CLASSIC (2 saws)",        { 512, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "FAT BASS (3 saws)",       { 1536, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "OBOE (3 pulses)",         { 16896, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "SHARK (saw + tri mixes)", { 22016, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "BREATH (2 saws + noise)", { 30208, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "ACID (1 saw)",            { 5632, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "FUNK BS (square saw square)", { 3584, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "CLASSIC, no emphasis",    { 512, 15000, 0, 12000, 0x5301, 0, 0, 0 }, 25480370 },
    { "CLASSIC, saturation",     { 512, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370, 20000 },
    { "CLASSIC, steady filter",  { 512, 15000, 9000, 0, 0x5301, 0, 0, 0 }, 25480370 },
    { "CLASSIC, drive 50 %",     { 512, 15000, 9000, 12000, 0x5301, 0, 16384, 0 }, 25480370 },
    { "CLASSIC, drive 75 %, low cut", { 512, 9000, 9000, 0, 0x5301, 0, 24576, 0 }, 25480370 },
    { "high note, open, res",    { 512, 30000, 30000, 0, 0x5301, 0, 0, 0 }, 203842960 },
    { "CLASSIC, osc 3 LO: vibrato + filter", { 512, 15000, 9000, 12000, 0x5301, 0, 0, 0 }, 25480370, 0, 1 },
};

int main(void)
{
    static int16_t out[OP1_BLOCK];
    for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)(i < 4096 ? i * 8 : (8192 - i) * 8);   /* shape is irrelevant */
    for (unsigned s = 0; s < sizeof S / sizeof S[0]; s++) {
        for (int i = 0; i < 128; i++) host_pitch_inc[i] = S[s].midi_inc;
        host_voice_vel[0] = 100 << 8;
        for (int i = 0; i < 8; i++) host_knobs[0][i] = S[s].k[i];        /* the firmware passes the live knob words */
        const int16_t *k = host_knobs[0];
        mm_bench_sat(S[s].sat);
        mm_bench_mod(S[s].mod);
        ENGINE.note_on(host_voice_mem, k);
        for (int b = 0; b < 8; b++) ENGINE.render(host_voice_mem, out, k);       /* past the attack */
        uint32_t t = 0, h = 2166136261u;                      /* FNV-1a over the output: host and Blackfin must agree */
        for (int b = 0; b < 32; b++) {
            uint32_t t0 = cycles();
            ENGINE.render(host_voice_mem, out, k);
            t += cycles() - t0;
            for (int i = 0; i < OP1_BLOCK; i++) h = (h ^ (uint16_t)out[i]) * 16777619u;
        }
        put(S[s].name); put(": "); putn(t / (32 * OP1_BLOCK)); put(" cycles/sample  hash "); putn(h); put("\n");
#ifdef MM_PROF
        { extern uint32_t mm_prof[6]; static const char *st[4] = { "params", "guard+drift+key", "oscillators", "filter+contour" };
          put("    per sample:"); for (int i = 0; i < 4; i++) { put(" "); put(st[i]); put(" "); putn(mm_prof[i] / (40 * OP1_BLOCK)); mm_prof[i] = 0; } put("\n"); }
#endif
    }
    /* every preset (moai): 16 blocks each past the attack; the average, the costliest, one hash over all */
    uint32_t sum = 0, max = 0, h = 2166136261u; int n = 0, worst = 0;
    for (int16_t k[8]; mm_bench_preset(n, k); n++) {
        for (int i = 0; i < 8; i++) host_knobs[0][i] = k[i];
        mm_bench_sat(0); mm_bench_mod(0);
        ENGINE.note_on(host_voice_mem, host_knobs[0]);
        for (int b = 0; b < 8; b++) ENGINE.render(host_voice_mem, out, host_knobs[0]);
        uint32_t t = 0;
        for (int b = 0; b < 16; b++) {
            uint32_t t0 = cycles();
            ENGINE.render(host_voice_mem, out, host_knobs[0]);
            t += cycles() - t0;
            for (int i = 0; i < OP1_BLOCK; i++) h = (h ^ (uint16_t)out[i]) * 16777619u;
        }
        t /= 16 * OP1_BLOCK; sum += t;
        if (t > max) { max = t; worst = n; }
    }
    if (n) { put("presets P1 .. P"); putn(n); put(": average "); putn(sum / n); put(", the costliest P"); putn(worst + 1);
             put(" "); putn(max); put(" cycles/sample  hash "); putn(h); put("\n"); }
    return 0;
}
