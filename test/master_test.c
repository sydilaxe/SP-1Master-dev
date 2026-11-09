/* Host test for the SP-1 Master engine: renders WAVs into build/test and checks the behaviour.
 *   master_test [OUTDIR]                          run the checks and write the WAVs
 *   master_test OUTDIR IN.wav CHAIN F1 F2 F3 F4 [P1 P2 P3 P4] [OUT.wav] [C1 C2 C3 C4]
 *                                                 also run a 48 kHz stereo 16/24-bit WAV through
 *                                                 chain 1-4 at those fader positions (0..1), page 2
 *                                                 at P1..P4 (default 0.5), into OUTDIR/yours.wav
 *                                                 (or OUT.wav), the crush page at C1..C4 (default
 *                                                 0, .5, .5, .5); with OUTDIR "-", skip the checks */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../master/master.h"

#define SR 48000
#define CRUSH_HALF 0.015f   /* AMOUNT's dead zone is 0.03: this lands the mix at a half */
static ms_engine E;
static int fails;

static void check(int ok, const char *what, double v)
{
    printf("%-58s %10.4f  %s\n", what, v, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

/* ---- WAV ---- */
static void put32(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

static void wav_write(const char *dir, const char *name, const float *l, const float *r, int n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); exit(1); }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n * 6); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SR); put32(f, SR * 6); put16(f, 6); put16(f, 24);
    fwrite("data", 1, 4, f); put32(f, n * 6);
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 2; c++) {
            double x = (c ? r : l)[i];
            x = x > 1 ? 1 : x < -1 ? -1 : x;
            int v = (int)lrint(x * 8388607.0);
            fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f);
        }
    fclose(f);
}

static int wav_read(const char *path, float **l, float **r)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return -1; }
    int ch = 0, bits = 0, rate = 0, n = -1;
    unsigned char c[8];
    while (fread(c, 1, 8, f) == 8) {
        unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fm[40];
            if (fread(fm, 1, sz < 40 ? sz : 40, f) < 16) break;
            if (sz > 40) fseek(f, sz - 40, SEEK_CUR);
            ch = fm[2] | fm[3] << 8; rate = fm[4] | fm[5] << 8 | fm[6] << 16; bits = fm[14] | fm[15] << 8;
        } else if (!memcmp(c, "data", 4)) {
            if ((bits != 16 && bits != 24) || ch < 1 || ch > 2) break;
            if (rate != SR) fprintf(stderr, "warning: %d Hz, played as 48 kHz\n", rate);
            int bps = bits / 8;
            n = (int)(sz / (unsigned)(bps * ch));
            *l = malloc(sizeof(float) * n); *r = malloc(sizeof(float) * n);
            for (int i = 0; i < n; i++)
                for (int k = 0; k < ch; k++) {
                    unsigned char s[3] = {0};
                    if (fread(s, 1, bps, f) != (size_t)bps) { n = i; goto done; }
                    int v = bits == 16 ? (int16_t)(s[0] | s[1] << 8) * 256 : ((int32_t)((unsigned)s[0] << 8 | (unsigned)s[1] << 16 | (unsigned)s[2] << 24)) >> 8;
                    float x = (float)v / 8388608.0f;
                    if (k == 0) { (*l)[i] = x; if (ch == 1) (*r)[i] = x; }
                    else (*r)[i] = x;
                }
            break;
        } else fseek(f, sz + (sz & 1), SEEK_CUR);
    }
done:
    fclose(f);
    return n;
}

/* ---- the test signal: a one-drop reggae bar, 75 BPM ---- */
static double nz(unsigned *s) { *s = *s * 1664525u + 1013904223u; return (double)(int)*s / 2147483648.0; }

