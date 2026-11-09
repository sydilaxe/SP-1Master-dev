/* SP-1 Master: the mastering engine. See master.h for the chains and the faders. */
#include "master.h"

#ifdef MS_PROFILE   /* tools/bench_m4.py: keep the stages apart so their cost can be read */
#define STAGE __attribute__((noinline))
#else
#define STAGE
#endif

#define CR 8               /* compressor gain update: every eighth frame */
#define WARM_S 0.030f      /* a chain coming in runs unheard this long, so its compressors settle */
#define FADE_S 0.050f      /* the crossfade to and from bypass */
#define DIP_S 0.015f       /* chain to chain: the fade out, and the fade in */
enum { DIP_NONE, DIP_OUT, DIP_WARM, DIP_IN };

/* one-pole coefficient for a time constant, applied every `every` frames */
static float step_coef(float seconds, float sr, float every)
{
    return 1.0f - fx_exp2f(-1.44269504f * every / (seconds * sr));
}
static float sample_coef(float seconds, float sr) { return step_coef(seconds, sr, 1.0f); }

static inline float glide(float v, float target, float k)
{
    v += (target - v) * k;
    return fx_absf(target - v) < 1e-5f ? target : v;
}

/* from a to b on a log scale, t = 0..1 */
static inline float logmix(float a, float b, float t) { return a * fx_exp2f(t * fx_log2f(b / a)); }

/* a page 2 control whose centre (a tenth either side) is neutral: -1 .. 0 .. 1 */
static inline float centred(float p)
{
    if (p < 0.45f) return (p - 0.45f) / 0.45f;
    if (p > 0.55f) return (p - 0.55f) / 0.45f;
    return 0.0f;
}

/* ============================================================ compressor ==== */

static void comp_reset(ms_comp *c)
{
    c->det = c->gf = c->gs = 0.0f;
    c->g = fx_db2lin(c->makeup);
    c->dg = 0.0f;
    c->sc[0] = c->sc[1] = 0.0f;
    c->ph = 0;
    c->gr = 0.0f;
}

/* the gain for each frame of x0/x1 (the sidechain) into gain[] */
static void comp_run(ms_comp *c, const float *x0, const float *x1, float *gain, int n)
{
    const float da = c->det_att, dr = c->det_rel, thr = c->thr, slope = c->slope, knee = c->knee;
    const float ca = c->c_att, cr = c->c_rel, cc = c->c_chg, cm = c->c_mem, mk = c->makeup;
    const float scc = c->sc_c, fade = c->fade;
    float det = c->det, gf = c->gf, gs = c->gs, g = c->g, dg = c->dg;
    float s0 = c->sc[0], s1 = c->sc[1];
    int ph = c->ph;
    for (int i = 0; i < n; i++) {
        s0 += scc * (x0[i] - s0);
        s1 += scc * (x1[i] - s1);
        uint32_t a0 = fx_abs_bits(x0[i] - s0), a1 = fx_abs_bits(x1[i] - s1);
        float pk = fx_from_bits(a0 > a1 ? a0 : a1);
        det += (pk - det) * (pk > det ? da : dr);
        if (ph == 0) {
            float over = fx_lin2db(det + 1e-9f) - thr, gt;
            if (2.0f * over < -knee) gt = 0.0f;
            else if (2.0f * over > knee) gt = slope * over;
            else { float k = over + 0.5f * knee; gt = slope * k * k * (0.5f / knee); }
            gf += (gt - gf) * (gt > gf ? ca : cr);
            float gr = gf;
            if (cc > 0.0f) {
                float ts = 0.5f * gf;
                gs += (ts - gs) * (ts > gs ? cc : cm);
                if (gs > gr) gr = gs;
            }
            gr *= fade;
            c->gr = gr;
            dg = (fx_db2lin(mk - gr) - g) * (1.0f / CR);
        }
        ph = (ph + 1) & (CR - 1);
        g += dg;
        gain[i] = g;
    }
    c->det = det; c->gf = gf; c->gs = gs; c->g = g; c->dg = dg; c->ph = ph;
    c->sc[0] = s0; c->sc[1] = s1;
}

/* the static settings that do not move with the faders */
static void comp_times(ms_comp *c, float det_att, float det_rel, float att, float rel,
                       float chg, float mem, float sc_hz, float sr)
{
    c->det_att = sample_coef(det_att, sr);
    c->det_rel = sample_coef(det_rel, sr);
    c->c_att = step_coef(att, sr, CR);
    c->c_rel = step_coef(rel, sr, CR);
    c->c_chg = chg > 0.0f ? step_coef(chg, sr, CR) : 0.0f;
    c->c_mem = mem > 0.0f ? step_coef(mem, sr, CR) : 0.0f;
    c->sc_c = sc_hz > 0.0f ? 1.0f - fx_exp2f(-1.44269504f * 2.0f * FX_PI * sc_hz / sr) : 0.0f;
}

/* ================================================================ chains ==== */

