/* Three held notes for tools/cachetrace.sh, which traces every fetch and data access in the Blackfin simulator.
 * Voices C3, E3 and G3 play preset TRACE_PRESET (default 0) with detune, drive and drift at 10 %, the case heard on
 * the OP-1 (TRACE_DRIVE: another drive). After 8 warm-up callbacks come TRACE_N traced ones, each starting with a call of trace_mark(); every voice
 * renders one block per callback, as on the OP-1. Built as bench.c is (-DOP1_HOST, the SDK's plugin flags). */
#include "bench_common.h"
extern const op1_engine_t ENGINE;
int mm_bench_preset(int p, int16_t *k);
#ifndef TRACE_PRESET
#define TRACE_PRESET 0
#endif
#define TRACE_N 4
__attribute__((noinline)) void trace_mark(int n) { __asm__ volatile("" : : "r"(n) : "memory"); }

int main(void)
{
    static int16_t out[OP1_BLOCK];
    static const int32_t INC[3] = { 25480370, 32103170, 38183730 };    /* C3, E3, G3 (2^32 f / 44100) */
    int16_t K[8];
    mm_bench_preset(TRACE_PRESET, K);
    K[5] = K[6] = K[7] = 3277;                                          /* detune, drive, drift: 10 % */
#ifdef TRACE_DRIVE
    K[6] = TRACE_DRIVE;                                                 /* above 16384: the transistor ladder */
#endif
    for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)(i < 4096 ? i * 8 : (8192 - i) * 8);
    for (int i = 0; i < 8; i++) host_knobs[0][i] = K[i];
    for (int v = 0; v < 3; v++) {
        host_voice_vel[v] = 100 << 8;
        for (int i = 0; i < 128; i++) host_pitch_inc[i] = INC[v];
        ENGINE.note_on(host_voice_mem + v * 0x24, host_knobs[0]);
    }
    for (int c = 0; c < 8 + TRACE_N; c++) {                            /* 8 to warm up, then the traced ones */
        if (c >= 8) trace_mark(c);
        for (int v = 0; v < 3; v++) {
            for (int i = 0; i < 128; i++) host_pitch_inc[i] = INC[v];
            ENGINE.render(host_voice_mem + v * 0x24, out, host_knobs[0]);
        }
    }
    trace_mark(0);
    put("trace done\n");
    return 0;
}
