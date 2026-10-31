/* Per-part cycle counts of the moai engine in the simulator: includes the engine source to call its pieces. */
#include "bench_common.h"
#include "moai.c"
int main(void)
{
    static int16_t buf[OP1_BLOCK]; uint32_t ph = 0, ph2 = 0, nz = 1; wave_t wv; uint32_t t;
    lad_t l = { 0, 0, 0, 0, { 0, 0, 0, 0 } }; lad_coef_t lc; int32_t yp = 0, dcx = 0, dcy = 0;
    for (int i = 0; i < OP1_BLOCK; i++) buf[i] = (int16_t)(i * 200 - 12000);
    wave_of(&wv, W_SAW);
    t = cycles(); for (int b = 0; b < 16; b++) osc_pass(buf, &wv, rcp30(25480370), &ph, 25480370, 0, 16384, &nz, 0); t = cycles() - t;
    put("osc saw (store):      "); putn(t / (16 * OP1_BLOCK)); put("\n");
    t = cycles(); for (int b = 0; b < 16; b++) osc_pass(buf, &wv, rcp30(25480370), &ph2, 25500000, 0, 14000, &nz, 1); t = cycles() - t;
    put("osc saw (add):        "); putn(t / (16 * OP1_BLOCK)); put("\n");
    wave_of(&wv, W_SQUARE);
    t = cycles(); for (int b = 0; b < 16; b++) osc_pass(buf, &wv, rcp30(25480370), &ph, 25480370, 0, 16384, &nz, 1); t = cycles() - t;
    put("osc pulse (add):      "); putn(t / (16 * OP1_BLOCK)); put("\n");
    lad_coef(&lc, 3000, 9000);
    t = cycles();
    for (int b = 0; b < 16; b++) for (int n = 0; n < OP1_BLOCK; n++) {
        int32_t x = ADDS(SHL2S(FM_LL(buf[n], 9000)), FM_HL(yp, 6000));
        yp = lad_step(&l, &lc, x, l.t, 1, 0);
        int32_t y = yp - dcx; dcx += y >> 9;
        buf[n] = (int16_t)(SHL3S(FM_HL(sclip(y), 20000)) >> 16);
    }
    t = cycles() - t; put("filter step (+drive, feedback, DC block): "); putn(t / (16 * OP1_BLOCK)); put("\n");
    volatile int32_t vv = 3000; volatile uint32_t vi = 25480370; int32_t acc = 0;
    t = cycles(); for (int b = 0; b < 64; b++) { lad_coef(&lc, vv + b, 9000); acc += lc.invk; } t = cycles() - t;
    put("lad_coef (per call):  "); putn(t / 64); put("\n");
    t = cycles(); for (int b = 0; b < 64; b++) acc += pitch_oct(vi + b); t = cycles() - t;
    put("pitch_oct (per call): "); putn(t / 64); put("\n");
    t = cycles(); for (int b = 0; b < 64; b++) acc += (int32_t)rcp30(vi + b); t = cycles() - t;
    put("rcp30 (per call):     "); putn(t / 64); put("\n");
    t = cycles(); for (int b = 0; b < 64; b++) acc += vidx(host_voice_mem + 36 * (b & 3)) + vel_gain(b & 3); t = cycles() - t;
    put("voice index + vel:    "); putn(t / 64); put("\n");
    t = cycles(); for (int b = 0; b < 64; b++) { wave_of(&wv, (vv + b) & 7); acc += wv.c1; } t = cycles() - t;
    put("wave_of:              "); putn(t / 64); put("\n");
    t = cycles(); for (int b = 0; b < 64; b++) acc += lerp_tab(MM_G, vv + b); t = cycles() - t;
    put("lerp_tab:             "); putn(t / 64); put("\n");
    vv = acc;
    return 0;
}
