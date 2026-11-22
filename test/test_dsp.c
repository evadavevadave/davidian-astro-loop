/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Checks for the Astro-Loop. Usage: test_dsp <bundle folder>
 * Part 1 drives the engine directly: echo times per head and mode against the published
 * figures, the mode selector table, the tone controls against the service notes, where the
 * repeats run away, the motor ballistics, the spring tank, stability, and bypass. Part 2 loads the built plugin
 * library the way a host does. */
#ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine.h"
#include "../src/lv2_abi.h"
#include "../src/params.h"
#include "../src/spring.h"

#ifdef _WIN32
#    include <windows.h>
#    define LIB_NAME "astro_loop.dll"
#    define LIB_OPEN(p) ((void*)LoadLibraryA(p))
#    define LIB_SYM(l, s) ((void*)GetProcAddress((HMODULE)(l), s))
#else
#    include <dlfcn.h>
#    ifdef __APPLE__
#        define LIB_NAME "astro_loop.dylib"
#    else
#        define LIB_NAME "astro_loop.so"
#    endif
#    define LIB_OPEN(p) dlopen(p, RTLD_NOW)
#    define LIB_SYM(l, s) dlsym(l, s)
#endif

#define PI 3.14159265358979323846
#define BLOCK 256

static int failures;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

/* ---- An engine with its own control values ---- */

typedef struct {
    AlEngine* e;
    float ctl[PORT_COUNT];
    double fs;
} Rig;

static int rig_open(Rig* r, double fs)
{
    int i;
    memset(r, 0, sizeof *r);
    r->fs = fs;
    r->e = al_engine_new(fs);
    if (!r->e)
        return 0;
    for (i = 0; i < AL_NUM_PARAMS; ++i)
        r->ctl[AL_PARAMS[i].port] = AL_PARAMS[i].def;
    for (i = AL_FIRST_CONTROL; i < PORT_COUNT; ++i)
        al_engine_connect(r->e, (uint32_t)i, &r->ctl[i]);
    return 1;
}

static void rig_start(Rig* r) { al_engine_activate(r->e); }

/* Mono in, left out. */
static void rig_run(Rig* r, const float* in, float* out, long n)
{
    static float dummy[BLOCK];
    long i;
    for (i = 0; i < n; i += BLOCK) {
        const uint32_t m = (uint32_t)(n - i < BLOCK ? n - i : BLOCK);
        al_engine_connect(r->e, PORT_IN_L, (void*)(in + i));
        al_engine_connect(r->e, PORT_IN_R, (void*)(in + i));
        al_engine_connect(r->e, PORT_OUT_L, out + i);
        al_engine_connect(r->e, PORT_OUT_R, dummy);
        al_engine_run(r->e, m);
    }
}

static void rig_close(Rig* r) { al_engine_free(r->e); }

/* Echo-only settings: no dry (measured by subtracting nothing: the dry path is linear and the
 * impulse is at t = 0, so anything later than 20 ms is wet), no reverb, no wow. */
static void quiet(Rig* r)
{
    r->ctl[PORT_WOW] = 0.0f;
    r->ctl[PORT_AGE] = 0.0f;
    r->ctl[PORT_REVERB] = 0.0f;
    r->ctl[PORT_INTENSITY] = 0.0f;
    r->ctl[PORT_BASS] = 0.5f;
    r->ctl[PORT_TREBLE] = 0.5f;
}

/* Times (s) of the echo peaks after an impulse, strongest first are not needed: returns the
 * local maxima above a threshold, in time order. */
static int echo_times(const float* y, long n, double fs, double* t, int max)
{
    long i, last = -100000;
    int c = 0;
    double peak = 0.0;
    const long start = (long)(0.02 * fs);
    for (i = start; i < n; ++i)
        peak = fmax(peak, fabs(y[i]));
    for (i = start + 1; i < n - 1 && c < max; ++i) {
        const double a = fabs(y[i]);
        if (a > 0.3 * peak && a >= fabs(y[i - 1]) && a >= fabs(y[i + 1]) && i - last > (long)(0.02 * fs)) {
            /* the biggest sample within 5 ms */
            long j, best = i;
            for (j = i; j < i + (long)(0.005 * fs) && j < n; ++j)
                if (fabs(y[j]) > fabs(y[best]))
                    best = j;
            t[c++] = best / fs;
            last = best;
            i = best;
        }
    }
    return c;
}

static void impulse_response(double fs, int mode, float rate, float* y, long n)
{
    Rig r;
    float* x = (float*)calloc((size_t)n, sizeof(float));
    rig_open(&r, fs);
    quiet(&r);
    r.ctl[PORT_MODE] = (float)mode;
    r.ctl[PORT_RATE] = rate;
    rig_start(&r);
    x[0] = 0.25f;
    rig_run(&r, x, y, n);
    rig_close(&r);
    free(x);
}

