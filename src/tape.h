/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The tape transport: an endless tape loop passing a record head and three playback heads,
 * pulled by a capstan whose speed the repeat rate control sets (12 to 40 cm/s, from the
 * service notes' reference data).
 *
 * The model keeps the tape position (in cm) of every recorded sample. A playback head that is
 * d cm downstream of the record head reads the sample that was recorded when the tape was d cm
 * further back. So the delay is not d / speed at one instant: it follows from how far the tape
 * actually travelled, which gives the right pitch glide when the speed changes and the right
 * size of wow and flutter (the pitch moves by the speed difference between recording and
 * playback, not by the full delay change). */
#ifndef AL_TAPE_H
#define AL_TAPE_H

#include <stdlib.h>

#include "dsp.h"

/* The service notes give the speed range only roughly: "Tape Speed ... 12cm - 40cm/sec
 * (approx)". Head spacing is not in them. Published measurements of real units put head 1 at
 * about 69-177 ms and heads 2 and 3 at about 1.94x and 2.85x head 1 (two commercial
 * emulations' manuals, Universal Audio's and Cherry Audio's; the latter gives 500 ms at
 * head 3). Taking the 40 cm/s top speed from the service notes, head 1 is 2.76 cm from the
 * record head, and 177 ms then needs a slowest speed of 15.6 cm/s, inside the notes'
 * approximate range. */
#define TAPE_SPEED_MIN 15.6
#define TAPE_SPEED_MAX 40.0

static const double HEAD_CM[3] = { 2.76, 2.76 * 1.94, 2.76 * 2.85 };

/* Longest delay the buffer must hold (head 3 at the slowest speed is about 0.50 s), with room
 * for wow. */
#define TAPE_MAX_SECONDS 0.75

typedef struct {
    float* sig;     /* what is on the tape at each sample index */
    double* pos;    /* tape position (cm) at which that sample was recorded */
    unsigned len;   /* power of two */
    unsigned mask;
    unsigned w;     /* next write index */
    double now;     /* tape position under the record head */
    unsigned head[3]; /* index of the newest sample at or before each head's read position */
} Tape;

static inline int tape_init(Tape* t, double fs)
{
    unsigned n = 1024;
    while (n < (unsigned)(TAPE_MAX_SECONDS * fs) + 64)
        n <<= 1;
    t->sig = (float*)calloc(n, sizeof(float));
    t->pos = (double*)calloc(n, sizeof(double));
    t->len = n;
    t->mask = n - 1;
    return t->sig && t->pos;
}

static inline void tape_free(Tape* t)
{
    free(t->sig);
    free(t->pos);
    t->sig = NULL;
    t->pos = NULL;
}

/* Blank tape that has been running at the given speed. */
static inline void tape_reset(Tape* t, double cm_per_sample)
{
    unsigned i;
    t->w = 0;
    t->now = 0.0;
    for (i = 0; i < t->len; ++i) {
        t->sig[i] = 0.0f;
        /* index len - k holds the sample from k samples ago */
        t->pos[(t->len - i) & t->mask] = -(double)i * cm_per_sample;
    }
    t->pos[0] = 0.0;
    for (i = 0; i < 3; ++i) {
        unsigned back = (unsigned)(HEAD_CM[i] / cm_per_sample) + 1;
        if (back > t->len - 8)
            back = t->len - 8;
        t->head[i] = (0u - back) & t->mask;
    }
}

/* Moves the tape on by one sample: the record head is now at a position cm further on. */
static inline void tape_advance(Tape* t, double cm)
{
    t->now += cm;
}

/* Writes the record head's sample at the current position. */
static inline void tape_write(Tape* t, double x)
{
    t->sig[t->w] = (float)x;
    t->pos[t->w] = t->now;
    t->w = (t->w + 1) & t->mask;
}

/* Playback head k's output: the tape at position now - HEAD_CM[k], with four-point
 * (Catmull-Rom) interpolation between recorded samples. */
static inline double tape_read(Tape* t, int k)
{
    const double target = t->now - HEAD_CM[k];
    unsigned i = t->head[k];
    const unsigned newest = (t->w - 1) & t->mask, oldest = (t->w + 2) & t->mask;
    double p0, p1, f, xm1, x0, x1, x2;
    /* The head moves forward along the recorded samples; it never needs to go back far. */
    while (i != newest && t->pos[(i + 1) & t->mask] <= target)
        i = (i + 1) & t->mask;
    while (i != oldest && t->pos[i] > target)
        i = (i - 1) & t->mask;
    t->head[k] = i;
    p0 = t->pos[i];
    p1 = t->pos[(i + 1) & t->mask];
    f = p1 > p0 ? (target - p0) / (p1 - p0) : 0.0;
    f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    xm1 = t->sig[(i - 1) & t->mask];
    x0 = t->sig[i];
    x1 = t->sig[(i + 1) & t->mask];
    x2 = t->sig[(i + 2) & t->mask];
    return x0 + 0.5 * f * (x1 - xm1 + f * (2.0 * xm1 - 5.0 * x0 + 4.0 * x1 - x2 + f * (3.0 * (x0 - x1) + x2 - xm1)));
}

#endif