/* each chain's tone curve at full: low shelf, bell, high shelf */
static const struct {
    float ls_hz, ls_q, ls_db, bl_hz, bl_q, bl_db, hs_hz, hs_q, hs_db;
} tone_tab[MS_CHAINS] = {
    /* London: curve-bender shelves and a little presence */
    {  100.0f, 0.70f, 4.0f,  2800.0f, 0.70f,  1.5f, 10000.0f, 0.70f,  3.0f },
    /* Punch: the low boost with the dip above it, and a 10 kHz lift */
    {   60.0f, 0.70f, 5.0f,   220.0f, 1.00f, -2.5f, 10000.0f, 0.60f,  3.5f },
    /* Clean: broad Baxandall-style shelves, a little less mud */
    {   80.0f, 0.50f, 2.5f,   350.0f, 0.80f, -1.5f, 12000.0f, 0.50f,  4.0f },
    /* Lacquer: warm low end, a touch of presence, a softer top */
    {  120.0f, 0.60f, 2.5f,  3500.0f, 1.00f,  1.0f,  9000.0f, 0.70f, -2.5f },
};
#define TILT_DB 4.0f

static void chain_init(ms_chain *c, int id, float sr)
{
    __builtin_memset(c, 0, sizeof *c);
    c->id = id;
    for (int k = 0; k < MS_SLOTS * 2; k++) c->set[k] = -1.0f;
    c->dc_r = 1.0f - 2.0f * FX_PI * 5.0f / sr;
    c->width = 1.0f;
    c->drive_g = c->drive_now = 1.0f;
    c->hf_g = 1.0f;
    c->hf_att = sample_coef(0.0002f, sr);
    c->hf_rel = sample_coef(0.030f, sr);
    c->hf_thr = fx_db2lin(-16.0f);
    fx_bq_lowpass(&c->hf_hp, 6000.0f, 0.707f, sr);   /* the band above is x minus this */
    fx_bq_highpass(&c->air, 7000.0f, 0.707f, sr);
    fx_bq_lowpass(&c->xo[0], 180.0f, 0.5f, sr);     /* Clean's band splits */
    fx_bq_lowpass(&c->xo[1], 3500.0f, 0.5f, sr);
    /* Punch's parallel compressor: fast and heavy */
    comp_times(&c->par, 0.0001f, 0.005f, 0.0005f, 0.060f, 0.0f, 0.0f, 0.0f, sr);
    c->par.thr = -32.0f; c->par.slope = 0.9f; c->par.knee = 2.0f; c->par.makeup = 10.0f;
    c->par.fade = 1.0f;
}

/* back to a clean start: the chain is about to come in */
static void chain_reset(ms_chain *c)
{
    __builtin_memset(c->eqs, 0, sizeof c->eqs);
    __builtin_memset(c->xos, 0, sizeof c->xos);
    __builtin_memset(c->airs, 0, sizeof c->airs);
    __builtin_memset(&c->sides, 0, sizeof c->sides);
    __builtin_memset(c->hfs, 0, sizeof c->hfs);
    c->dc_x[0] = c->dc_x[1] = c->dc_y[0] = c->dc_y[1] = 0.0f;
    for (int k = 0; k < 3; k++) comp_reset(&c->comp[k]);
    comp_reset(&c->par);
    c->hf_det = 0.0f; c->hf_g = 1.0f; c->hf_dg = 0.0f; c->hf_ph = 0;
    c->drive_now = c->drive_g;
    c->drive_step = 0.0f;
    for (int k = 0; k < MS_SLOTS * 2; k++) c->set[k] = -1.0f;   /* make everything again */
}