static void test_echo_times(double fs)
{
    /* Head 1: 69-177 ms; heads 2 and 3 at 1.94x and 2.85x (published measurements). */
    const double h1[2] = { 0.177, 0.069 }, ratio[3] = { 1.0, 1.94, 2.85 };
    const long n = (long)(0.7 * fs);
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    int rate, head;
    printf("Echo times at %.0f Hz\n", fs);
    for (rate = 0; rate < 2; ++rate)
        for (head = 0; head < 3; ++head) {
            double t[4], expect = h1[rate] * ratio[head];
            char msg[160];
            int c;
            impulse_response(fs, head + 1, (float)rate, y, n);
            c = echo_times(y, n, fs, t, 4);
            snprintf(msg, sizeof msg, "repeat rate %s, head %d: first echo %.1f ms (expect %.1f ms +/-2%%)",
                     rate ? "max" : "min", head + 1, c ? 1000 * t[0] : -1.0, 1000 * expect);
            check(msg, c > 0 && fabs(t[0] - expect) < 0.02 * expect);
        }
    free(y);
}

/* Every mode: which heads sound, and whether the reverb does. */
static void test_modes(void)
{
    const double fs = 48000.0;
    const long n = (long)(0.45 * fs);
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    int m;
    printf("Mode selector (service notes' table)\n");
    for (m = 1; m <= MODE_COUNT; ++m) {
        Rig r;
        float* x = (float*)calloc((size_t)n, sizeof(float));
        double head_t[3], e_head[3] = { 0, 0, 0 }, e_rev = 0.0;
        int k, ok = 1;
        char msg[160];
        rig_open(&r, fs);
        quiet(&r);
        r.ctl[PORT_MODE] = (float)m;
        r.ctl[PORT_RATE] = 1.0f;
        r.ctl[PORT_REVERB] = 0.8f;
        rig_start(&r);
        x[0] = 0.25f;
        rig_run(&r, x, y, n);
        for (k = 0; k < 3; ++k) {
            long i, c;
            head_t[k] = 0.069 * (k == 0 ? 1.0 : (k == 1 ? 1.94 : 2.85));
            c = (long)(head_t[k] * fs);
            for (i = c - (long)(0.003 * fs); i < c + (long)(0.003 * fs); ++i)
                e_head[k] = fmax(e_head[k], fabs(y[i]));
        }
        /* reverb: energy between 2 and 40 ms, before any echo */
        for (k = (int)(0.002 * fs); k < (int)(0.04 * fs); ++k)
            e_rev += y[k] * y[k];
        e_rev = sqrt(e_rev / (0.038 * fs));
        snprintf(msg, sizeof msg, "mode %2d: heads %c%c%c reverb %c (levels %.3f %.3f %.3f, rev %.4f)", m,
                 e_head[0] > 0.01 ? '1' : '-', e_head[1] > 0.01 ? '2' : '-', e_head[2] > 0.01 ? '3' : '-',
                 e_rev > 1e-3 ? 'R' : '-', e_head[0], e_head[1], e_head[2], e_rev);
        for (k = 0; k < 3; ++k)
            ok &= (e_head[k] > 0.01) == (((MODE_TABLE[m - 1] >> k) & 1) != 0);
        ok &= (e_rev > 1e-3) == ((MODE_TABLE[m - 1] & MODE_REVERB) != 0);
        check(msg, ok);
        rig_close(&r);
        free(x);
    }
    free(y);
}

/* Tone controls, measured with a sine through the echo path against a flat setting. */
static double echo_gain_at(double f, float bass, float treble)
{
    const double fs = 48000.0;
    const long n = (long)(1.0 * fs);
    float* x = (float*)malloc(sizeof(float) * (size_t)n);
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    Rig r;
    long i;
    double s = 0.0, c = 0.0;
    for (i = 0; i < n; ++i)
        x[i] = (float)(0.01 * sin(2 * PI * f * i / fs));
    /* Echo only: subtract the dry signal, which is exactly x with the input volume at 5. */
    rig_open(&r, fs);
    quiet(&r);
    r.ctl[PORT_MODE] = 1.0f;
    r.ctl[PORT_RATE] = 1.0f;
    r.ctl[PORT_BASS] = bass;
    r.ctl[PORT_TREBLE] = treble;
    rig_start(&r);
    rig_run(&r, x, y, n);
    rig_close(&r);
    for (i = n / 2; i < n; ++i) {
        const double w = y[i] - x[i]; /* input volume 5 = unity dry */
        s += w * sin(2 * PI * f * i / fs);
        c += w * cos(2 * PI * f * i / fs);
    }
    free(x);
    free(y);
    return 2.0 * sqrt(s * s + c * c) / (n / 2);
}

