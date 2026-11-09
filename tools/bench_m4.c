/* Cortex-M4 cycle estimate for the mastering engine: built for the M4F like the firmware, run in
 * an emulator by tools/bench_m4.py. Worst case: the heaviest chain (Clean; only one chain ever
 * runs), every fader full and moving (so every setting is recomputed each chunk), full-scale
 * noise in, and the crush page on and moving too (the filter near the top, recomputed each chunk).
 * BENCH_A picks another chain. */
#include "master.h"

#ifndef BENCH_A
#define BENCH_A MS_CLEAN
#endif

static ms_engine E;
static float L[128], R[128];
static uint32_t s = 1;

void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

static void fill(void)
{
    for (int i = 0; i < 128; i++) {
        s = s * 1664525u + 1013904223u; L[i] = (float)(int32_t)s * 4.0e-10f;
        s = s * 1664525u + 1013904223u; R[i] = (float)(int32_t)s * 4.0e-10f;
    }
}

void bench_init(void)
{
    ms_init(&E, 48000.0f);
    E.cur = BENCH_A;
    ms_set_chain(&E, BENCH_A);
    for (int k = 0; k < 4; k++) ms_set_fader(&E, k, 1.0f);
}

void bench_block(void)   /* one firmware block: 128 frames */
{
    static int t;
    float w = (t++ & 1) ? 0.0f : 0.02f;    /* the faders move a little every block */
    for (int k = 0; k < 4; k++) { ms_set_fader(&E, k, 1.0f - w); ms_set_param2(&E, k, 0.2f + w); }
    for (int k = 0; k < 3; k++) ms_set_crush(&E, k, 0.9f - w);
    ms_set_crush(&E, MS_CR_FILTER, 0.95f - w);
    fill();
    ms_process(&E, L, R, 128);
}