STAGE static void chain_control(ms_chain *c, const float *f, const float *p, float sr)
{
    const int id = c->id;

    /* ---- 1: tone ---- */
    if (f[0] != c->set[0] || p[0] != c->set[4]) {
        c->set[0] = f[0]; c->set[4] = p[0];
        float a = f[0], t = centred(p[0]) * TILT_DB;
        float ls = a * tone_tab[id].ls_db - t, bl = a * tone_tab[id].bl_db;
        float hs = a * tone_tab[id].hs_db + t;
        int on = fx_absf(ls) > 0.01f || fx_absf(bl) > 0.01f || fx_absf(hs) > 0.01f;
        if (on && !c->eq_on) __builtin_memset(c->eqs, 0, sizeof c->eqs);
        c->eq_on = on;
        fx_bq_lowshelf(&c->eq[0], tone_tab[id].ls_hz, tone_tab[id].ls_q, ls, sr);
        fx_bq_bell(&c->eq[1], tone_tab[id].bl_hz, tone_tab[id].bl_q, bl, sr);
        fx_bq_highshelf(&c->eq[2], tone_tab[id].hs_hz, tone_tab[id].hs_q, hs, sr);
    }

    /* ---- 2: glue ---- */
    if (f[1] != c->set[1] || p[1] != c->set[5]) {
        c->set[1] = f[1]; c->set[5] = p[1];
        float a = f[1], s = p[1];
        int nb = id == MS_CLEAN ? 3 : 1;
        for (int b = 0; b < nb; b++) {
            ms_comp *m = &c->comp[b];
            /* on as soon as the fader moves; off (in chain_process) once it is down and the gain
             * has glided back to unity, so it never stops with a step */
            if (a > 0.0f && !m->active) { m->active = 1; m->fade = 0.0f; m->makeup = 0.0f; comp_reset(m); }
            m->fade = fx_clampf(8.0f * a, 0.0f, 1.0f);
            switch (id) {
            case MS_LONDON:   /* slow tube (variable-mu): soft, with the ratio rising with level */
                comp_times(m, 0.0005f, 0.030f, logmix(0.025f, 0.005f, s), logmix(1.2f, 0.25f, s),
                           0.0f, 0.0f, 0.0f, sr);
                m->thr = -8.0f - 16.0f * a; m->slope = 0.6f; m->knee = 18.0f;
                m->makeup = 4.0f * a;
                break;
            case MS_PUNCH:    /* VCA bus compressor, 4:1, auto release, the kick kept out */
                comp_times(m, 0.0002f, 0.010f, logmix(0.030f, 0.001f, s), logmix(0.300f, 0.080f, s),
                           0.8f, 1.5f, 90.0f, sr);
                m->thr = -10.0f - 16.0f * a; m->slope = 0.75f; m->knee = 4.0f;
                m->makeup = 6.0f * a;
                break;
            case MS_CLEAN:    /* one band of three: 2.5:1, quick and even */
                comp_times(m, 0.0005f, 0.020f, logmix(0.020f, 0.003f, s), logmix(0.300f, 0.060f, s),
                           0.0f, 0.0f, 0.0f, sr);
                m->thr = -10.0f - 14.0f * a; m->slope = 0.6f; m->knee = 6.0f;
                m->makeup = 4.0f * a;
                break;
            default:          /* Lacquer: gentle 2:1 tube, slow, with memory */
                comp_times(m, 0.0005f, 0.030f, logmix(0.020f, 0.005f, s), logmix(0.800f, 0.200f, s),
                           1.0f, 2.0f, 0.0f, sr);
                m->thr = -6.0f - 18.0f * a; m->slope = 0.5f; m->knee = 10.0f;
                m->makeup = 4.0f * a;
                break;
            }
        }
    }

    /* ---- 3: colour, and the width on its page 2 ---- */
    if (f[2] != c->set[2] || p[2] != c->set[6]) {
        c->set[2] = f[2]; c->set[6] = p[2];
        float a = f[2], w = centred(p[2]);
        c->width = w < 0.0f ? 1.0f + w : 1.0f + 0.6f * w;
        int was = c->col_on;
        c->col_on = a > 0.0f;
        int fresh = c->col_on && !was;
        switch (id) {
        case MS_LONDON:   /* the console's transformers: soft, a little asymmetric */
            c->drive = 1.0f + 1.5f * a;
            c->inv_drive = 1.0f / c->drive;
            c->sat_mix = fx_clampf(1.5f * a, 0.0f, 1.0f);
            c->bias = 0.06f * a;
            c->bias_out = fx_softclip(c->bias);
            c->sat_trim = fx_db2lin(-1.0f * a);
            if (fresh) c->dc_x[0] = c->dc_x[1] = c->dc_y[0] = c->dc_y[1] = 0.0f;
            break;
        case MS_PUNCH:    /* the parallel compressor, blended in */
            c->par_mix = 0.5f * a;
            c->par_trim = fx_db2lin(-2.5f * a);
            c->par.active = c->col_on;
            if (fresh) comp_reset(&c->par);
            break;
        case MS_CLEAN:    /* the air band, saturated and blended in */
            c->air_mix = 0.25f * a;
            if (fresh) __builtin_memset(c->airs, 0, sizeof c->airs);
            break;
        default:          /* Lacquer: mono below 40 Hz (just up) to 300 Hz (full up) */
            fx_bq_highpass(&c->side_lp, 40.0f * fx_exp2f(2.9f * a), 0.707f, sr);
            c->side_mix = fx_clampf(8.0f * a, 0.0f, 1.0f);
            if (fresh) __builtin_memset(&c->sides, 0, sizeof c->sides);
            break;
        }
    }

    /* ---- 4: loudness (the release is the shared limiter's) ---- */
    c->drive_g = fx_db2lin(12.0f * f[3]);
    c->drive_step = (c->drive_g - c->drive_now) * (1.0f / (float)MS_CHUNK);
    c->clip_mix = f[3];
}

/* every frame, the width: side gain w */
static inline void width_run(float *l, float *r, int n, float w)
{
    const float k = 0.5f * (1.0f - w);   /* l' = l - k (l - r), r' = r + k (l - r) */
    for (int i = 0; i < n; i++) {
        float d = k * (l[i] - r[i]);
        l[i] -= d; r[i] += d;
    }
}

