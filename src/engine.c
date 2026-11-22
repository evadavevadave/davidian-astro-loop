/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The Astro-Loop signal path, following the block diagram and circuit diagram in the service
 * notes of the classic three-head tape echo with spring reverb (echo board, miniature board,
 * filter board):
 *
 *   input volume -> mic amp -> (+ intensity feedback) -> recording pre-amp -> recording amp
 *     -> record head ...tape... playback heads 1-3 -> mode selector -> playback pre-amp
 *     -> echo volume -> bass/treble -> buffer amp -> output
 *   the recording pre-amp input also feeds the reverb drive amp -> springs -> reverb amp
 *     -> mode selector (reverb on in positions 5-12) -> reverb volume -> buffer amp
 *   the input signal also goes straight to the buffer amp (the dry sound)
 *
 * The intensity control taps the playback pre-amp ahead of the echo volume and tone controls,
 * so those two do not change the regeneration. The unit is mono: the stereo inputs are summed
 * for the echo and reverb, the dry signal keeps its stereo image, and the wet signal goes to
 * both outputs. Figures that are not in the service notes are marked as estimates, with the
 * published sources used to check them. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dsp.h"
#include "engine.h"
#include "spring.h"
#include "tape.h"

/* ---- Calibration ---- */

#define CTRL_TAU 0.02     /* control smoothing (s) */
#define COEF_EVERY 16     /* samples between updates of the speed-dependent filters */

/* Capstan motor ballistics: time constants measured on a unit by Downing and Terjesen
 * (University of Rochester, 2016): 1.89 s when the delay gets longer, 1.07 s when shorter. */
#define MOTOR_TAU_SLOWER 1.89
#define MOTOR_TAU_FASTER 1.07

/* Record level: a sine of this peak amplitude at the record head reads 0 VU (estimate). With
 * the input volume at 5, an input peaking at -12 dBFS records at 0 VU. */
#define REC_NOMINAL 0.25
/* Tape saturation follows an error-function curve (as Downing and Terjesen measured); this is
 * where it levels off. Estimate. */
#define TAPE_SAT 1.0

/* Recording pre-emphasis and playback de-emphasis (record amp Q4 network; estimate). The tape
 * saturates after the pre-emphasis, so loud treble compresses first, as on tape. */
#define EMPH_HZ 2500.0
#define EMPH_DB 8.0

/* Playback high-frequency limit at top speed, falling with speed (spacing and gap losses), and
 * the low-frequency head bump at a wavelength of 2.5 mm. Estimates. */
#define GAP_HZ_TOP 7000.0
#define BUMP_CM 0.25
#define BUMP_DB 2.0
#define LOOP_HP_HZ 90.0

/* Intensity at full turn gives this loop gain: with one head, repeats run away at about 7 on
 * the dial, which is where the service notes' intensity adjustment (VR12) sets it. */
#define INTENSITY_MAX 1.4

/* Tone controls: the service notes give "Treble ... +/-10dB at 5KHz, Bass ... +/-10dB at
 * 100Hz". The shelf frequencies sit either side of the 750 Hz crossover Downing and Terjesen
 * found for the Baxandall stage; the shelf gains are set so the notes' figures come out exactly
 * (checked by test_dsp). */
#define BASS_HZ 250.0
#define TREBLE_HZ 2200.0
#define BASS_DB 10.32
#define TREBLE_DB 10.42

/* Wow and flutter at full turn, as fractions of tape speed. Mostly slow drift below about
 * 1 Hz (Downing and Terjesen), plus the pinch roller and capstan once-per-turn. Estimates. */
#define WOW_DRIFT 0.0035
#define WOW_ROLLER 0.0008
#define WOW_CAPSTAN 0.0004
#define ROLLER_CM 5.0  /* pinch roller circumference (estimate) */
#define CAPSTAN_CM 1.5 /* capstan circumference (estimate) */

/* ---- Engine ---- */

struct AlEngine {
    double fs;
    float* port[PORT_COUNT];
    Tape tape;
    SpringTank tank;

    double speed; /* cm/s */
    double k_ctrl, k_bypass, k_slower, k_faster, k_drift, drift_norm, k_vu;
    double inv_fs, k_roller, k_capstan; /* 1 / fs, and cm of tape to turns of the pinch roller and capstan per sample */
    double in_g, rec_on, intensity, echo_g, rev_g, out_g, mix, bass, treble;
    double head_g[3], rev_on;
    double ph_roller, ph_capstan, drift1, drift2;
    unsigned rng;
    int coef_count;
    double vu;
    /* The values the speed and tone filters were last designed for: they are only redesigned
     * when one of these changes, so a settled motor and still knobs cost nothing. */
    double designed_speed, designed_age, designed_bass, designed_treble;