static void test_tone(void)
{
    const double fl = 100.0, fh = 5000.0;
    const double b0 = echo_gain_at(fl, 0.5f, 0.5f), t0 = echo_gain_at(fh, 0.5f, 0.5f);
    const double bp = 20 * log10(echo_gain_at(fl, 1.0f, 0.5f) / b0);
    const double bm = 20 * log10(echo_gain_at(fl, 0.0f, 0.5f) / b0);
    const double tp = 20 * log10(echo_gain_at(fh, 0.5f, 1.0f) / t0);
    const double tm = 20 * log10(echo_gain_at(fh, 0.5f, 0.0f) / t0);
    char msg[160];
    printf("Tone controls (service notes: +/-10 dB at 100 Hz and 5 kHz)\n");
    snprintf(msg, sizeof msg, "bass at 100 Hz: %+.2f / %+.2f dB", bp, bm);
    check(msg, fabs(bp - 10) < 0.5 && fabs(bm + 10) < 0.5);
    snprintf(msg, sizeof msg, "treble at 5 kHz: %+.2f / %+.2f dB", tp, tm);
    check(msg, fabs(tp - 10) < 0.5 && fabs(tm + 10) < 0.5);
}

/* Echo path frequency response at top and bottom speed, for the report. */
static void report_response(void)
{
    const double f[] = { 50, 100, 200, 500, 1000, 2000, 3000, 5000, 8000 };
    int i;
    printf("Echo path response, mode 1, relative to 1 kHz (dB)\n   Hz:");
    for (i = 0; i < 9; ++i)
        printf(" %6.0f", f[i]);
    printf("\n");
    {
        int s;
        for (s = 0; s < 2; ++s) {
            Rig r;
            const double fs = 48000.0;
            const long n = (long)(1.5 * fs);
            float* x = (float*)malloc(sizeof(float) * (size_t)n);
            float* y = (float*)malloc(sizeof(float) * (size_t)n);
            double g[9], ref = 1.0;
            int k;
            for (k = 0; k < 9; ++k) {
                long j;
                double ss = 0, cc = 0;
                for (j = 0; j < n; ++j)
                    x[j] = (float)(0.01 * sin(2 * PI * f[k] * j / fs));
                rig_open(&r, fs);
                quiet(&r);
                r.ctl[PORT_MODE] = 1.0f;
                r.ctl[PORT_RATE] = (float)s;
                rig_start(&r);
                rig_run(&r, x, y, n);
                rig_close(&r);
                for (j = n / 2; j < n; ++j) {
                    const double w = y[j] - x[j];
                    ss += w * sin(2 * PI * f[k] * j / fs);
                    cc += w * cos(2 * PI * f[k] * j / fs);
                }
                g[k] = sqrt(ss * ss + cc * cc);
                if (f[k] == 1000)
                    ref = g[k];
            }
            printf(" %s:", s ? "fast" : "slow");
            for (k = 0; k < 9; ++k)
                printf(" %+6.1f", 20 * log10(g[k] / ref));
            printf("\n");
            free(x);
            free(y);
        }
    }
}

/* Lowest intensity at which a short burst keeps repeating for 8 s without dying away. */
static double runaway_point(int mode)
{
    const double fs = 48000.0;
    const long n = (long)(8.0 * fs);
    float* x = (float*)calloc((size_t)n, sizeof(float));
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    double lo = 0.0, hi = 1.0;
    long i;
    int it;
    for (i = 0; i < (long)(0.05 * fs); ++i)
        x[i] = (float)(0.2 * sin(2 * PI * 440 * i / fs));
    for (it = 0; it < 12; ++it) {
        const double mid = 0.5 * (lo + hi);
        double e1 = 0, e2 = 0;
        Rig r;
        rig_open(&r, fs);
        quiet(&r);
        r.ctl[PORT_MODE] = (float)mode;
        r.ctl[PORT_INTENSITY] = (float)mid;
        r.ctl[PORT_INPUT] = 0.5f;
        rig_start(&r);
        rig_run(&r, x, y, n);
        rig_close(&r);
        for (i = (long)(1.0 * fs); i < (long)(2.0 * fs); ++i)
            e1 += y[i] * y[i];
        for (i = n - (long)fs; i < n; ++i)
            e2 += y[i] * y[i];
        if (e2 > 0.5 * e1)
            hi = mid;
        else
            lo = mid;
    }
    free(x);
    free(y);
    return 0.5 * (lo + hi);
}

