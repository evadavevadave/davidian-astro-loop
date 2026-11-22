/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The three-spring reverberation unit, driven by its power amplifier IC and recovered by a
 * one-transistor amplifier (service notes: reverberation unit Z-3F, drive amp IC1, return amp
 * Q10).
 *
 * The tank is the same part, the Z-3F, that the Davidian Jade Choir (the two-channel chorus combo)
 * models, and the springs here are that model's: the Z-3F's listed replacement is a short
 * (9-1/4") three-spring type 8 tank with medium decay (1.75-3.0 s). Each spring is a delay line
 * that the drive coil writes into. The pickup coil hears it one transit later; the wave reflects
 * and comes back round after two transits, through stretched first-order allpasses (the
 * dispersion that turns each return into a falling chirp), a loop lowpass (steel loses highs on
 * each trip) and a DC block. Neighbouring springs are read in opposite polarity, as the coil
 * magnets alternate. Transit times, decay (the middle of the medium range) and the loop's band
 * limits are estimates shared with the Jade Choir (see README). */
#ifndef AL_SPRING_H
#define AL_SPRING_H

#include <stdlib.h>
#include <string.h>

#include "dsp.h"

#define TANK_SPRINGS 3
#define TANK_AP 24          /* allpass stages per spring */
#define TANK_AP_COEF 0.62   /* allpass coefficient, at 24 kHz stretch spacing */
#define TANK_T60 2.375      /* seconds */
#define TANK_LP_HZ 4500.0   /* loop lowpass */
#define TANK_MAX_STRETCH 4
static const double TANK_TRANSIT[TANK_SPRINGS] = { 0.0295, 0.0337, 0.0371 }; /* seconds, one way */

/* Level from the drive amp's input to the return amp's output, set so the reverb is as loud as
 * this plugin's earlier tank at the same REVERB setting (see test_dsp). */
#define TANK_GAIN 1.74

typedef struct {
    float* buf;
    unsigned mask, w, d1, d2;
    /* Stretched allpasses H(z) = (a + z^-k) / (1 + a z^-k), as k interleaved first-order
     * sections, so the dispersion stays the same at every sample rate. */
    double ap_x[TANK_AP][TANK_MAX_STRETCH], ap_y[TANK_AP][TANK_MAX_STRETCH];
    int stretch, ph;
    double g, lp, lp_k, dc_x, dc_y, norm;
} Spring;

typedef struct {
    Spring s[TANK_SPRINGS];
    Biquad in_hp, out_lp, out_hp;
} SpringTank;

static int spring_init(SpringTank* t, double fs)
{
    int i;
    for (i = 0; i < TANK_SPRINGS; ++i) {
        Spring* s = &t->s[i];
        const unsigned need = (unsigned)(2.0 * TANK_TRANSIT[i] * fs) + 8;
        unsigned n = 256;
        double loop;
        memset(s, 0, sizeof *s);
        while (n < need)
            n <<= 1;
        s->buf = (float*)calloc(n, sizeof(float));
        if (!s->buf)
            return 0;
        s->mask = n - 1;
        s->d1 = (unsigned)(TANK_TRANSIT[i] * fs);
        s->d2 = 2 * s->d1;
        s->stretch = (int)(fs / 24000.0 + 0.5);
        if (s->stretch < 1)
            s->stretch = 1;
        if (s->stretch > TANK_MAX_STRETCH)
            s->stretch = TANK_MAX_STRETCH;
        /* The loop time includes the allpasses' low-frequency group delay. */
        loop = s->d2 / fs + TANK_AP * s->stretch * (1.0 - TANK_AP_COEF) / (1.0 + TANK_AP_COEF) / fs;
        s->g = pow(10.0, -3.0 * loop / TANK_T60);
        s->lp_k = 1.0 - exp(-2.0 * AL_PI * TANK_LP_HZ / fs);
        s->norm = sqrt(1.0 - s->g * s->g) / sqrt((double)TANK_SPRINGS);
    }
    /* The drive and return amps' coupling and the transducers' band limits (estimates). */
    bq_highpass(&t->in_hp, 180.0, 0.7071, fs);
    bq_lowpass(&t->out_lp, 4800.0, 0.7071, fs);
    bq_highpass(&t->out_hp, 120.0, 0.7071, fs);
    return 1;
}

static void spring_reset(SpringTank* t)
{
    int i;
    for (i = 0; i < TANK_SPRINGS; ++i) {
        Spring* s = &t->s[i];
        memset(s->buf, 0, (s->mask + 1) * sizeof(float));
        memset(s->ap_x, 0, sizeof s->ap_x);
        memset(s->ap_y, 0, sizeof s->ap_y);
        s->w = 0;
        s->ph = 0;
        s->lp = s->dc_x = s->dc_y = 0.0;
    }
    bq_reset(&t->in_hp);
    bq_reset(&t->out_lp);
    bq_reset(&t->out_hp);
}

static void spring_free(SpringTank* t)
{
    int i;
    for (i = 0; i < TANK_SPRINGS; ++i) {
        free(t->s[i].buf);
        t->s[i].buf = NULL;
    }
}

static inline double tank_tick(SpringTank* t, double x)
{
    double out = 0.0;
    int i, j;
    for (i = 0; i < TANK_SPRINGS; ++i) {
        Spring* s = &t->s[i];
        const int k = s->ph;
        double fb = s->buf[(s->w - s->d2) & s->mask], y;
        for (j = 0; j < TANK_AP; ++j) {
            const double yy = TANK_AP_COEF * (fb - s->ap_y[j][k]) + s->ap_x[j][k];
            s->ap_x[j][k] = fb;
            s->ap_y[j][k] = yy;
            fb = yy;
        }
        s->ph = (s->ph + 1) % s->stretch;
        s->lp += s->lp_k * (fb - s->lp);
        y = s->lp - s->dc_x + 0.995 * s->dc_y;
        s->dc_x = s->lp;
        s->dc_y = y + 1e-18; /* the tiny offset keeps the loop out of denormals */
        s->buf[s->w] = (float)(x + s->g * y);
        out += (i & 1 ? -1.0 : 1.0) * s->norm * s->buf[(s->w - s->d1) & s->mask];
        s->w = (s->w + 1) & s->mask;
    }
    return out;
}

/* x: the signal at the drive amp's input. Returns the return amp's output. */
static inline double spring_run(SpringTank* t, double x)
{
    /* The drive IC (TA7200P) swings about 7 V p-p into the transducer: it clips gently. */
    const double d = soft_limit(bq_run(&t->in_hp, x), 1.0);
    const double y = TANK_GAIN * tank_tick(t, d);
    return bq_run(&t->out_hp, bq_run(&t->out_lp, y));
}

#endif