STAGE static void chain_process(ms_chain *c, float *l, float *r, int n, int ends)
{
    c->phase_end = ends;
    const int id = c->id;
    float g0[MS_CHUNK];

    /* ---- 1: tone ---- */
    if (c->eq_on) {
        const fx_bq A = c->eq[0], B = c->eq[1], C = c->eq[2];
        for (int ch = 0; ch < 2; ch++) {
            float *x = ch ? r : l;
            fx_bq_state *s = c->eqs[ch];
            float a1 = s[0].s1, a2 = s[0].s2, b1 = s[1].s1, b2 = s[1].s2, c1 = s[2].s1, c2 = s[2].s2;
            for (int i = 0; i < n; i++) {
                float y = fx_bq_tick(A.b0, A.b1, A.b2, A.a1, A.a2, &a1, &a2, x[i]);
                y = fx_bq_tick(B.b0, B.b1, B.b2, B.a1, B.a2, &b1, &b2, y);
                x[i] = fx_bq_tick(C.b0, C.b1, C.b2, C.a1, C.a2, &c1, &c2, y);
            }
            s[0].s1 = a1; s[0].s2 = a2; s[1].s1 = b1; s[1].s2 = b2; s[2].s1 = c1; s[2].s2 = c2;
        }
    }

    /* ---- 2: glue ---- */
    if (id == MS_CLEAN && c->comp[0].active) {
        /* three bands that add back up exactly: low = LP1(x), rest = x - low,
         * mid = LP2(rest), high = rest - mid */
        float lo[2][MS_CHUNK], mi[2][MS_CHUNK], hi[2][MS_CHUNK], g1[MS_CHUNK], g2[MS_CHUNK];
        const fx_bq X = c->xo[0], Y = c->xo[1];
        for (int ch = 0; ch < 2; ch++) {
            const float *x = ch ? r : l;
            float x1 = c->xos[ch][0].s1, x2 = c->xos[ch][0].s2, y1 = c->xos[ch][1].s1, y2 = c->xos[ch][1].s2;
            for (int i = 0; i < n; i++) {
                float lw = fx_bq_tick(X.b0, X.b1, X.b2, X.a1, X.a2, &x1, &x2, x[i]);
                float rest = x[i] - lw;
                float md = fx_bq_tick(Y.b0, Y.b1, Y.b2, Y.a1, Y.a2, &y1, &y2, rest);
                lo[ch][i] = lw; mi[ch][i] = md; hi[ch][i] = rest - md;
            }
            c->xos[ch][0].s1 = x1; c->xos[ch][0].s2 = x2; c->xos[ch][1].s1 = y1; c->xos[ch][1].s2 = y2;
        }
        comp_run(&c->comp[0], lo[0], lo[1], g0, n);
        comp_run(&c->comp[1], mi[0], mi[1], g1, n);
        comp_run(&c->comp[2], hi[0], hi[1], g2, n);
        for (int i = 0; i < n; i++) {
            l[i] = lo[0][i] * g0[i] + mi[0][i] * g1[i] + hi[0][i] * g2[i];
            r[i] = lo[1][i] * g0[i] + mi[1][i] * g1[i] + hi[1][i] * g2[i];
        }
        if (c->set[1] == 0.0f) {
            /* all the way down: stop once every band is back at unity */
            int idle = 1;
            for (int b = 0; b < 3; b++) if (fx_absf(c->comp[b].g - 1.0f) > 1e-6f) idle = 0;
            if (idle) for (int b = 0; b < 3; b++) c->comp[b].active = 0;
        }
    } else if (id != MS_CLEAN && c->comp[0].active) {
        ms_comp *m = &c->comp[0];
        comp_run(m, l, r, g0, n);
        for (int i = 0; i < n; i++) { l[i] *= g0[i]; r[i] *= g0[i]; }
        if (c->set[1] == 0.0f && fx_absf(m->g - 1.0f) < 1e-6f) { m->active = 0; m->gr = 0.0f; }
    }

    /* ---- 3: colour ---- */
    if (c->col_on) {
        switch (id) {
        case MS_LONDON: {
            /* y + mix * ((softclip(y * drive + bias) - softclip(bias)) / drive - y), DC-blocked */
            const float drive = c->drive, idr = c->inv_drive, mix = c->sat_mix, bias = c->bias;
            const float bout = c->bias_out, trim = c->sat_trim, dcr = c->dc_r;
            for (int ch = 0; ch < 2; ch++) {
                float *x = ch ? r : l;
                float dx = c->dc_x[ch], dy = c->dc_y[ch];
                for (int i = 0; i < n; i++) {
                    /* only what the saturation adds is DC-blocked: at mix 0 this is exact */
                    float y = x[i];
                    float w = mix * ((fx_softclip(y * drive + bias) - bout) * idr - y);
                    float o = w - dx + dcr * dy;
                    dx = w; dy = o;
                    x[i] = (y + o) * trim;
                }
                c->dc_x[ch] = dx; c->dc_y[ch] = dy;
            }
            break;
        }
        case MS_PUNCH: {
            ms_comp *m = &c->par;
            comp_run(m, l, r, g0, n);
            const float mix = c->par_mix, trim = c->par_trim;
            for (int i = 0; i < n; i++) {
                l[i] = (l[i] + mix * l[i] * g0[i]) * trim;
                r[i] = (r[i] + mix * r[i] * g0[i]) * trim;
            }
            break;
        }
        case MS_CLEAN: {
            const fx_bq H = c->air;
            const float mix = c->air_mix;
            for (int ch = 0; ch < 2; ch++) {
                float *x = ch ? r : l;
                float s1 = c->airs[ch].s1, s2 = c->airs[ch].s2;
                for (int i = 0; i < n; i++) {
                    float h = fx_bq_tick(H.b0, H.b1, H.b2, H.a1, H.a2, &s1, &s2, x[i]);
                    x[i] += mix * fx_softclip(2.0f * h);
                }
                c->airs[ch].s1 = s1; c->airs[ch].s2 = s2;
            }
            break;
        }
        default: {
            /* mono bass: the side through a high-pass (blended in from the bottom of the fader) */
            const fx_bq L = c->side_lp;
            const float mix = c->side_mix;
            float s1 = c->sides.s1, s2 = c->sides.s2;
            for (int i = 0; i < n; i++) {
                float side = 0.5f * (l[i] - r[i]);
                float low = mix * (side - fx_bq_tick(L.b0, L.b1, L.b2, L.a1, L.a2, &s1, &s2, side));
                l[i] -= low; r[i] += low;
            }
            c->sides.s1 = s1; c->sides.s2 = s2;
            break;
        }
        }
    }
    if (c->width != 1.0f) width_run(l, r, n, c->width);

    /* ---- 4: loudness ---- */
    {
        float g = c->drive_now;
        const float gs = c->drive_step;
        if (g != 1.0f || gs != 0.0f) {
            for (int i = 0; i < n; i++) { g += gs; l[i] *= g; r[i] *= g; }
            /* the chunk's end lands exactly on the target */
            c->drive_now = c->phase_end ? c->drive_g : g;
        }
    }
    if (id == MS_PUNCH && c->clip_mix > 0.0f) {
        /* a soft clipper rounding the peaks off toward +1 dBFS; the limiter takes the rest */
        const float cl = 1.12f, icl = 1.0f / 1.12f, mix = c->clip_mix;
        for (int i = 0; i < n; i++) {
            l[i] += mix * (cl * fx_softclip(l[i] * icl) - l[i]);
            r[i] += mix * (cl * fx_softclip(r[i] * icl) - r[i]);
        }
    } else if (id == MS_LACQUER) {
        /* the high-frequency limiter: the band above 6 kHz is held under -16 dBFS, as a cutting
         * lathe needs; the rest passes untouched */
        float hb[2][MS_CHUNK];
        const fx_bq H = c->hf_hp;
        for (int ch = 0; ch < 2; ch++) {
            const float *x = ch ? r : l;
            float s1 = c->hfs[ch].s1, s2 = c->hfs[ch].s2;
            for (int i = 0; i < n; i++) hb[ch][i] = x[i] - fx_bq_tick(H.b0, H.b1, H.b2, H.a1, H.a2, &s1, &s2, x[i]);
            c->hfs[ch].s1 = s1; c->hfs[ch].s2 = s2;
        }
        float det = c->hf_det, g = c->hf_g, dg = c->hf_dg;
        int ph = c->hf_ph;
        const float da = c->hf_att, dr = c->hf_rel, thr = c->hf_thr;
        for (int i = 0; i < n; i++) {
            uint32_t a0 = fx_abs_bits(hb[0][i]), a1 = fx_abs_bits(hb[1][i]);
            float pk = fx_from_bits(a0 > a1 ? a0 : a1);
            det += (pk - det) * (pk > det ? da : dr);
            if (ph == 0) dg = ((det > thr ? thr / det : 1.0f) - g) * (1.0f / CR);
            ph = (ph + 1) & (CR - 1);
            g += dg;
            float k = 1.0f - g;
            l[i] -= k * hb[0][i];
            r[i] -= k * hb[1][i];
        }
        c->hf_det = det; c->hf_g = g; c->hf_dg = dg; c->hf_ph = ph;
    }
}

