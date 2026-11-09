/* SP-1 Master: a mastering processor engine. Four mastering chains, one per TRACK button; the
 * four faders set the same four stages in every chain, each with a second parameter on page 2.
 *
 *   chain 1  LONDON   1960s studio master: warm shelves, a slow tube compressor, console
 *                     transformer saturation.
 *   chain 2  PUNCH    New York style punch: a big low end with the dip above it, a fast bus
 *                     compressor, parallel compression, a soft clipper before the limiter.
 *   chain 3  CLEAN    modern streaming master: broad tilt EQ, a three-band compressor, a
 *                     saturating air band, a transparent limiter.
 *   chain 4  LACQUER  vinyl cutting room: a warm cutting curve, a gentle tube compressor, mono
 *                     bass (the elliptical EQ), a high-frequency limiter that tames sibilance.
 *
 *   fader 1  TONE     the chain's EQ curve, from flat (down) to full (up).  Page 2: tilt.
 *   fader 2  GLUE     the chain's compressor, from off (down) to heavy.    Page 2: speed.
 *   fader 3  COLOUR   the chain's character stage, from off (down).       Page 2: stereo width.
 *   fader 4  LOUDNESS drive into the limiter, 0 to +12 dB.                 Page 2: limiter release.
 *
 * After the chain, a look-ahead limiter (-1 dBFS ceiling, 1 ms look-ahead) is always on. With
 * every fader down and every page 2 parameter centred, a chain is flat: the output is the input,
 * delayed by ms_latency(), unless it goes over the ceiling.
 *
 * Only one chain runs at a time. Switching chains dips: the old chain fades out in 15 ms, the
 * new one starts clean and runs muted for 30 ms (so its compressors have caught the music),
 * then fades in over 15 ms. Bypass crossfades over 50 ms with the dry signal, which is the
 * input through the same limiter, at the same latency.
 *
 * Float, stereo, any block size; parameters move every MS_CHUNK frames counted from the start,
 * so the output does not depend on how the audio is cut into blocks. No libm, no doubles, no
 * heap: an ms_engine should be a static. */
#ifndef SP1_MASTER_H
#define SP1_MASTER_H

#include <stdint.h>
#include "fx_math.h"

#define MS_CHAINS 4
#define MS_SLOTS 4
#define MS_CHUNK 64   /* frames (48 kHz) between parameter updates: 1.3 ms */
#define MS_DRY MS_CHAINS   /* bypass, as a chain index */

enum { MS_LONDON, MS_PUNCH, MS_CLEAN, MS_LACQUER };
enum { MS_TONE, MS_GLUE, MS_COLOUR, MS_LOUD };

/* a stereo-linked compressor: a peak detector every frame, the gain every eighth frame (glided
 * between). Gain reduction is smoothed in dB with an attack and a release; with c_chg > 0 a slow
 * "memory" part follows it, for a program-dependent (auto) release. */
typedef struct {
    float thr, slope, knee, makeup;   /* dB; slope = 1 - 1/ratio */
    float det_att, det_rel;           /* the detector, per frame */
    float c_att, c_rel, c_chg, c_mem; /* the reduction, per 8 frames */
    float sc_c;                       /* sidechain high-pass (one-pole), 0 = none */
    float fade;                       /* how much of the reduction is applied, 0..1 */
    float det, gf, gs, g, dg;
    float sc[2];
    int ph, active;
    float gr;                         /* reduction now, dB */
} ms_comp;

typedef struct {
    int id;
    float set[MS_SLOTS * 2];          /* the settings the coefficients were last made for */

    /* 1: tone */
    int eq_on;
    fx_bq eq[3];
    fx_bq_state eqs[2][3];

    /* 2: glue (Clean: three bands, split by xo[0] and xo[1]) */
    ms_comp comp[3];
    fx_bq xo[2];
    fx_bq_state xos[2][2];

    /* 3: colour */
    int col_on;
    float drive, inv_drive, sat_mix, bias, bias_out, sat_trim;   /* London: saturation */
    float dc_x[2], dc_y[2], dc_r;
    ms_comp par;                      /* Punch: the parallel compressor */
    float par_mix, par_trim;
    fx_bq air;                        /* Clean: the air band's high-pass */
    fx_bq_state airs[2];
    float air_mix;
    fx_bq side_lp;                    /* Lacquer: the side's high-pass */
    fx_bq_state sides;
    float side_mix;
    float width;                      /* page 2, every chain: side gain, 0 (mono) .. 1 .. 1.6 */

    /* 4: loudness */
    float drive_g, drive_now, drive_step;   /* linear: target, now, step per frame */
    int phase_end;                    /* this call ends a chunk */
    float clip_mix;                   /* Punch: the soft clipper */
    fx_bq hf_hp;                      /* Lacquer: the high-frequency limiter's split */
    fx_bq_state hfs[2];
    float hf_det, hf_att, hf_rel, hf_thr, hf_g, hf_dg;
    int hf_ph;
} ms_chain;

#define LIM_LA 48      /* look-ahead: 1 ms, and its latency */
#define LIM_SEG 8
#define LIM_NSEG (LIM_LA / LIM_SEG)

typedef struct {
    float ceiling;
    float dl[2][LIM_LA];
    float seg[LIM_NSEG];
    float acc_pk;
    int si, sn, pos_dl;
    float sgr, s_rel;
    float sg, sg_step;
    float red;                /* deepest reduction this chunk, 0..1 (1 - gain) */
} ms_limiter;

typedef struct {
    float sr;
    int phase;                    /* frames into the current chunk */
    float fader[MS_SLOTS];        /* page 1 targets, 0..1 */
    float param2[MS_SLOTS];       /* page 2 targets, 0..1 */
    float f[MS_SLOTS], p[MS_SLOTS];   /* the same, smoothed */
    int chain;                    /* the selected chain */
    int bypass;
    int cur, nxt;                 /* playing, and crossfading in (-1: none); MS_DRY = bypass */
    int warm;                     /* chunks the incoming chain still runs unheard */
    float xf, xf_step;            /* the bypass crossfade, 0 (cur) .. 1 (nxt); the dip's gain */
    int dip, dip_to;              /* chain to chain: the stage of the dip, and where to */
    float dip_step;
    ms_chain ch[MS_CHAINS];
    ms_limiter lim;
    float meter_db;               /* deepest total reduction since the last read */
} ms_engine;

void ms_init(ms_engine *e, float sr);
void ms_set_fader(ms_engine *e, int slot, float pos);    /* page 1, 0..1 */
void ms_set_param2(ms_engine *e, int slot, float pos);   /* page 2, 0..1 */
void ms_set_chain(ms_engine *e, int chain);              /* 0..3 */
void ms_set_bypass(ms_engine *e, int on);
int ms_playing(const ms_engine *e);                      /* the chain heard now, or MS_DRY */
void ms_process(ms_engine *e, float *l, float *r, int n); /* in place */
int ms_latency(const ms_engine *e);                       /* frames */
float ms_gain_reduction_db(const ms_engine *e);           /* compressor + limiter, from the meter */
void ms_meter_fall(ms_engine *e, float k);                /* the meter falls back: *= k */

#endif