static void test_runaway(void)
{
    const double one = runaway_point(1), three = runaway_point(11);
    char msg[160];
    printf("Regeneration\n");
    snprintf(msg, sizeof msg, "mode 1 runs away from intensity %.2f on the dial (expect about 7)", 10 * one);
    check(msg, one > 0.6 && one < 0.8);
    snprintf(msg, sizeof msg, "mode 11 (all heads) runs away earlier, at %.2f", 10 * three);
    check(msg, three < one);
}

/* Motor ballistics: the echo time after a jump of the repeat rate. */
static void test_motor(void)
{
    const double fs = 48000.0;
    Rig r;
    const long n = (long)(fs * 0.5);
    float* x = (float*)calloc((size_t)n, sizeof(float));
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    double t_at[12];
    int s;
    char msg[160];
    printf("Motor\n");
    rig_open(&r, fs);
    quiet(&r);
    r.ctl[PORT_MODE] = 1.0f;
    r.ctl[PORT_RATE] = 1.0f;
    rig_start(&r);
    r.ctl[PORT_RATE] = 0.0f; /* slow down: delay grows with a 1.89 s time constant */
    for (s = 0; s < 12; ++s) {
        double t[2];
        memset(x, 0, sizeof(float) * (size_t)n);
        x[0] = 0.25f;
        rig_run(&r, x, y, n);
        t_at[s] = echo_times(y, n, fs, t, 1) ? t[0] : -1;
    }
    rig_close(&r);
    /* After about one time constant (at 2.0 s, the start of block 4), the speed has gone
     * 1 - 1/e of the way, so the delay is d / v(t). */
    {
        const double v = 15.6 + (40.0 - 15.6) * exp(-2.0 / 1.89);
        const double expect = 2.76 / v;
        snprintf(msg, sizeof msg, "2 s after turning the rate down: echo at %.1f ms (expect about %.1f ms)",
                 1000 * t_at[4], 1000 * expect);
        check(msg, fabs(t_at[4] - expect) < 0.06 * expect);
        const double v2 = 15.6 + (40.0 - 15.6) * exp(-5.5 / 1.89);
        snprintf(msg, sizeof msg, "5.5 s after: echo at %.1f ms (expect about %.1f ms, still closing on 177 ms)",
                 1000 * t_at[11], 1000 * 2.76 / v2);
        check(msg, fabs(t_at[11] - 2.76 / v2) < 0.03 * 2.76 / v2);
    }
    free(x);
    free(y);
}

/* Everything at full and loud noise in: finite and bounded. */
static void test_stability(double fs)
{
    const long n = (long)(6.0 * fs);
    float* x = (float*)malloc(sizeof(float) * (size_t)n);
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    unsigned s = 12345;
    long i;
    int m, ok = 1;
    double peak = 0.0;
    char msg[160];
    for (i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        x[i] = (float)(((s >> 9) / 4194304.0 - 1.0) * (i < n / 2 ? 0.9 : 0.0));
    }
    for (m = 1; m <= MODE_COUNT; ++m) {
        Rig r;
        rig_open(&r, fs);
        r.ctl[PORT_MODE] = (float)m;
        r.ctl[PORT_INPUT] = 1.0f;
        r.ctl[PORT_INTENSITY] = 1.0f;
        r.ctl[PORT_ECHO] = 1.0f;
        r.ctl[PORT_REVERB] = 1.0f;
        r.ctl[PORT_BASS] = 1.0f;
        r.ctl[PORT_TREBLE] = 1.0f;
        r.ctl[PORT_WOW] = 1.0f;
        r.ctl[PORT_AGE] = 1.0f;
        r.ctl[PORT_OUTPUT] = 12.0f;
        rig_start(&r);
        /* sweep the repeat rate while it runs */
        for (i = 0; i < n; i += 4800) {
            r.ctl[PORT_RATE] = (float)(0.5 + 0.5 * sin(i / fs * 2.0));
            rig_run(&r, x + i, y + i, n - i < 4800 ? n - i : 4800);
        }
        for (i = 0; i < n; ++i) {
            if (!isfinite(y[i]))
                ok = 0;
            peak = fmax(peak, fabs(y[i]));
        }
        rig_close(&r);
    }
    snprintf(msg, sizeof msg, "%.0f Hz, every mode, every control at full: finite, peak %.2f (limit 4)", fs, peak);
    check(msg, ok && peak <= 4.0);
    free(x);
    free(y);
}

