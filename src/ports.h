/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Port indices, shared by the plugin and its GUI. They must match bundle/astro_loop.ttl. */
#ifndef PORTS_H
#define PORTS_H

#define AL_URI    "urn:davidian:astro-loop"
#define AL_UI_URI "urn:davidian:astro-loop#ui"

enum {
    PORT_IN_L, PORT_IN_R, PORT_OUT_L, PORT_OUT_R,
    PORT_INPUT,     /* instrument volume, 0-1 */
    PORT_MODE,      /* mode selector, 1-12 */
    PORT_RATE,      /* repeat rate (tape speed), 0-1 */
    PORT_INTENSITY, /* regeneration, 0-1 */
    PORT_ECHO,      /* echo volume, 0-1 */
    PORT_REVERB,    /* reverb volume, 0-1 */
    PORT_BASS,      /* echo bass, 0-1 (0.5 flat) */
    PORT_TREBLE,    /* echo treble, 0-1 (0.5 flat) */
    PORT_ECHO_ON,   /* echo footswitch: 0 cancels the input to the record head */
    PORT_WOW,       /* wow and flutter, 0-1 */
    PORT_AGE,       /* tape and head wear, 0-1 */
    PORT_OUTPUT,    /* output trim, dB */
    PORT_ENABLED,
    PORT_VU,        /* output: record level meter needle, 0-1 (-20 to +3 VU) */
    PORT_COUNT
};

#define MODE_COUNT 12

/* The mode selector's table from the service notes ("mode selector position vs activated
 * playback head"): bits 0-2 are playback heads 1-3, bit 3 is the spring reverb. */
#define MODE_REVERB 8
static const unsigned char MODE_TABLE[MODE_COUNT] = {
    1, 2, 4, 2 | 4,                                  /* 1-4: repeat */
    1 | 8, 2 | 8, 4 | 8, 1 | 2 | 8, 2 | 4 | 8, 1 | 4 | 8, 1 | 2 | 4 | 8, /* 5-11: reverb + echo */
    8                                                /* 12: reverb only */
};

#endif
