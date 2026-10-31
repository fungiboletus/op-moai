/* shared by bench.c and parts.c: host mocks, stack, output and the cycle counter in the simulator */
#include "op1_plugin.h"
int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24] __attribute__((aligned(4))); int16_t host_voice_vel[6];
int16_t host_accel[3]; int16_t host_knobs[8][8]; int host_slot; const uint8_t *host_sample_buffer[8];
int16_t host_fx_knobs[3][8]; int16_t host_fx_meter[3]; void *host_fx_scratch[3];
volatile uint8_t op1_fx_page[3][4]; volatile uint8_t op1_eng_page[4];
void *host_find_node(void *s, const char *id) { (void)s; (void)id; return 0; }
void host_node_set_transform(void *n, const int32_t *m) { (void)n; (void)m; }
void host_node_set_visible(void *n, int on) { (void)n; (void)on; }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }
void *host_raster(void) { return 0; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c) { (void)r; (void)a; (void)b; (void)w; (void)c; }
void *read_fx_param_safe(int slot, int pos) { (void)slot; (void)pos; return 0; }
#ifdef __bfin__
uint32_t bench_stack[8192] __attribute__((aligned(8))); extern uint32_t bench_stack_top[] __asm__("_bench_stack_top");
__asm__(".global _bench_stack_top\n.set _bench_stack_top, _bench_stack + 32760");

static void put(const char *s)                       /* CB_SYS_write: P0 = 5, R0 -> { fd, buf, len } (as libgloss) */
{
    int n = 0; while (s[n]) n++;
    volatile int blk[3] = { 1, (int)s, n };
    register int r0 __asm__("R0") = (int)blk; register int p0 __asm__("P0") = 5;
    __asm__ volatile("EXCPT 0;" : "+d"(r0) : "a"(p0) : "memory", "R1", "R2");
}
static void putn(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); put(b + i); }
static inline uint32_t cycles(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }
#else                                                   /* host build: same program, stdout, no cycle counter */
#include <stdio.h>
static void put(const char *s) { fputs(s, stdout); }
static void putn(uint32_t v) { printf("%u", v); }
static inline uint32_t cycles(void) { return 0; }
#endif