/* the chain's compressor reduction now, dB */
static float chain_gr(const ms_chain *c)
{
    float gr = 0.0f;
    int nb = c->id == MS_CLEAN ? 3 : 1;
    for (int b = 0; b < nb; b++)
        if (c->comp[b].active && c->comp[b].gr > gr) gr = c->comp[b].gr;
    return gr;
}

/* ============================================================== limiter ==== */

/* The look-ahead limiter: the input waits LIM_LA frames (6 segments of 8) in the delay line.
 * Once a segment, the gain the outgoing segment ends on is set from the peaks still to come:
 * the full reduction for the segments next in line, ramping down to a fifth of it for the
 * newest. Between those points the gain moves in a straight line, so every frame is at or under
 * the ceiling and the gain has no corners. It lets go at the rate page 2 of fader 4 sets. At
 * unity it passes the input exactly. */
static void limiter_init(ms_limiter *m, float sr)
{
    __builtin_memset(m, 0, sizeof *m);
    m->ceiling = fx_db2lin(-1.0f);
    m->s_rel = step_coef(0.1f, sr, LIM_SEG);
    m->sg = 1.0f;
}

STAGE static void limiter_process(ms_limiter *m, float *l, float *r, int n)
{
    const float ceil = m->ceiling;
    const uint32_t ceil_bits = fx_abs_bits(ceil);
    uint32_t acc = fx_abs_bits(m->acc_pk);
    float g = m->sg, gs = m->sg_step, meter = m->red;
    int p = m->pos_dl, sn = m->sn;
    while (n > 0) {
        int run = LIM_SEG - sn;
        if (run > n) run = n;
        float *d0 = &m->dl[0][p], *d1 = &m->dl[1][p];
        for (int i = 0; i < run; i++) {
            float xl = l[i], xr = r[i];
            uint32_t ul = fx_abs_bits(xl), ur = fx_abs_bits(xr);
            uint32_t u = ul > ur ? ul : ur;
            acc = u > acc ? u : acc;
            float ol = d0[i] * g, orr = d1[i] * g;
            d0[i] = xl; d1[i] = xr;
            if (fx_abs_bits(ol) > ceil_bits) ol = ol > 0.0f ? ceil : -ceil;
            if (fx_abs_bits(orr) > ceil_bits) orr = orr > 0.0f ? ceil : -ceil;
            l[i] = ol; r[i] = orr;
            g += gs;
        }
        l += run; r += run; n -= run;
        p += run; if (p == LIM_LA) p = 0;
        sn += run;
        if (sn == LIM_SEG) {
            sn = 0;
            int si = m->si;
            m->seg[si] = acc > ceil_bits ? 1.0f - ceil / fx_from_bits(acc) : 0.0f;
            acc = 0;
            static const float w[LIM_NSEG] = { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 1.0f };
            float red = 0.0f;
            for (int a = 0; a < LIM_NSEG; a++) {
                int k = si - a; if (k < 0) k += LIM_NSEG;
                float v = m->seg[k] * w[a];
                if (v > red) red = v;
            }
            m->si = si + 1 < LIM_NSEG ? si + 1 : 0;
            float prev = m->sgr;
            if (red < prev) { red = prev + m->s_rel * (red - prev); if (red < 1e-6f) red = 0.0f; }
            m->sgr = red;
            if (red > meter) meter = red;
            float ge = 1.0f - red;
            g = 1.0f - prev;
            gs = (ge - g) * (1.0f / (float)LIM_SEG);
            if (ge == 1.0f && g == 1.0f) gs = 0.0f;
        }
    }
    m->acc_pk = fx_from_bits(acc); m->sg = g; m->sg_step = gs; m->red = meter;
    m->pos_dl = p; m->sn = sn;
}