static void test_bypass(void)
{
    const double fs = 48000.0;
    const long n = (long)fs;
    float* x = (float*)malloc(sizeof(float) * (size_t)n);
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    Rig r;
    long i;
    double d = 0.0;
    for (i = 0; i < n; ++i)
        x[i] = (float)(0.3 * sin(2 * PI * 330 * i / fs));
    rig_open(&r, fs);
    r.ctl[PORT_INTENSITY] = 0.9f;
    r.ctl[PORT_ENABLED] = 0.0f;
    rig_start(&r);
    rig_run(&r, x, y, n);
    rig_close(&r);
    for (i = 0; i < n; ++i)
        d = fmax(d, fabs(y[i] - x[i]));
    printf("Bypass\n");
    check("bypassed from the start: output equals input", d < 1e-6);
}

/* ---- Part 2: the built library ---- */

static void test_plugin(const char* bundle)
{
    char path[1024];
    void* lib;
    const LV2_Descriptor* (*get)(uint32_t);
    const LV2_Descriptor* d;
    printf("Plugin library\n");
    snprintf(path, sizeof path, "%s/%s", bundle, LIB_NAME);
    lib = LIB_OPEN(path);
    check("loads", lib != NULL);
    if (!lib)
        return;
    *(void**)&get = LIB_SYM(lib, "lv2_descriptor");
    check("exports lv2_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor 0 has our URI", d && !strcmp(d->URI, AL_URI));
    check("only one descriptor", get(1) == NULL);
    if (d) {
        static float in[48000], out_l[48000], out_r[48000];
        float ctl[PORT_COUNT];
        LV2_Handle h = d->instantiate(d, 48000.0, bundle, NULL);
        int i;
        double peak = 0.0;
        check("instantiates", h != NULL);
        if (!h)
            return;
        for (i = 0; i < AL_NUM_PARAMS; ++i)
            ctl[AL_PARAMS[i].port] = AL_PARAMS[i].def;
        for (i = AL_FIRST_CONTROL; i < PORT_COUNT; ++i)
            d->connect_port(h, (uint32_t)i, &ctl[i]);
        for (i = 0; i < 48000; ++i)
            in[i] = i < 2400 ? (float)(0.3 * sin(2 * PI * 440 * i / 48000.0)) : 0.0f;
        d->connect_port(h, PORT_IN_L, in);
        d->connect_port(h, PORT_IN_R, in);
        d->connect_port(h, PORT_OUT_L, out_l);
        d->connect_port(h, PORT_OUT_R, out_r);
        d->activate(h);
        d->run(h, 48000);
        for (i = 12000; i < 48000; ++i)
            peak = fmax(peak, fabs(out_l[i]));
        check("echoes after the input stops", peak > 0.01);
        check("VU meter output written", ctl[PORT_VU] >= 0.0f && ctl[PORT_VU] <= 1.0f);
        d->cleanup(h);
    }
}

/* The spring tank, shared with the Jade Choir: the first return one transit of the shortest
 * spring after the impulse, and a decay inside the replacement tank's medium range (1.75-3 s). */
static void test_spring(double fs)
{
    SpringTank t;
    const long n = (long)(2.0 * fs);
    long i, first = -1;
    double a = 0.0, b = 0.0, rt60;
    char msg[160];
    if (!spring_init(&t, fs)) {
        check("spring tank allocates", 0);
        return;
    }
    spring_reset(&t);
    for (i = 0; i < n; ++i) {
        const double y = spring_run(&t, i == 0 ? 0.5 : 0.0);
        if (first < 0 && fabs(y) > 1e-3)
            first = i;
        if (i >= (long)(0.5 * fs) && i < (long)(0.6 * fs))
            a += y * y;
        if (i >= (long)(1.5 * fs) && i < (long)(1.6 * fs))
            b += y * y;
    }
    spring_free(&t);
    rt60 = 60.0 / (-10.0 * log10(b / a));
    snprintf(msg, sizeof msg, "spring tank at %.0f Hz: first return %.1f ms (expect %.1f), decay %.2f s",
             fs, 1000.0 * first / fs, 1000.0 * TANK_TRANSIT[0], rt60);
    check(msg, fabs((double)first / fs - TANK_TRANSIT[0]) < 0.002 && rt60 > 1.75 && rt60 < 3.0);
}

int main(int argc, char** argv)
{
    test_echo_times(48000.0);
    test_echo_times(96000.0);
    test_modes();
    test_tone();
    report_response();
    test_runaway();
    test_motor();
    test_spring(44100.0);
    test_spring(192000.0);
    test_stability(44100.0);
    test_stability(192000.0);
    test_bypass();
    if (argc > 1)
        test_plugin(argv[1]);
    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
