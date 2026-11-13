/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Control metadata for the hosts that don't read bundle/astro_loop.ttl: the VST3 and Audio Unit
 * builds. Values are the same plain values as the LV2 ports, so a setting means the same thing
 * in every format. test/params_check.c checks this table against the TTL and the panel. */
#ifndef AL_PARAMS_H
#define AL_PARAMS_H

#ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS /* sscanf in al_parse */
#endif
#include <math.h>
#include <stdio.h>

#include "ports.h"

#define AL_MAX_OPTS 12

typedef struct {
    int port;          /* also the parameter ID in VST3 and the Audio Unit */
    const char* name;
    const char* short_name;
    float min, max, def;
    int log;           /* logarithmic */
    int steps;         /* 0: continuous; otherwise a switch with steps + 1 positions */
    const char* unit;  /* "dB", or "" for 0-1 knobs shown as 0-10 like the panel */
    const char* opts[AL_MAX_OPTS];
    int output;        /* read only: the VU meter */
} AlParam;

static const AlParam AL_PARAMS[] = {
    { PORT_INPUT, "Input Volume", "Input", 0, 1, 0.5f, 0, 0, "", { 0 }, 0 },
    { PORT_MODE, "Mode Selector", "Mode", 1, 12, 4, 0, 11, "",
      { "1: Head 1", "2: Head 2", "3: Head 3", "4: Heads 2+3", "5: H1 + Rev", "6: H2 + Rev",
        "7: H3 + Rev", "8: H1+2 + Rev", "9: H2+3 + Rev", "10: H1+3 + Rev", "11: H1+2+3 + Rev",
        "12: Reverb Only" }, 0 },
    { PORT_RATE, "Repeat Rate", "Rate", 0, 1, 0.5f, 0, 0, "", { 0 }, 0 },
    { PORT_INTENSITY, "Intensity", "Intens", 0, 1, 0.35f, 0, 0, "", { 0 }, 0 },
    { PORT_ECHO, "Echo Volume", "Echo", 0, 1, 0.6f, 0, 0, "", { 0 }, 0 },
    { PORT_REVERB, "Reverb Volume", "Reverb", 0, 1, 0.3f, 0, 0, "", { 0 }, 0 },
    { PORT_BASS, "Bass", "Bass", 0, 1, 0.5f, 0, 0, "", { 0 }, 0 },
    { PORT_TREBLE, "Treble", "Treble", 0, 1, 0.5f, 0, 0, "", { 0 }, 0 },
    { PORT_ECHO_ON, "Echo On", "Echo On", 0, 1, 1, 0, 1, "", { "Cancel", "On" }, 0 },
    { PORT_WOW, "Wow and Flutter", "Wow", 0, 1, 0.3f, 0, 0, "", { 0 }, 0 },
    { PORT_AGE, "Tape Age", "Age", 0, 1, 0.2f, 0, 0, "", { 0 }, 0 },
    { PORT_OUTPUT, "Output", "Output", -24.0f, 12.0f, 0.0f, 0, 0, "dB", { 0 }, 0 },
    { PORT_ENABLED, "Enabled", "Enabled", 0, 1, 1, 0, 1, "", { "Off", "On" }, 0 },
    { PORT_VU, "Record Level", "VU", 0, 1, 0, 0, 0, "", { 0 }, 1 },
};
#define AL_NUM_PARAMS ((int)(sizeof AL_PARAMS / sizeof AL_PARAMS[0]))

/* The first control port and the number of saved values: every input control, in port order. */
#define AL_FIRST_CONTROL PORT_INPUT
#define AL_NUM_INPUTS (PORT_ENABLED - PORT_INPUT + 1)

static const AlParam* al_param(int port)
{
    int i;
    for (i = 0; i < AL_NUM_PARAMS; ++i)
        if (AL_PARAMS[i].port == port)
            return &AL_PARAMS[i];
    return NULL;
}

static float al_clamp(const AlParam* p, float v)
{
    if (!(v >= p->min)) /* also catches NaN */
        v = p->min;
    if (v > p->max)
        v = p->max;
    return p->steps ? floorf(v + 0.5f) : v;
}

/* 0-1 host value to plain value. Switches split 0-1 into equal parts, as the VST3 SDK does. */
static float al_from_norm(const AlParam* p, double n)
{
    n = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
    if (p->steps) {
        int i = (int)(n * (p->steps + 1));
        return p->min + (float)(i > p->steps ? p->steps : i);
    }
    if (p->log)
        return (float)(p->min * pow((double)p->max / p->min, n));
    return (float)(p->min + n * (p->max - p->min));
}

static double al_to_norm(const AlParam* p, float v)
{
    v = al_clamp(p, v);
    if (p->steps)
        return (double)(v - p->min) / p->steps;
    if (p->log)
        return log((double)v / p->min) / log((double)p->max / p->min);
    return (double)(v - p->min) / (p->max - p->min);
}

/* Text for a value, matching the panel's display. */
static void al_format(const AlParam* p, float v, char* buf, size_t n)
{
    v = al_clamp(p, v);
    if (p->steps)
        snprintf(buf, n, "%s", p->opts[(int)(v - p->min)]);
    else if (p->unit[0] == 'd')
        snprintf(buf, n, "%+.1f", v);
    else if (p->output)
        snprintf(buf, n, "%.0f%%", 100.0f * v);
    else
        snprintf(buf, n, "%.1f", 10.0f * v);
}

/* The reverse of al_format; returns 0 if the text means nothing. Switch positions match on their
 * whole name, or (for the mode selector) on the number before the colon. */
static int al_parse(const AlParam* p, const char* s, float* out)
{
    double d;
    int i;
    for (i = 0; p->steps && i <= p->steps; ++i) {
        const char *a = p->opts[i], *b = s;
        while (*a && *b && (*a | 32) == (*b | 32))
            ++a, ++b;
        if (!*a && !*b) {
            *out = p->min + (float)i;
            return 1;
        }
    }
    if (sscanf(s, "%lf", &d) != 1)
        return 0;
    if (!p->unit[0] && !p->steps)
        d /= 10.0; /* 0-10 scale */
    *out = al_clamp(p, (float)d);
    return 1;
}

#endif
