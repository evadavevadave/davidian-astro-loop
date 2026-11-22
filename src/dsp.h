/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Small filter building blocks used by the tape and spring models. Everything runs in double
 * precision. Biquads use the transposed direct form II and the well-known bilinear-transform
 * designs (lowpass, highpass, peaking and shelving sections). */
#ifndef AL_DSP_H
#define AL_DSP_H

#include <math.h>

#define AL_PI 3.14159265358979323846

typedef struct {
    double b0, b1, b2, a1, a2; /* a0 normalised to 1 */
    double z1, z2;
} Biquad;

static inline double bq_run(Biquad* f, double x)
{
    const double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

static inline void bq_reset(Biquad* f) { f->z1 = f->z2 = 0.0; }

static void bq_set(Biquad* f, double b0, double b1, double b2, double a0, double a1, double a2)
{
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

static double bq_w0(double f, double fs)
{
    if (f > 0.45 * fs)
        f = 0.45 * fs;
    return 2.0 * AL_PI * f / fs;
}

static void bq_lowpass(Biquad* f, double fc, double q, double fs)
{
    const double w = bq_w0(fc, fs), c = cos(w), al = sin(w) / (2.0 * q);
    bq_set(f, 0.5 * (1 - c), 1 - c, 0.5 * (1 - c), 1 + al, -2 * c, 1 - al);
}

static void bq_highpass(Biquad* f, double fc, double q, double fs)
{
    const double w = bq_w0(fc, fs), c = cos(w), al = sin(w) / (2.0 * q);
    bq_set(f, 0.5 * (1 + c), -(1 + c), 0.5 * (1 + c), 1 + al, -2 * c, 1 - al);
}

static void bq_peak(Biquad* f, double fc, double q, double db, double fs)
{
    const double A = pow(10.0, db / 40.0), w = bq_w0(fc, fs), c = cos(w), al = sin(w) / (2.0 * q);
    bq_set(f, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

/* Shelves with slope 1 (the gentlest without overshoot). */
static void bq_lowshelf(Biquad* f, double fc, double db, double fs)
{
    const double A = pow(10.0, db / 40.0), w = bq_w0(fc, fs), c = cos(w), s = sin(w);
    const double al = s / 2.0 * sqrt(2.0), k = 2.0 * sqrt(A) * al;
    bq_set(f, A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
           (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
}

static void bq_highshelf(Biquad* f, double fc, double db, double fs)
{
    const double A = pow(10.0, db / 40.0), w = bq_w0(fc, fs), c = cos(w), s = sin(w);
    const double al = s / 2.0 * sqrt(2.0), k = 2.0 * sqrt(A) * al;
    bq_set(f, A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
           (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
}

/* Magnitude of a biquad at frequency f, for the tests. */
static double bq_mag(const Biquad* b, double f, double fs)
{
    const double w = 2.0 * AL_PI * f / fs;
    const double cr = cos(w), ci = -sin(w), c2r = cos(2 * w), c2i = -sin(2 * w);
    const double nr = b->b0 + b->b1 * cr + b->b2 * c2r, ni = b->b1 * ci + b->b2 * c2i;
    const double dr = 1.0 + b->a1 * cr + b->a2 * c2r, di = b->a1 * ci + b->a2 * c2i;
    return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

/* One-pole smoother: y moves toward the target with time constant tau. */
static inline double smooth_coef(double tau, double fs) { return 1.0 - exp(-1.0 / (tau * fs)); }

/* First-order allpass: a_k = (tan(pi f / fs) - 1) / (tan(pi f / fs) + 1). */
typedef struct { double a, z; } Allpass1;

static inline double ap1_run(Allpass1* p, double x)
{
    const double y = p->a * x + p->z;
    p->z = x - p->a * y;
    return y;
}

static double ap1_coef(double f, double fs)
{
    const double t = tan(AL_PI * (f > 0.45 * fs ? 0.45 * fs : f) / fs);
    return (t - 1.0) / (t + 1.0);
}

/* sin(2 pi p) for a phase p in 0..1, without a library call: folded to a quarter cycle and a
 * Taylor series to the 9th power, accurate to about 4e-6. For the wow oscillators, whose
 * whole effect is a few hundredths of a percent of tape speed. */
static inline double sin_cycle(double p)
{
    double x = p < 0.5 ? p : p - 1.0; /* -0.5..0.5 */
    double z, z2;
    if (x > 0.25)
        x = 0.5 - x;
    else if (x < -0.25)
        x = -0.5 - x;
    z = 2.0 * AL_PI * x;
    z2 = z * z;
    return z * (1.0 - z2 / 6.0 * (1.0 - z2 / 20.0 * (1.0 - z2 / 42.0 * (1.0 - z2 / 72.0))));
}

/* Linear up to the knee, then rounds off smoothly toward the ceiling. */
static inline double soft_limit(double v, double ceiling)
{
    const double knee = 0.8 * ceiling, room = ceiling - knee;
    if (v > knee)
        return knee + room * tanh((v - knee) / room);
    if (v < -knee)
        return -knee - room * tanh((-v - knee) / room);
    return v;
}

/* Deterministic white noise in -1..1 (xorshift32). */
static inline double rng_next(unsigned* s)
{
    unsigned x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (double)x * (2.0 / 4294967295.0) - 1.0;
}

#endif