static void make_loop(float *l, float *r, int n)
{
    const double beat = 0.8, bar = 4 * beat;
    const double bass[8] = { 55.0, 55.0, 65.41, 73.42, 55.0, 49.0, 55.0, 82.41 }; /* A1 C2 D2 G1 E2 */
    unsigned s = 1;
    double bp1 = 0, bp2 = 0, hh = 0;   /* skank band-pass, hat high-pass */
    double ph[3] = {0}, bph = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR, tb = fmod(t, bar), tq = fmod(t, beat);
        int q = (int)(tb / beat);
        /* bass: eighth notes, round */
        int e8 = (int)(fmod(t, 2 * bar) / (beat / 2)) % 16;
        double f = bass[e8 / 2], te = fmod(t, beat / 2);
        bph += 2 * M_PI * f / SR;
        double benv = (1 - exp(-te / 0.004)) * exp(-te / 0.35);
        double b = (sin(bph) + 0.3 * sin(2 * bph)) * benv * 0.45;
        /* one drop: kick and rim together on beat 3 */
        double k = 0, rim = 0;
        if (q == 2) {
            double kp = 2 * M_PI * (50 * tq + 60 * 0.03 * (1 - exp(-tq / 0.03)));
            k = sin(kp) * exp(-tq / 0.18) * 0.7;
            rim = nz(&s) * exp(-tq / 0.012) * 0.35 + sin(2 * M_PI * 1700 * tq) * exp(-tq / 0.02) * 0.2;
        }
        /* skank: an Am chop on the off-beats (2 and 4) */
        double sk = 0;
        if (q == 1 || q == 3) {
            const double fr[3] = { 220.0, 261.63, 329.63 };
            for (int j = 0; j < 3; j++) { ph[j] += fr[j] / SR; sk += 2 * fmod(ph[j], 1.0) - 1; }
            sk *= exp(-tq / 0.06) * 0.18;
        }
        bp1 += 0.25 * (sk - bp1); bp2 += 0.25 * (bp1 - bp2);
        sk = bp1 - bp2 * 0.6;
        /* hats: eighths */
        double th = fmod(t, beat / 2), w = nz(&s);
        hh = w - hh * 0.0 ; double hat = (w - 0.7 * hh) * exp(-th / 0.03) * 0.08;
        double mono = b + k + rim;
        l[i] = (float)(mono + sk * 1.1 + hat * 0.8);
        r[i] = (float)(mono + sk * 0.9 + hat * 1.2);
    }
    float pk = 0;
    for (int i = 0; i < n; i++) { if (fabsf(l[i]) > pk) pk = fabsf(l[i]); if (fabsf(r[i]) > pk) pk = fabsf(r[i]); }
    for (int i = 0; i < n; i++) { l[i] *= 0.5f / pk; r[i] *= 0.5f / pk; }
}

/* ---- measuring ---- */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } bq;
static void bq_set(bq *q, int hp, double hz)
{
    double w = 2 * M_PI * hz / SR, c = cos(w), al = sin(w) / (2 * 0.7071), a0 = 1 + al;
    q->b1 = (hp ? -(1 + c) : (1 - c)) / a0; q->b0 = q->b2 = (hp ? (1 + c) / 2 : (1 - c) / 2) / a0;
    q->a1 = -2 * c / a0; q->a2 = (1 - al) / a0; q->z1 = q->z2 = 0;
}
static double bq_run(bq *q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y;
    return y;
}
static double band_db(const float *x, int from, int n, double lo, double hi)
{
    bq a, b, c, d; bq_set(&a, 1, lo); bq_set(&b, 1, lo); bq_set(&c, 0, hi); bq_set(&d, 0, hi);
    double e = 0;
    for (int i = 0; i < n; i++) {
        double y = bq_run(&d, bq_run(&c, bq_run(&b, bq_run(&a, x[i]))));
        if (i >= from) e += y * y;
    }
    return 10 * log10(e / (n - from) + 1e-30);
}
static double rms_db(const float *x, int from, int n)
{
    double e = 0; for (int i = from; i < n; i++) e += (double)x[i] * (double)x[i];
    return 10 * log10(e / (n - from) + 1e-30);
}
static double peak(const float *l, const float *r, int n)
{
    double p = 0;
    for (int i = 0; i < n; i++) { if (fabs(l[i]) > p) p = fabs(l[i]); if (fabs(r[i]) > p) p = fabs(r[i]); }
    return p;
}
static int finite_all(const float *l, const float *r, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(l[i]) || !isfinite(r[i])) return 0;
    return 1;
}
/* clicks: a 256-frame window whose largest second difference stands out from both neighbours.
 * A loud, bright, distorted sound has big steps everywhere; a click has them in one place. */
static double click_ratio(const float *x, int n)
{
    int nw = n / 256;
    double *m = calloc(nw, sizeof *m), worst = 0;
    for (int w = 0; w < nw; w++)
        for (int i = w * 256 + (w ? 0 : 2); i < w * 256 + 256; i++) {
            double d = fabs(x[i] - 2.0 * x[i - 1] + x[i - 2]);
            if (d > m[w]) m[w] = d;
        }
    for (int w = 2; w < nw - 1; w++) {   /* from the second window: the tone's onset is a step */
        double nb = fmax(fmax(m[w - 1], m[w + 1]), 1e-4);
        if (m[w] / nb > worst) worst = m[w] / nb;
    }
    free(m);
    return worst;
}