/* ================================================================= crush ==== */
/* The crush page, ahead of the chain: SP-1FX's bit and sample-rate reducer, then its filter. */

#define CRUSH_DEAD 0.03f    /* AMOUNT below this is off */
#define CRUSH_FLOOR 1e-4f   /* -80 dBFS: quieter than this the crusher outputs silence */
#define FLT_DEAD 0.10f      /* FILTER: half-width of the flat zone around the centre */

/* the step a value falls in: floor(v), held to -top-1 .. top */
static inline float quant(float v, float top)
{
    float f = (float)(int)v;
    if (f > v) f -= 1.0f;
    return f > top ? top : f < -top - 1.0f ? -top - 1.0f : f;
}

static void crush_reset(ms_crush *c)
{
    c->env = c->hold_l = c->hold_r = c->phase = 0.0f;
    for (int ch = 0; ch < 2; ch++) c->st[ch][0] = c->st[ch][1] = (fx_svf_state){0};
}

static void crush_init(ms_crush *c, float sr)
{
    __builtin_memset(c, 0, sizeof *c);
    c->half = 32768.0f;
    c->top = 32767.0f;
    c->out_g = 1.0f / 32767.5f;
    c->rate_ratio = 1.0f;
    c->env_rel = sample_coef(0.012f, sr);   /* lets go fast: quiet sound after a hit keeps its steps */
    c->fpos = 0.5f;
    c->set_t = -1.0f;
}

/* once a chunk, from the smoothed crush page */
STAGE static void crush_control(ms_crush *c, const float *cr, float sr)
{
    /* the reducer */
    float a = cr[MS_CR_AMOUNT];
    c->wet = a < CRUSH_DEAD ? 0.0f : (a - CRUSH_DEAD) / (1.0f - CRUSH_DEAD);
    c->gate_target = c->wet > 0.0f ? 1.0f : 0.0f;
    /* 16 bits at the bottom of BITS down to 1 at the top: 2^bits levels across the signal's own
     * level (see crush_process), so the crunch is the same loud or quiet. The levels sit
     * between the steps (no zero level), so 1 bit is two levels, a square at the music's level;
     * the outer levels reach the level itself. */
    float h = fx_exp2f(15.0f - 15.0f * cr[MS_CR_BITS]);   /* levels / 2: 32768 .. 1 */
    float hc = (float)(int)h;
    if (hc < h) hc += 1.0f;                                /* ceil */
    c->half = h;
    c->top = hc - 1.0f;
    c->out_g = 1.0f / (hc - 0.5f);
    /* 48 kHz at the bottom of RATE down to 2 kHz at the top, evenly in octaves */
    c->rate_ratio = fx_exp2f(-4.585f * cr[MS_CR_RATE]);

    /* the filter, as on SP-1FX: down a low-pass down, up a high-pass up, the middle fifth flat */
    float p = cr[MS_CR_FILTER], t;
    int mode;
    c->fpos = p;
    if (p < 0.5f - FLT_DEAD)      { mode = 1; t = (0.5f - FLT_DEAD - p) / (0.5f - FLT_DEAD); }
    else if (p > 0.5f + FLT_DEAD) { mode = 0; t = (p - 0.5f - FLT_DEAD) / (0.5f - FLT_DEAD); }
    else                          { mode = c->mode; t = 0.0f; }
    if (c->fgate == 0.0f && mode != c->mode) {
        c->mode = mode;
        c->st[0][0] = c->st[0][1] = c->st[1][0] = c->st[1][1] = (fx_svf_state){0};
    }
    c->fgate_target = t > 0.0f && mode == c->mode ? 1.0f : 0.0f;
    if (c->fgate == 0.0f && c->fgate_target == 0.0f) return;
    t = fx_clampf(t, 0.0f, 1.0f);
    if (t == c->set_t && c->mode == c->set_mode) return;   /* parked */
    c->set_t = t;
    c->set_mode = c->mode;
    /* 24 dB/oct, resonance building toward the ends (SP-1FX's page 2 at its middle) */
    float q2 = 1.3066f * (1.0f + 3.6f * t);
    if (c->mode == 0) {           /* high-pass: 20 Hz up to 6 kHz */
        float hz = 20.0f * fx_exp2f(8.23f * t);
        fx_svf_highpass(&c->s1, hz, 0.5412f, sr);
        fx_svf_highpass(&c->s2, hz, q2, sr);
    } else {                      /* low-pass: 20 kHz down to 60 Hz */
        float hz = 20000.0f * fx_exp2f(-8.38f * t);
        fx_svf_lowpass(&c->s1, hz, 0.5412f, sr);
        fx_svf_lowpass(&c->s2, hz, q2, sr);
    }
}

/* a 15 ms on/off fade, per frame, into buf; returns whether anything is fading */
static int gate_ramp(float *g, float gt, float step, float *buf, float scale, int n)
{
    float v = *g;
    if (v == gt && (v == 0.0f || v == 1.0f)) return 0;
    for (int i = 0; i < n; i++) {
        if (v != gt) { v += v < gt ? step : -step; v = fx_clampf(v, 0.0f, 1.0f); }
        buf[i] = v * scale;
    }
    *g = v;
    return 1;
}