    Biquad rec_hp, pre, de, gap, bump, loop_hp, bass_f, treble_f;
};

static float ctl(const AlEngine* e, int port, float def)
{
    const float* p = e->port[port];
    return p && isfinite(*p) ? *p : def;
}

static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

/* Audio-taper pots (VR3, VR6, VR9 behave like this in use; estimate). */
static double taper(double n) { n = clamp01(n); return n * n; }

static double target_speed(const AlEngine* e)
{
    /* VR4 sets the motor servo; speed is taken as linear in rotation (estimate). */
    return TAPE_SPEED_MIN + (TAPE_SPEED_MAX - TAPE_SPEED_MIN) * clamp01(ctl(e, PORT_RATE, 0.5f));
}

static void update_speed_filters(AlEngine* e, double age)
{
    double gap;
    if (e->speed == e->designed_speed && age == e->designed_age)
        return;
    e->designed_speed = e->speed;
    e->designed_age = age;
    gap = GAP_HZ_TOP * pow(e->speed / TAPE_SPEED_MAX, 0.75) * (1.0 - 0.4 * clamp01(age));
    bq_lowpass(&e->gap, gap, 0.65, e->fs);
    bq_peak(&e->bump, e->speed / BUMP_CM, 1.0, BUMP_DB, e->fs);
}

static void update_tone(AlEngine* e)
{
    if (e->bass == e->designed_bass && e->treble == e->designed_treble)
        return;
    e->designed_bass = e->bass;
    e->designed_treble = e->treble;
    bq_lowshelf(&e->bass_f, BASS_HZ, (2.0 * e->bass - 1.0) * BASS_DB, e->fs);
    bq_highshelf(&e->treble_f, TREBLE_HZ, (2.0 * e->treble - 1.0) * TREBLE_DB, e->fs);
}

AlEngine* al_engine_new(double fs)
{
    AlEngine* e = (AlEngine*)calloc(1, sizeof(AlEngine));
    if (!e)
        return NULL;
    e->fs = fs;
    if (!tape_init(&e->tape, fs) || !spring_init(&e->tank, fs)) {
        al_engine_free(e);
        return NULL;
    }
    e->k_ctrl = smooth_coef(CTRL_TAU, fs);
    e->k_bypass = 1.0 / (0.02 * fs);
    e->k_slower = smooth_coef(MOTOR_TAU_SLOWER, fs);
    e->k_faster = smooth_coef(MOTOR_TAU_FASTER, fs);
    e->k_drift = smooth_coef(1.0 / (2.0 * AL_PI * 1.0), fs); /* 1 Hz */
    /* Two one-pole stages of uniform noise: scale to unit standard deviation. */
    e->drift_norm = 1.0 / sqrt((1.0 / 3.0) * e->k_drift / 4.0);
    e->k_vu = smooth_coef(0.3 / 2.2, fs); /* VU ballistics: 300 ms to 99% */
    e->inv_fs = 1.0 / fs;
    e->k_roller = 1.0 / (ROLLER_CM * fs);
    e->k_capstan = 1.0 / (CAPSTAN_CM * fs);
    bq_highpass(&e->rec_hp, 40.0, 0.5, fs);
    bq_highshelf(&e->pre, EMPH_HZ, EMPH_DB, fs);
    bq_highshelf(&e->de, EMPH_HZ, -EMPH_DB, fs);
    bq_highpass(&e->loop_hp, LOOP_HP_HZ, 0.6, fs);
    e->speed = TAPE_SPEED_MAX;
    update_speed_filters(e, 0.0);
    e->bass = e->treble = 0.5;
    update_tone(e);
    return e;
}

void al_engine_connect(AlEngine* e, uint32_t port, void* data)
{
    if (port < PORT_COUNT)
        e->port[port] = (float*)data;
}

static void mode_targets(const AlEngine* e, double* heads, double* rev)
{
    int m = (int)floorf(ctl(e, PORT_MODE, 4.0f) + 0.5f), k;
    unsigned bits;
    m = m < 1 ? 1 : (m > MODE_COUNT ? MODE_COUNT : m);
    bits = MODE_TABLE[m - 1];
    for (k = 0; k < 3; ++k)
        heads[k] = (bits >> k) & 1 ? 1.0 : 0.0;
    *rev = bits & MODE_REVERB ? 1.0 : 0.0;
}