/* residual: the output against the input it came from, relative to the input (dB) */
static double residual_db(const float *y, const float *x, int lat, int from, int n)
{
    double e = 0, s = 0;
    for (int i = from; i < n; i++) { double d = y[i] - x[i - lat]; e += d * d; s += (double)x[i - lat] * x[i - lat]; }
    return 10 * log10(e / s + 1e-30);
}

/* the glue on a 440 Hz tone stepping between -6 and -26 dBFS every second: the step that comes
 * out, over the last 0.3 s of each second, averaged */
static double level_steps(int chain, float glue)
{
    int n = 8 * SR;
    float *a = malloc(4 * n), *b = malloc(4 * n);
    for (int i = 0; i < n; i++) a[i] = b[i] = (float)(((i / SR) & 1 ? 0.05 : 0.5) * sin(2 * M_PI * 440.0 * i / SR));
    ms_init(&E, SR); ms_set_chain(&E, chain); E.cur = chain; ms_set_fader(&E, MS_GLUE, glue);
    ms_process(&E, a, b, n);
    double sum = 0; int k = 0, lat = ms_latency(&E);
    for (int s = 2; s + 1 < 8; s += 2) {
        double hi = rms_db(a + lat, (int)(s * SR + 0.7 * SR), (s + 1) * SR);
        double lo = rms_db(a + lat, (int)((s + 1) * SR + 0.7 * SR), (s + 2) * SR - lat);
        sum += hi - lo; k++;
    }
    free(a); free(b);
    return sum / k;
}

static const float *g_crush;   /* the crush page for run(); NULL = its defaults */

static const char *chain_name[MS_CHAINS] = { "london", "punch", "clean", "lacquer" };

/* run a whole signal through one chain at fixed settings; block 0 = random sizes 1..300 */
static void run(const float *il, const float *ir, float *ol, float *orr, int n, int chain,
                const float f[4], const float p[4], int block)
{
    ms_init(&E, SR);
    ms_set_chain(&E, chain);
    E.cur = chain;   /* start in the chain, no crossfade from the default */
    for (int k = 0; k < 4; k++) { ms_set_fader(&E, k, f[k]); ms_set_param2(&E, k, p ? p[k] : 0.5f); }
    if (g_crush) for (int k = 0; k < 4; k++) { ms_set_crush(&E, k, g_crush[k]); E.cr[k] = g_crush[k]; }
    memcpy(ol, il, sizeof(float) * n); memcpy(orr, ir, sizeof(float) * n);
    unsigned s = 7;
    for (int i = 0; i < n;) {
        int m = block > 0 ? block : 1 + (int)((s = s * 1103515245u + 12345u) >> 16) % 300;
        if (m > n - i) m = n - i;
        ms_process(&E, ol + i, orr + i, m);
        i += m;
    }
}

static void noise(float *l, float *r, int n, float amp, unsigned seed)
{
    for (int i = 0; i < n; i++) { l[i] = amp * (float)nz(&seed); r[i] = amp * (float)nz(&seed); }
}