STAGE static void crush_process(ms_crush *c, float *l, float *r, int n, float sr)
{
    const float gstep = 1.0f / (0.015f * sr);
    float mix[MS_CHUNK];

    /* the reducer: quantized sample-and-hold, mixed in by AMOUNT */
    if (c->gate != 0.0f || c->gate_target != 0.0f) {
        const float wet = c->wet, half = c->half, top = c->top, og = c->out_g, rr = c->rate_ratio;
        const float rel = c->env_rel;
        const int fading = gate_ramp(&c->gate, c->gate_target, gstep, mix, wet, n);
        float ph = c->phase, env = c->env, hl = c->hold_l, hr = c->hold_r;
        for (int i = 0; i < n; i++) {
            uint32_t ul = fx_abs_bits(l[i]), ur = fx_abs_bits(r[i]);
            float pk = fx_from_bits(ul > ur ? ul : ur);
            env = pk > env ? pk : env + rel * (pk - env);   /* instant attack: |x| <= env always */
            ph += rr;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                /* x / env lies in -1..1: a quiet passage keeps all its steps */
                if (env > CRUSH_FLOOR) {
                    float k = half / env, s = og * env;
                    hl = (quant(l[i] * k, top) + 0.5f) * s;
                    hr = (quant(r[i] * k, top) + 0.5f) * s;
                } else {
                    hl = hr = 0.0f;
                }
            }
            float m = fading ? mix[i] : wet;
            l[i] += m * (hl - l[i]);
            r[i] += m * (hr - r[i]);
        }
        c->phase = ph; c->env = env; c->hold_l = hl; c->hold_r = hr;
        if (c->gate == 0.0f && c->gate_target == 0.0f) c->env = c->hold_l = c->hold_r = c->phase = 0.0f;
    }

    /* the filter */
    if (c->fgate != 0.0f || c->fgate_target != 0.0f) {
        float dry[2][MS_CHUNK];
        const int fading = gate_ramp(&c->fgate, c->fgate_target, gstep, mix, 1.0f, n);
        if (fading) for (int i = 0; i < n; i++) { dry[0][i] = l[i]; dry[1][i] = r[i]; }
        const fx_svf A = c->s1, B = c->s2;
        for (int ch = 0; ch < 2; ch++) {
            float *x = ch ? r : l;
            float a1 = c->st[ch][0].ic1, a2 = c->st[ch][0].ic2, b1 = c->st[ch][1].ic1, b2 = c->st[ch][1].ic2;
            if (c->mode) {
                for (int i = 0; i < n; i++) {
                    float y = fx_svf_lp(A.a1, A.a2, A.a3, &a1, &a2, x[i]);
                    x[i] = fx_svf_lp(B.a1, B.a2, B.a3, &b1, &b2, y);
                }
            } else {
                const float ka = -A.m1, kb = -B.m1;
                for (int i = 0; i < n; i++) {
                    float y = fx_svf_hp(A.a1, A.a2, A.a3, ka, &a1, &a2, x[i]);
                    x[i] = fx_svf_hp(B.a1, B.a2, B.a3, kb, &b1, &b2, y);
                }
            }
            c->st[ch][0].ic1 = a1; c->st[ch][0].ic2 = a2; c->st[ch][1].ic1 = b1; c->st[ch][1].ic2 = b2;
            if (fading) {
                const float *d = dry[ch];
                for (int i = 0; i < n; i++) x[i] = d[i] + mix[i] * (x[i] - d[i]);
            }
        }
    }
}

/* ================================================================ engine ==== */

void ms_init(ms_engine *e, float sr)
{
    __builtin_memset(e, 0, sizeof *e);
    e->sr = sr;
    for (int k = 0; k < MS_SLOTS; k++) { e->param2[k] = e->p[k] = 0.5f; }
    e->crush[MS_CR_BITS] = e->cr[MS_CR_BITS] = 0.5f;
    e->crush[MS_CR_RATE] = e->cr[MS_CR_RATE] = 0.5f;
    e->crush[MS_CR_FILTER] = e->cr[MS_CR_FILTER] = 0.5f;
    crush_init(&e->crs, sr);
    for (int c = 0; c < MS_CHAINS; c++) chain_init(&e->ch[c], c, sr);
    limiter_init(&e->lim, sr);
    e->chain = MS_LONDON;
    e->cur = MS_LONDON;
    e->nxt = -1;
    e->xf_step = 1.0f / (FADE_S * sr);
    e->dip_step = 1.0f / (DIP_S * sr);
}

void ms_set_fader(ms_engine *e, int slot, float pos)
{
    if (slot >= 0 && slot < MS_SLOTS) e->fader[slot] = fx_clampf(pos, 0.0f, 1.0f);
}

void ms_set_param2(ms_engine *e, int slot, float pos)
{
    if (slot >= 0 && slot < MS_SLOTS) e->param2[slot] = fx_clampf(pos, 0.0f, 1.0f);
}

void ms_set_crush(ms_engine *e, int slot, float pos)
{
    if (slot >= 0 && slot < MS_SLOTS) e->crush[slot] = fx_clampf(pos, 0.0f, 1.0f);
}

void ms_set_chain(ms_engine *e, int chain)
{
    if (chain >= 0 && chain < MS_CHAINS) e->chain = chain;
}

void ms_set_bypass(ms_engine *e, int on) { e->bypass = on != 0; }