void al_engine_activate(AlEngine* e)
{
    e->speed = target_speed(e);
    tape_reset(&e->tape, e->speed / e->fs);
    spring_reset(&e->tank);
    bq_reset(&e->rec_hp);
    bq_reset(&e->pre);
    bq_reset(&e->de);
    bq_reset(&e->gap);
    bq_reset(&e->bump);
    bq_reset(&e->loop_hp);
    bq_reset(&e->bass_f);
    bq_reset(&e->treble_f);
    e->in_g = 4.0 * taper(ctl(e, PORT_INPUT, 0.5f));
    e->rec_on = ctl(e, PORT_ECHO_ON, 1.0f) > 0.5f ? 1.0 : 0.0;
    e->intensity = INTENSITY_MAX * clamp01(ctl(e, PORT_INTENSITY, 0.35f));
    e->echo_g = 2.0 * taper(ctl(e, PORT_ECHO, 0.6f));
    e->rev_g = 2.0 * taper(ctl(e, PORT_REVERB, 0.3f));
    e->out_g = pow(10.0, ctl(e, PORT_OUTPUT, 0.0f) / 20.0);
    e->mix = ctl(e, PORT_ENABLED, 1.0f) > 0.5f ? 1.0 : 0.0;
    e->bass = clamp01(ctl(e, PORT_BASS, 0.5f));
    e->treble = clamp01(ctl(e, PORT_TREBLE, 0.5f));
    mode_targets(e, e->head_g, &e->rev_on);
    e->ph_roller = e->ph_capstan = 0.0;
    e->drift1 = e->drift2 = 0.0;
    e->rng = 0x2545F491u;
    e->coef_count = 0;
    e->vu = 0.0;
    e->designed_speed = e->designed_age = e->designed_bass = e->designed_treble = -1.0; /* none yet */
    update_speed_filters(e, ctl(e, PORT_AGE, 0.2f));
    update_tone(e);
}

