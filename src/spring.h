/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The three-spring reverberation unit (Z-3F in the service notes; its springs run in a "Z"),
 * driven by its power amplifier IC and recovered by a one-transistor amplifier (service
 * notes: reverberation unit, drive amp IC1, return amp Q10).
 *
 * Each spring is a delay loop with a chain of first-order allpass filters inside it. The
 * allpasses delay low frequencies more than high ones, as a helical spring does, so every
 * round trip comes back as a falling chirp and successive trips smear into the familiar
 * metallic "drip". The spring dimensions are not in the service notes and no published decay
 * figure was found: the delays, decay time and band limits below are estimates in the range of
 * short three-spring tanks. */
#ifndef AL_SPRING_H
#define AL_SPRING_H

#include <stdlib.h>
#include <string.h>

#include "dsp.h"

#define SPRING_STAGES 24
#define SPRING_MAX_SECONDS 0.06

typedef struct {
    float* buf;
    unsigned mask, w, delay;
    Allpass1 ap[SPRING_STAGES];
    double lp_a, lp_z; /* one-pole lowpass in the loop: steel loses highs on each trip */
    double g;          /* loop gain */
} Spring;

typedef struct {
    Spring s[3];
    Biquad in_hp, out_lp, out_hp;
    double drive;      /* gain into the drive IC */
} SpringTank;

/* Round-trip delays (s), allpass break frequencies (Hz) and decay time: estimates. */
static const double SPRING_DELAY[3] = { 0.0331, 0.0373, 0.0413 };
static const double SPRING_AP_HZ[3] = { 1450.0, 1300.0, 1150.0 };
#define SPRING_RT60 2.4

static int spring_init(SpringTank* t, double fs)
{
    int k, i;
    for (k = 0; k < 3; ++k) {
        Spring* s = &t->s[k];
        unsigned n = 256;
        while (n < (unsigned)(SPRING_MAX_SECONDS * fs) + 4)
            n <<= 1;
        s->buf = (float*)calloc(n, sizeof(float));
        if (!s->buf)
            return 0;
        s->mask = n - 1;
        s->w = 0;
        /* The allpass chain adds about SPRING_STAGES * (1 - a) / (1 + a) samples at low
         * frequencies; take that off the plain delay so the round trip stays as set. */
        {
            const double a = ap1_coef(SPRING_AP_HZ[k], fs);
            const double ap_delay = SPRING_STAGES * (1.0 - a) / (1.0 + a);
            double d = SPRING_DELAY[k] * fs - ap_delay;
            if (d < 2.0)
                d = 2.0;
            s->delay = (unsigned)d;
            for (i = 0; i < SPRING_STAGES; ++i)
                s->ap[i].a = a;
        }
        s->lp_a = smooth_coef(1.0 / (2.0 * AL_PI * 4200.0), fs);
        s->g = pow(10.0, -3.0 * SPRING_DELAY[k] / SPRING_RT60);
    }
    bq_highpass(&t->in_hp, 180.0, 0.7071, fs);
    bq_lowpass(&t->out_lp, 4800.0, 0.7071, fs);
    bq_highpass(&t->out_hp, 120.0, 0.7071, fs);
    return 1;
}

static void spring_reset(SpringTank* t)
{
    int k, i;
    for (k = 0; k < 3; ++k) {
        Spring* s = &t->s[k];
        memset(s->buf, 0, (s->mask + 1) * sizeof(float));
        s->w = 0;
        s->lp_z = 0.0;
        for (i = 0; i < SPRING_STAGES; ++i)
            s->ap[i].z = 0.0;
    }
    bq_reset(&t->in_hp);
    bq_reset(&t->out_lp);
    bq_reset(&t->out_hp);
}

static void spring_free(SpringTank* t)
{
    int k;
    for (k = 0; k < 3; ++k) {
        free(t->s[k].buf);
        t->s[k].buf = NULL;
    }
}

static inline double spring_one(Spring* s, double x)
{
    double y = s->buf[(s->w - s->delay) & s->mask];
    int i;
    for (i = 0; i < SPRING_STAGES; ++i)
        y = ap1_run(&s->ap[i], y);
    s->lp_z += s->lp_a * (y - s->lp_z);
    y = s->lp_z;
    s->buf[s->w] = (float)(x + s->g * y + 1e-18); /* the tiny offset keeps the loop out of denormals */
    s->w = (s->w + 1) & s->mask;
    return y;
}

/* x: the signal at the drive amp's input. Returns the return amp's output. */
static inline double spring_run(SpringTank* t, double x)
{
    /* The drive IC (TA7200P) swings about 7 V p-p into the transducer: it clips gently. */
    const double d = soft_limit(bq_run(&t->in_hp, x), 1.0);
    const double y = 0.6 * (spring_one(&t->s[0], d) - spring_one(&t->s[1], d) + 0.85 * spring_one(&t->s[2], d));
    return bq_run(&t->out_hp, bq_run(&t->out_lp, y));
}

#endif