int ms_playing(const ms_engine *e)
{
    if (e->dip == DIP_OUT) return e->dip_to;
    return e->nxt >= 0 && e->xf > 0.5f ? e->nxt : e->cur;
}

int ms_latency(const ms_engine *e) { (void)e; return LIM_LA; }

float ms_gain_reduction_db(const ms_engine *e) { return e->meter_db; }

void ms_meter_fall(ms_engine *e, float k) { e->meter_db *= k; }

/* Switching. Only one chain ever runs, so the CPU never carries two:
 *   chain to chain: the old one fades out (DIP_S), the new one starts clean and runs muted for
 *     WARM_S so its compressors have caught the music, then fades in (DIP_S);
 *   to or from bypass: a crossfade (FADE_S) with the dry signal, which costs nothing; out of
 *     bypass the chain runs unheard for WARM_S first. */
void ms_process(ms_engine *e, float *l, float *r, int n)
{
    const float sr = e->sr;
    float al[MS_CHUNK], ar[MS_CHUNK];
    while (n > 0) {
        if (e->phase == 0) {
            /* the controls: glide over about 30 ms, settle exactly */
            const float k = step_coef(0.03f, sr, (float)MS_CHUNK);
            for (int s = 0; s < MS_SLOTS; s++) {
                e->f[s] = glide(e->f[s], e->fader[s], k);
                e->p[s] = glide(e->p[s], e->param2[s], k);
                e->cr[s] = glide(e->cr[s], e->crush[s], k);
            }
            crush_control(&e->crs, e->cr, sr);
            /* the limiter's release: 400 ms (down) to 25 ms (up), 100 ms in the middle */
            e->lim.s_rel = step_coef(0.4f * fx_exp2f(-4.0f * e->p[MS_LOUD]), sr, LIM_SEG);

            int want = e->bypass ? MS_DRY : e->chain;
            if (e->nxt < 0 && e->dip == DIP_NONE && want != e->cur) {
                if (e->cur == MS_DRY || want == MS_DRY) {
                    e->nxt = want;
                    e->xf = 0.0f;
                    e->warm = want == MS_DRY ? 0 : (int)(WARM_S * sr / MS_CHUNK);
                    if (want != MS_DRY) { chain_reset(&e->ch[want]); crush_reset(&e->crs); }
                } else {
                    e->dip = DIP_OUT;
                    e->dip_to = want;
                    e->xf = 1.0f;
                }
            }
            if (e->cur != MS_DRY) chain_control(&e->ch[e->cur], e->f, e->p, sr);
            if (e->nxt >= 0 && e->nxt != MS_DRY) chain_control(&e->ch[e->nxt], e->f, e->p, sr);
        }
        int m = MS_CHUNK - e->phase;
        if (m > n) m = n;
        const int ends = e->phase + m == MS_CHUNK;

        if (e->nxt >= 0) {
            /* to or from bypass: at most one chain runs, the other side is the dry input */
            for (int i = 0; i < m; i++) { al[i] = l[i]; ar[i] = r[i]; }
            if (e->nxt != MS_DRY) {
                crush_process(&e->crs, al, ar, m, sr);
                chain_process(&e->ch[e->nxt], al, ar, m, ends);
            } else if (e->cur != MS_DRY) {
                crush_process(&e->crs, l, r, m, sr);
                chain_process(&e->ch[e->cur], l, r, m, ends);
            }
            if (e->warm > 0) {
                if (ends) e->warm--;
            } else {
                float x = e->xf;
                const float xs = e->xf_step;
                for (int i = 0; i < m; i++) {
                    x += xs; if (x > 1.0f) x = 1.0f;
                    l[i] += x * (al[i] - l[i]);
                    r[i] += x * (ar[i] - r[i]);
                }
                e->xf = x;
                if (x >= 1.0f) { e->cur = e->nxt; e->nxt = -1; }
            }
        } else if (e->cur != MS_DRY) {
            crush_process(&e->crs, l, r, m, sr);
            chain_process(&e->ch[e->cur], l, r, m, ends);
            if (e->dip != DIP_NONE) {
                /* chain to chain: out, swap, settle muted, in */
                float x = e->xf;
                const float xs = e->dip == DIP_OUT ? -e->dip_step : e->dip == DIP_IN ? e->dip_step : 0.0f;
                for (int i = 0; i < m; i++) {
                    x += xs; x = fx_clampf(x, 0.0f, 1.0f);
                    l[i] *= x; r[i] *= x;
                }
                e->xf = x;
                if (e->dip == DIP_OUT && x <= 0.0f && ends) {
                    e->cur = e->dip_to;
                    chain_reset(&e->ch[e->cur]);
                    e->warm = (int)(WARM_S * sr / MS_CHUNK);
                    e->dip = DIP_WARM;
                } else if (e->dip == DIP_WARM && ends && --e->warm <= 0) {
                    e->dip = DIP_IN;
                } else if (e->dip == DIP_IN && x >= 1.0f) {
                    e->dip = DIP_NONE;
                }
            }
        }

        e->lim.red = 0.0f;
        limiter_process(&e->lim, l, r, m);
        float gr = e->cur != MS_DRY ? chain_gr(&e->ch[e->cur]) : 0.0f;
        gr += -fx_lin2db(1.0f - e->lim.red + 1e-6f);
        if (gr > e->meter_db) e->meter_db = gr;

        e->phase = (e->phase + m) % MS_CHUNK;
        l += m; r += m; n -= m;
    }
}