void al_engine_run(AlEngine* e, uint32_t n)
{
    const float* in_l = e->port[PORT_IN_L];
    const float* in_r = e->port[PORT_IN_R];
    float* out_l = e->port[PORT_OUT_L];
    float* out_r = e->port[PORT_OUT_R];
    const double k = e->k_ctrl;
    const double t_in = 4.0 * taper(ctl(e, PORT_INPUT, 0.5f));
    const double t_rec = ctl(e, PORT_ECHO_ON, 1.0f) > 0.5f ? 1.0 : 0.0;
    const double t_int = INTENSITY_MAX * clamp01(ctl(e, PORT_INTENSITY, 0.35f));
    const double t_echo = 2.0 * taper(ctl(e, PORT_ECHO, 0.6f));
    const double t_rev = 2.0 * taper(ctl(e, PORT_REVERB, 0.3f));
    const double t_out = pow(10.0, (36.0 * clamp01((ctl(e, PORT_OUTPUT, 0.0f) + 24.0) / 36.0) - 24.0) / 20.0);
    const double t_mix = ctl(e, PORT_ENABLED, 1.0f) > 0.5f ? 1.0 : 0.0;
    const double t_bass = clamp01(ctl(e, PORT_BASS, 0.5f));
    const double t_treble = clamp01(ctl(e, PORT_TREBLE, 0.5f));
    const double wow = clamp01(ctl(e, PORT_WOW, 0.3f));
    const double age = clamp01(ctl(e, PORT_AGE, 0.2f));
    const double v_target = target_speed(e);
    /* Tape hiss at the playback heads: -84 dBFS for new tape, up to -60 dBFS worn. */
    const double hiss = pow(10.0, (-84.0 + 24.0 * age) / 20.0);
    const double wow_scale = wow * (1.0 + 0.6 * age);
    double t_heads[3], t_revon;
    uint32_t i;

    mode_targets(e, t_heads, &t_revon);
    if (!in_l || !out_l)
        return;
    if (!in_r)
        in_r = in_l;

    for (i = 0; i < n; ++i) {
        const double l = in_l[i], r = in_r[i];
        const double x = 0.5 * (l + r);
        double w, play = 0.0, rec, tape_in, echo, rev, dry_l, dry_r, wet, ol, orr, rect;
        int h;

        /* Controls */
        e->in_g += k * (t_in - e->in_g);
        e->rec_on += k * (t_rec - e->rec_on);
        e->intensity += k * (t_int - e->intensity);
        e->echo_g += k * (t_echo - e->echo_g);
        e->rev_g += k * (t_rev - e->rev_g);
        e->out_g += k * (t_out - e->out_g);
        /* Bypass: a linear 20 ms crossfade, so the dry signal is exact once it ends. */
        if (e->mix < t_mix)
            e->mix = fmin(t_mix, e->mix + e->k_bypass);
        else if (e->mix > t_mix)
            e->mix = fmax(t_mix, e->mix - e->k_bypass);
        e->bass += k * (t_bass - e->bass);
        e->treble += k * (t_treble - e->treble);
        for (h = 0; h < 3; ++h)
            e->head_g[h] += k * (t_heads[h] - e->head_g[h]);
        e->rev_on += k * (t_revon - e->rev_on);

        /* Motor and transport */
        e->speed += (v_target - e->speed) * (v_target < e->speed ? e->k_slower : e->k_faster);
        e->ph_roller += e->speed * e->k_roller;
        e->ph_capstan += e->speed * e->k_capstan;
        e->ph_roller -= floor(e->ph_roller);
        e->ph_capstan -= floor(e->ph_capstan);
        e->drift1 += e->k_drift * (rng_next(&e->rng) - e->drift1);
        e->drift2 += e->k_drift * (e->drift1 - e->drift2);
        w = wow_scale * (WOW_DRIFT * e->drift2 * e->drift_norm
                         + WOW_ROLLER * sin_cycle(e->ph_roller)
                         + WOW_CAPSTAN * sin_cycle(e->ph_capstan));
        tape_advance(&e->tape, e->speed * (1.0 + w) * e->inv_fs);
        if (++e->coef_count >= COEF_EVERY) {
            e->coef_count = 0;
            update_speed_filters(e, age);
            update_tone(e);
        }

        /* Playback heads -> mode selector -> playback pre-amp */
        for (h = 0; h < 3; ++h)
            if (e->head_g[h] > 1e-5)
                play += e->head_g[h] * tape_read(&e->tape, h);
        play += hiss * rng_next(&e->rng) * (e->head_g[0] + e->head_g[1] + e->head_g[2]);
        play = bq_run(&e->de, play);
        play = bq_run(&e->gap, play);
        play = bq_run(&e->bump, play);
        play = bq_run(&e->loop_hp, play);

        /* Mic amp + intensity -> recording pre-amp */
        rec = e->rec_on * e->in_g * x + e->intensity * play;
        rec = bq_run(&e->rec_hp, rec);
        rect = fabs(rec);
        e->vu += e->k_vu * (rect - e->vu);

        /* Reverb drive amp from the same node; reverb amp; mode selector; reverb volume */
        rev = spring_run(&e->tank, rec) * e->rev_on * e->rev_g;

        /* Recording amp -> record head: pre-emphasis, then tape saturation */
        tape_in = bq_run(&e->pre, rec);
        tape_in = TAPE_SAT * erf(0.886226925 * tape_in / TAPE_SAT);
        tape_write(&e->tape, tape_in + 1e-20); /* the offset keeps the loop out of denormals */

        /* Echo volume -> bass and treble -> buffer amp */
        echo = bq_run(&e->treble_f, bq_run(&e->bass_f, e->echo_g * play));
        wet = echo + rev;
        dry_l = e->in_g * l;
        dry_r = e->in_g * r;
        ol = soft_limit((dry_l + wet) * e->out_g, 4.0);
        orr = soft_limit((dry_r + wet) * e->out_g, 4.0);
        out_l[i] = (float)(e->mix * ol + (1.0 - e->mix) * l);
        if (out_r)
            out_r[i] = (float)(e->mix * orr + (1.0 - e->mix) * r);
    }

    if (e->port[PORT_VU]) {
        /* Average-responding meter, calibrated on a sine: 0 VU at REC_NOMINAL peak. */
        const double ref = REC_NOMINAL * 2.0 / AL_PI;
        const double db = e->vu > 1e-9 ? 20.0 * log10(e->vu / ref) : -100.0;
        *e->port[PORT_VU] = (float)clamp01((db + 20.0) / 23.0);
    }
}

void al_engine_free(AlEngine* e)
{
    if (!e)
        return;
    tape_free(&e->tape);
    spring_free(&e->tank);
    free(e);
}