static const float REST[4] = { 0, 0, 0, 0 };
static const float MID[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
static const float FULL[4] = { 1, 1, 1, 1 };

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/test";
    const int only_file = argc > 7 && argv[1][0] == '-' && argv[1][1] == 0;
    if (only_file) dir = ".";
    const int N = 8 * SR, W = SR;   /* 8 s; measure after the first second */
    float *il = malloc(4 * N), *ir = malloc(4 * N), *l = malloc(4 * N), *r = malloc(4 * N);
    float *l2 = malloc(4 * N), *r2 = malloc(4 * N), *nl = malloc(4 * N), *nr = malloc(4 * N);
    make_loop(il, ir, N);
    wav_write(dir, "loop_dry.wav", il, ir, N);
    noise(nl, nr, N, 0.05f, 3);   /* white noise at about -26 dBFS rms: for the EQ curves */
    if (only_file) goto file_only;
    const int lat = ms_latency(&E);
    const float ceil = 0.89126f;  /* -1 dBFS */
    char what[128];

    /* the math the engine adds: the high shelf and high-pass, against their ideal responses */
    {
        fx_bq q; fx_bq_highshelf(&q, 2000.0f, 0.7f, 6.0f, SR);
        /* the DC and Nyquist gains of the biquad */
        double dc = (q.b0 + q.b1 + q.b2) / (1 + q.a1 + q.a2);
        double ny = (q.b0 - q.b1 + q.b2) / (1 - q.a1 + q.a2);
        check(fabs(20 * log10(fabs(dc))) < 0.01, "math: high shelf, DC gain (dB)", 20 * log10(fabs(dc)));
        check(fabs(20 * log10(fabs(ny)) - 6.0) < 0.2, "math: high shelf +6, top gain (dB)", 20 * log10(fabs(ny)));
        fx_bq_highpass(&q, 1000.0f, 0.707f, SR);
        dc = (q.b0 + q.b1 + q.b2) / (1 + q.a1 + q.a2);
        ny = (q.b0 - q.b1 + q.b2) / (1 - q.a1 + q.a2);
        check(fabs(dc) < 1e-6 && fabs(fabs(ny) - 1) < 1e-3, "math: high-pass, DC and top", fabs(ny));
    }

    for (int c = 0; c < MS_CHAINS; c++) {
        const char *nm = chain_name[c];

        /* at rest: a delay, bit for bit */
        run(il, ir, l, r, N, c, REST, NULL, 256);
        int exact = 1;
        for (int i = lat; i < N; i++) if (l[i] != il[i - lat] || r[i] != ir[i - lat]) { exact = 0; break; }
        snprintf(what, sizeof what, "%s rest: bit-exact input at the stated latency", nm);
        check(exact, what, exact);

        /* the block size makes no difference */
        run(il, ir, l, r, N, c, MID, NULL, 256);
        run(il, ir, l2, r2, N, c, MID, NULL, 0);
        snprintf(what, sizeof what, "%s: blocks of 256 and of 1..300: same output", nm);
        check(!memcmp(l, l2, 4 * N) && !memcmp(r, r2, 4 * N), what, 1);
        snprintf(what, sizeof what, "master_%d_%s_mid.wav", c + 1, nm);
        wav_write(dir, what, l, r, N);
        double lv_mid = rms_db(l, W, N) - rms_db(il, W, N);
        snprintf(what, sizeof what, "%s mid: loudness change (dB)", nm);
        check(lv_mid > -1.0 && lv_mid < 10.0, what, lv_mid);

        /* everything full, fed hot: never over the ceiling, never anything but numbers */
        for (int i = 0; i < N; i++) { l2[i] = il[i] * 2.4f; r2[i] = ir[i] * 2.4f; }
        run(l2, r2, l, r, N, c, FULL, NULL, 256);
        snprintf(what, sizeof what, "%s full, +1.6 dBFS in: peak under the -1 dBFS ceiling", nm);
        check(peak(l, r, N) <= ceil + 1e-6 && finite_all(l, r, N), what, peak(l, r, N));
        run(il, ir, l, r, N, c, FULL, NULL, 256);
        snprintf(what, sizeof what, "master_%d_%s_full.wav", c + 1, nm);
        wav_write(dir, what, l, r, N);

        /* 1: tone. Full: the chain's curve; flat at the bottom. Page 2 tilts it */
        {
            double lo0 = band_db(nl, W, N, 30, 70), hi0 = band_db(nl, W, N, 10000, 16000);
            run(nl, nr, l, r, N, c, (const float[4]){ 1, 0, 0, 0 }, NULL, 256);
            double dlo = band_db(l, W, N, 30, 70) - lo0, dhi = band_db(l, W, N, 10000, 16000) - hi0;
            snprintf(what, sizeof what, "%s tone full: lows (30-70 Hz) change (dB)", nm);
            int want_lo = c == MS_PUNCH ? 3 : 1;
            check(dlo > want_lo && dlo < 7, what, dlo);
            snprintf(what, sizeof what, "%s tone full: highs (10-16 kHz) change (dB)", nm);
            check(c == MS_LACQUER ? (dhi < -1 && dhi > -4) : (dhi > 1.5 && dhi < 5), what, dhi);
            run(nl, nr, l, r, N, c, (const float[4]){ 1, 0, 0, 0 }, (const float[4]){ 1, 0.5f, 0.5f, 0.5f }, 256);
            double up = band_db(l, W, N, 10000, 16000) - band_db(l, W, N, 30, 70);
            run(nl, nr, l, r, N, c, (const float[4]){ 1, 0, 0, 0 }, (const float[4]){ 0, 0.5f, 0.5f, 0.5f }, 256);
            double dn = band_db(l, W, N, 10000, 16000) - band_db(l, W, N, 30, 70);
            snprintf(what, sizeof what, "%s tone page 2: tilt, bright vs dark (dB)", nm);
            check(up - dn > 8, what, up - dn);
        }

        /* 2: glue. More fader, more reduction; denser; the level roughly kept */
        {
            run(il, ir, l2, r2, N, c, REST, NULL, 256);
            double gr[3]; const float fp[3] = { 0.25f, 0.5f, 1.0f };
            for (int k = 0; k < 3; k++) {
                run(il, ir, l, r, N, c, (const float[4]){ 0, fp[k], 0, 0 }, NULL, 256);
                gr[k] = ms_gain_reduction_db(&E);
            }
            snprintf(what, sizeof what, "%s glue: more reduction up the fader (dB at full)", nm);
            if (!(gr[0] < gr[1] && gr[1] < gr[2] && gr[0] > 0.3)) printf("  %.2f %.2f %.2f\n", gr[0], gr[1], gr[2]);
            check(gr[0] < gr[1] && gr[1] < gr[2] && gr[0] > 0.3, what, gr[2]);
            double lv = rms_db(l, W, N) - rms_db(l2, W, N);
            snprintf(what, sizeof what, "%s glue full: loudness change (dB)", nm);
            check(lv > -3 && lv < 4, what, lv);
            double st = level_steps(c, 1.0f);
            snprintf(what, sizeof what, "%s glue full: a 20 dB level step comes out (dB)", nm);
            check(st < 16, what, st);
            snprintf(what, sizeof what, "master_%d_%s_glue.wav", c + 1, nm);
            wav_write(dir, what, l, r, N);
            /* page 2: speed. Fast comes back sooner than slow: less reduction on average */
            double rs[2];
            for (int k = 0; k < 2; k++) {
                run(il, ir, l, r, N, c, (const float[4]){ 0, 1, 0, 0 }, (const float[4]){ 0.5f, (float)k, 0.5f, 0.5f }, 256);
                rs[k] = rms_db(l, W, N);
            }
            snprintf(what, sizeof what, "%s glue page 2: speed changes the sound (dB)", nm);
            check(fabs(rs[1] - rs[0]) > 0.2, what, rs[1] - rs[0]);
        }

        /* 3: colour */
        {
            run(il, ir, l2, r2, N, c, REST, NULL, 256);
            run(il, ir, l, r, N, c, (const float[4]){ 0, 0, 1, 0 }, NULL, 256);
            double res = residual_db(l, il, lat, W, N);
            snprintf(what, sizeof what, "%s colour full: changes the sound (residual dB)", nm);
            /* (Lacquer's mono bass has its own check below: the loop's bass is mono already) */
            check(c == MS_LACQUER || (res > -45 && res < -6), what, res);
            snprintf(what, sizeof what, "master_%d_%s_colour.wav", c + 1, nm);
            wav_write(dir, what, l, r, N);
            double lv = rms_db(l, W, N) - rms_db(l2, W, N);
            snprintf(what, sizeof what, "%s colour full: loudness change (dB)", nm);
            check(lv > -1.5 && lv < 4, what, lv);
            /* silence in, silence out, no DC */
            float *zl = calloc(N, 4), *zr = calloc(N, 4);
            run(zl, zr, l, r, N, c, FULL, NULL, 256);
            double d = rms_db(l, W, N);
            snprintf(what, sizeof what, "%s full: silence stays silent (dBFS)", nm);
            check(d < -120, what, d);
            free(zl); free(zr);
        }

        /* 3, page 2: width. Down: mono; up: wider */
        {
            run(il, ir, l, r, N, c, REST, (const float[4]){ 0.5f, 0.5f, 0.0f, 0.5f }, 256);
            double side = 0;
            for (int i = W; i < N; i++) side = fmax(side, fabs(l[i] - r[i]));
            snprintf(what, sizeof what, "%s width down: mono (largest l - r)", nm);
            check(side < 1e-6, what, side);
            run(nl, nr, l, r, N, c, REST, (const float[4]){ 0.5f, 0.5f, 1.0f, 0.5f }, 256);
            double es = 0, em = 0, es0 = 0;
            for (int i = W; i < N; i++) {
                es += (l[i] - r[i]) * (double)(l[i] - r[i]); em += (l[i] + r[i]) * (double)(l[i] + r[i]);
                es0 += (nl[i - lat] - nr[i - lat]) * (double)(nl[i - lat] - nr[i - lat]);
            }
            snprintf(what, sizeof what, "%s width up: side louder (dB)", nm);
            check(10 * log10(es / es0) > 3, what, 10 * log10(es / es0));
            (void)em;
        }

        /* 4: loudness: louder up the fader, peaks still at the ceiling */
        {
            run(il, ir, l2, r2, N, c, REST, NULL, 256);
            run(il, ir, l, r, N, c, (const float[4]){ 0, 0, 0, 1 }, NULL, 256);
            double lv = rms_db(l, W, N) - rms_db(l2, W, N);
            snprintf(what, sizeof what, "%s loudness full: louder (dB)", nm);
            check(lv > 3, what, lv);
            snprintf(what, sizeof what, "%s loudness full: peak at most the ceiling", nm);
            check(peak(l, r, N) <= ceil + 1e-6, what, peak(l, r, N));
            double rr[2];
            for (int k = 0; k < 2; k++) {
                run(il, ir, l, r, N, c, (const float[4]){ 0, 0, 0, 1 }, (const float[4]){ 0.5f, 0.5f, 0.5f, (float)k }, 256);
                rr[k] = rms_db(l, W, N);
            }
            snprintf(what, sizeof what, "%s loudness page 2: fast release is louder (dB)", nm);
            check(rr[1] - rr[0] > 0.1, what, rr[1] - rr[0]);
        }

        /* every fader swept on a steady tone: no clicks */
        {
            for (int i = 0; i < N; i++) l2[i] = r2[i] = 0.25f * (float)sin(2 * M_PI * 220.0 * i / SR);
            ms_init(&E, SR); ms_set_chain(&E, c); E.cur = c;
            memcpy(l, l2, 4 * N); memcpy(r, r2, 4 * N);
            for (int i = 0; i < N; i += 64) {
                double t = (double)i / N, tri = t < 0.5 ? 2 * t : 2 - 2 * t;
                for (int k = 0; k < 4; k++) ms_set_fader(&E, k, (float)tri);
                for (int k = 0; k < 4; k++) ms_set_param2(&E, k, (float)(0.5 + 0.5 * sin(2 * M_PI * (k + 1) * t)));
                ms_process(&E, l + i, r + i, 64);
            }
            double cr = fmax(click_ratio(l, N), click_ratio(r, N));
            snprintf(what, sizeof what, "%s sweep all faders: no clicks (worst vs neighbours)", nm);
            check(cr < 15 && finite_all(l, r, N), what, cr);
        }
    }

    /* the chain colours that are specific */
    {
        /* Lacquer: mono bass. Side below 100 Hz gone at full colour; side at 3 kHz kept */
        float *sl = malloc(4 * N), *sr = malloc(4 * N);
        for (int i = 0; i < N; i++) {
            double s = 0.2 * sin(2 * M_PI * 60.0 * i / SR) + 0.2 * sin(2 * M_PI * 3000.0 * i / SR);
            sl[i] = (float)s; sr[i] = (float)-s;   /* all side */
        }
        run(sl, sr, l, r, N, MS_LACQUER, (const float[4]){ 0, 0, 1, 0 }, NULL, 256);
        for (int i = 0; i < N; i++) l2[i] = 0.5f * (l[i] - r[i]);
        double s60 = band_db(l2, W, N, 45, 80) - band_db(sl, W, N, 45, 80);
        double s3k = band_db(l2, W, N, 2500, 3600) - band_db(sl, W, N, 2500, 3600);
        check(s60 < -12, "lacquer colour: side at 60 Hz taken out (dB)", s60);
        check(fabs(s3k) < 0.5, "lacquer colour: side at 3 kHz kept (dB)", s3k);

        /* Lacquer: the HF limiter: a loud 10 kHz tone is held down, a loud 200 Hz tone is not */
        for (int i = 0; i < N; i++) sl[i] = sr[i] = 0.5f * (float)sin(2 * M_PI * 10000.0 * i / SR);
        run(sl, sr, l, r, N, MS_LACQUER, REST, NULL, 256);
        double h = 20 * log10(peak(l + W, r + W, N - W));
        check(h < -12, "lacquer: a -6 dBFS 10 kHz tone held down (dBFS peak)", h);
        for (int i = 0; i < N; i++) sl[i] = sr[i] = 0.5f * (float)sin(2 * M_PI * 200.0 * i / SR);
        run(sl, sr, l, r, N, MS_LACQUER, REST, NULL, 256);
        h = 20 * log10(peak(l + W, r + W, N - W));
        check(h > -6.3, "lacquer: a -6 dBFS 200 Hz tone untouched (dBFS peak)", h);
        free(sl); free(sr);
    }

    /* the crush page, ahead of the chain (London at rest, so only the crush and the limiter act) */
    {
        float *sl = malloc(4 * N), *sr = malloc(4 * N);
        for (int i = 0; i < N; i++) sl[i] = sr[i] = 0.5f * (float)sin(2 * M_PI * 440.0 * i / SR);

        /* AMOUNT down: BITS and RATE do nothing, bit for bit */
        g_crush = (const float[4]){ 0, 1, 1, 0.5f };
        run(il, ir, l, r, N, MS_LONDON, REST, NULL, 256);
        int exact = 1;
        for (int i = lat; i < N; i++) if (l[i] != il[i - lat] || r[i] != ir[i - lat]) { exact = 0; break; }
        check(exact, "crush: amount down, bits and rate up: bit-exact", exact);

        /* 4 bits (BITS at 0.8): a -6 dBFS sine carries a lot of quantization error; 16 bits:
         * hardly any */
        g_crush = (const float[4]){ 1, 0.8f, 0, 0.5f };
        run(sl, sr, l, r, N, MS_LONDON, REST, NULL, 256);
        double e4 = residual_db(l, sl, lat, W, N);
        check(e4 > -30 && e4 < -10, "crush: 4 bits, error against the input (dB)", e4);
        g_crush = (const float[4]){ 1, 0, 0, 0.5f };
        run(sl, sr, l, r, N, MS_LONDON, REST, NULL, 256);
        double e16 = residual_db(l, sl, lat, W, N);
        check(e16 < -80, "crush: 16 bits, error against the input (dB)", e16);
        /* half AMOUNT: half the error, about -6 dB */
        g_crush = (const float[4]){ 0.5f + CRUSH_HALF, 0.8f, 0, 0.5f };
        run(sl, sr, l, r, N, MS_LONDON, REST, NULL, 256);
        double eh = residual_db(l, sl, lat, W, N) - e4;
        check(eh > -7.5 && eh < -4.5, "crush: half amount, error against full (dB)", eh);

        /* 1 bit: two levels, a square that keeps the sine's sign and its level */
        g_crush = (const float[4]){ 1, 1, 0, 0.5f };
        run(sl, sr, l, r, N, MS_LONDON, REST, NULL, 256);
        int sq = 1; double lo1 = 1, hi1 = 0;
        for (int i = W; i < N; i++) {
            double x = sl[i - lat], y = l[i];
            if (fabs(x) > 0.02 && (x > 0) != (y > 0)) sq = 0;
            if (fabs(y) < lo1) lo1 = fabs(y);
            if (fabs(y) > hi1) hi1 = fabs(y);
        }
        check(sq && lo1 > 0.4 && hi1 < 0.51, "crush: 1 bit, a square at the level (smallest |y|)", lo1);

        /* RATE up (2 kHz): a 440 Hz tone grows images above 1.5 kHz */
        g_crush = (const float[4]){ 1, 0, 1, 0.5f };
        run(sl, sr, l, r, N, MS_LONDON, REST, NULL, 256);
        double im = band_db(l, W, N, 1500, 20000) - rms_db(l, W, N);
        check(im > -20, "crush: rate 2 kHz, images over 1.5 kHz (dB re all)", im);

        /* FILTER down: high-pass at 6 kHz; up: low-pass at 60 Hz */
        g_crush = (const float[4]){ 0, 0.5f, 0.5f, 0 };
        run(nl, nr, l, r, N, MS_LONDON, REST, NULL, 256);
        double lo = band_db(l, W, N, 80, 200) - band_db(nl, W, N, 80, 200);
        double hi = band_db(l, W, N, 10000, 16000) - band_db(nl, W, N, 10000, 16000);
        check(lo < -40 && fabs(hi) < 3, "crush: filter down, 80-200 Hz taken out (dB)", lo);
        g_crush = (const float[4]){ 0, 0.5f, 0.5f, 1 };
        run(nl, nr, l, r, N, MS_LONDON, REST, NULL, 256);
        lo = band_db(l, W, N, 25, 45) - band_db(nl, W, N, 25, 45);
        hi = band_db(l, W, N, 2000, 5000) - band_db(nl, W, N, 2000, 5000);
        check(hi < -40 && lo > -3,   /* the lows stay; the resonance may lift them */
               "crush: filter up, 2-5 kHz taken out (dB)", hi);
        g_crush = (const float[4]){ 0, 0.5f, 0.5f, 0.56f };
        run(il, ir, l, r, N, MS_LONDON, REST, NULL, 256);
        exact = 1;
        for (int i = lat; i < N; i++) if (l[i] != il[i - lat] || r[i] != ir[i - lat]) { exact = 0; break; }
        check(exact, "crush: filter in its flat middle: bit-exact", exact);

        /* every chain with the crush on: the block size makes no difference */
        g_crush = (const float[4]){ 0.7f, 0.6f, 0.4f, 0.8f };
        int same = 1;
        for (int c = 0; c < MS_CHAINS; c++) {
            run(il, ir, l, r, N, c, MID, NULL, 256);
            run(il, ir, l2, r2, N, c, MID, NULL, 0);
            same &= !memcmp(l, l2, 4 * N) && !memcmp(r, r2, 4 * N);
            if (c == MS_PUNCH) wav_write(dir, "crush_punch.wav", l, r, N);
        }
        check(same, "crush: blocks of 256 and of 1..300: same output", same);
        g_crush = NULL;

        /* moving: AMOUNT and FILTER swept up and down on a tone, no clicks */
        ms_init(&E, SR); E.cur = MS_LONDON;
        memcpy(l, sl, 4 * N); memcpy(r, sr, 4 * N);
        for (int i = 0; i < N; i += 64) {
            double t = (double)i / N, tri = t < 0.5 ? 2 * t : 2 - 2 * t;
            ms_set_crush(&E, MS_CR_AMOUNT, (float)tri);
            ms_set_crush(&E, MS_CR_BITS, 0.3f);
            ms_set_crush(&E, MS_CR_FILTER, (float)(0.5 + 0.45 * sin(2 * M_PI * t * 2)));
            ms_process(&E, l + i, r + i, 64);
        }
        double cr = fmax(click_ratio(l, N), click_ratio(r, N));
        check(cr < 15 && finite_all(l, r, N), "crush: amount and filter swept, no clicks", cr);
        free(sl); free(sr);
    }

    /* switching: chain to chain and to bypass, while the music plays: no clicks, the latency
     * never changes, and each switch is complete within 120 ms */
    {
        ms_init(&E, SR);
        for (int k = 0; k < 4; k++) ms_set_fader(&E, k, 0.6f);
        memcpy(l, il, 4 * N); memcpy(r, ir, 4 * N);
        int ok_time = 1, step = SR / 2;
        const int seq[] = { 1, 2, 3, 0, -1, 2, -2, 3 };   /* -1: bypass on, -2: bypass off */
        for (int s = 0; s < 8; s++) {
            int at = s * step;
            if (seq[s] >= 0) ms_set_chain(&E, seq[s]); else ms_set_bypass(&E, seq[s] == -1);
            int want = E.bypass ? MS_DRY : E.chain;
            int done = -1;
            for (int i = at; i < at + step; i += 64) {
                ms_process(&E, l + i, r + i, 64);
                if (done < 0 && E.cur == want && E.nxt < 0) done = i + 64 - at;
            }
            if (done < 0 || done > SR * 0.12) { ok_time = 0; printf("  switch %d: done after %d frames\n", s, done); }
        }
        ms_process(&E, l + 8 * step, r + 8 * step, N - 8 * step);
        check(ok_time, "switch: each switch done within 120 ms", ok_time);
        double cr = fmax(click_ratio(l, N), click_ratio(r, N));
        check(cr < 15 && finite_all(l, r, N), "switch: chains and bypass, no clicks", cr);
        wav_write(dir, "switching.wav", l, r, N);

        /* in bypass (the crush page up too) the output is the input at the latency, bit for bit */
        ms_init(&E, SR);
        for (int k = 0; k < 4; k++) { ms_set_fader(&E, k, 1.0f); ms_set_crush(&E, k, 1.0f); }
        ms_set_bypass(&E, 1);
        memcpy(l, il, 4 * N); memcpy(r, ir, 4 * N);
        ms_process(&E, l, r, N);
        int exact = 1;
        for (int i = SR / 2; i < N; i++) if (l[i] != il[i - lat] || r[i] != ir[i - lat]) { exact = 0; break; }
        check(exact, "bypass: bit-exact input at the stated latency", exact);
    }

file_only:
    if (argc > 7) {
        float *wl, *wr;
        int n = wav_read(argv[2], &wl, &wr);
        if (n <= 0) { fprintf(stderr, "can't read %s\n", argv[2]); return 1; }
        int c = atoi(argv[3]) - 1;
        ms_init(&E, SR); ms_set_chain(&E, c); E.cur = c;
        for (int k = 0; k < 4; k++) ms_set_fader(&E, k, (float)atof(argv[4 + k]));
        for (int k = 0; k < 4 && argc > 11; k++) ms_set_param2(&E, k, (float)atof(argv[8 + k]));
        for (int k = 0; k < 4 && argc > 16; k++) ms_set_crush(&E, k, (float)atof(argv[13 + k]));
        for (int i = 0; i < n; i += 256) ms_process(&E, wl + i, wr + i, n - i < 256 ? n - i : 256);
        const char *name = argc > 12 ? argv[12] : "yours.wav";
        if (only_file && name[0] == '/') { wav_write("", name, wl, wr, n); }
        else wav_write(dir, name, wl, wr, n);
        printf("wrote %s/%s\n", dir, name);
    }

    printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
