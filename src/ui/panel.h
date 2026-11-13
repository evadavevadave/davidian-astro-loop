/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * The design is original and in the house style (graphite panel, cream lettering, amber
 * accents, red footswitch LED, black knurled knobs, outlined sections). It keeps the
 * functional grouping of the tape echo (input and meter, mode selector, echo controls, tone,
 * reverb) but none of the original unit's branding or panel artwork. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "../params.h"
#include "../ports.h"
#include "../tape.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 960
#define PANEL_H 470

enum { W_KNOB, W_TRIM, W_FOOT, W_SELECTOR };

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def;
    const char* label; /* on the panel */
    const char* name;  /* in the display */
    const char* lo;    /* scale end labels */
    const char* hi;
} Widget;

static const Widget WIDGETS[] = {
    { W_KNOB, PORT_INPUT, 111, 244, 26, 0, 1, 0.5f, "INPUT", "INPUT VOLUME", "0", "10" },
    { W_SELECTOR, PORT_MODE, 331, 186, 38, 1, 12, 4, "MODE", "MODE SELECTOR", 0, 0 },
    { W_KNOB, PORT_RATE, 506, 146, 28, 0, 1, 0.5f, "REPEAT RATE", "REPEAT RATE", "0", "10" },
    { W_KNOB, PORT_INTENSITY, 596, 146, 28, 0, 1, 0.35f, "INTENSITY", "INTENSITY", "0", "10" },
    { W_KNOB, PORT_ECHO, 686, 146, 28, 0, 1, 0.6f, "ECHO", "ECHO VOLUME", "0", "10" },
    { W_KNOB, PORT_BASS, 806, 130, 22, 0, 1, 0.5f, "BASS", "ECHO BASS", "-", "+" },
    { W_KNOB, PORT_TREBLE, 896, 130, 22, 0, 1, 0.5f, "TREBLE", "ECHO TREBLE", "-", "+" },
    { W_KNOB, PORT_REVERB, 831, 254, 22, 0, 1, 0.3f, "REVERB", "REVERB VOLUME", "0", "10" },
    { W_TRIM, PORT_WOW, 71, 402, 17, 0, 1, 0.3f, "WOW", "WOW AND FLUTTER", 0, 0 },
    { W_TRIM, PORT_AGE, 151, 402, 17, 0, 1, 0.2f, "TAPE AGE", "TAPE AGE", 0, 0 },
    { W_FOOT, PORT_ECHO_ON, 481, 404, 25, 0, 1, 1, "ECHO", "ECHO", 0, 0 },
    { W_TRIM, PORT_OUTPUT, 851, 402, 17, -24.0f, 12.0f, 0.0f, "OUTPUT", "OUTPUT LEVEL", 0, 0 },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */
#define SEL_NUM_R 56.0f           /* radius of the position numbers */

/* The VU meter window and the tape path drawing. */
#define VU_X0 30.0f
#define VU_Y0 94.0f
#define VU_X1 192.0f
#define VU_Y1 186.0f
#define TAPE_Y0 218.0f       /* top of the head drawing: the "playback heads" caption */
#define LAMP_Y 244.0f        /* playback head lamps */
#define TAPE_Y 264.0f        /* the tape */
#define HEAD_LABEL_Y 282.0f  /* letters under the heads */
#define TAPE_X0 477.0f       /* guide roller */
#define TAPE_X1 727.0f       /* capstan */
#define ERASE_X 495.0f
#define REC_X 515.0f
#define TAPE_PX_PER_CM 23.0f /* playback heads drawn at their true spacing */
#define head_x(k) (REC_X + TAPE_PX_PER_CM * (float)HEAD_CM[k])
#define REV_LAMP_X 893.0f
#define REV_LAMP_Y 248.0f

typedef void (*PanelWrite)(void* host, int port, float value);
/* Brackets each edit (a drag, a click, a wheel step) for hosts that record automation. */
typedef void (*PanelGesture)(void* host, int port, int begin);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[PORT_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    PanelGesture gesture; /* optional; set it after panel_init */
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    const float n = (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->min + n * (w->max - w->min);
}

static int p_mode(const Panel* p)
{
    int m = (int)floorf(p->val[PORT_MODE] + 0.5f);
    return m < 1 ? 1 : (m > MODE_COUNT ? MODE_COUNT : m);
}

/* Echo time of head 1 at the repeat rate's setting, once the motor has caught up. */
static float head1_ms(float rate)
{
    const double v = TAPE_SPEED_MIN + (TAPE_SPEED_MAX - TAPE_SPEED_MIN) * (rate < 0 ? 0 : (rate > 1 ? 1 : rate));
    return (float)(1000.0 * HEAD_CM[0] / v);
}

static void upper(char* s)
{
    for (; *s; ++s)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->port];
    switch (w->kind) {
    case W_SELECTOR:
        snprintf(buf, n, "%s", al_param(PORT_MODE)->opts[p_mode(p) - 1]);
        upper(buf);
        break;
    case W_FOOT:
        snprintf(buf, n, "%s", v > 0.5f ? "ON" : "CANCEL");
        break;
    default:
        if (w->port == PORT_OUTPUT)
            snprintf(buf, n, "%+.1f DB", v);
        else if (w->port == PORT_RATE)
            snprintf(buf, n, "%.1f  HEAD 1 %.0f MS", 10.0f * v, head1_ms(v));
        else if (w->port == PORT_BASS || w->port == PORT_TREBLE)
            snprintf(buf, n, "%+.1f DB", (2.0f * v - 1.0f) * 10.0f);
        else
            snprintf(buf, n, "%.1f", 10.0f * v);
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, 9, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, 9, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, 7, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, 7, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? 9.0f : 8.0f, 2.0f,
                big ? 1.6f : 1.4f, COL_INK, 1);
}

static float sel_angle(int m) { return -KNOB_SPAN + 2.0f * KNOB_SPAN * (m - 1) / (MODE_COUNT - 1); }

static void draw_selector_static(Canvas* c, const Widget* w)
{
    int m;
    const float a_rep0 = sel_angle(1) - 0.12f, a_rep1 = sel_angle(4) + 0.12f;
    const float a_mix0 = sel_angle(5) - 0.12f, a_mix1 = sel_angle(11) + 0.12f;
    const float a_rev = sel_angle(12);
    /* Group arcs outside the numbers: repeat, reverb + echo, reverb only. */
    cv_arc(c, w->x, w->y, SEL_NUM_R + 12, 1.4f, a_rep0, a_rep1, COL_LINE, 1);
    cv_arc(c, w->x, w->y, SEL_NUM_R + 12, 1.4f, a_mix0, a_mix1, COL_LINE, 1);
    cv_arc(c, w->x, w->y, SEL_NUM_R + 12, 1.4f, a_rev - 0.12f, a_rev + 0.12f, COL_LINE, 1);
    text_center(c, 254, 112, "REPEAT", 7, 1.6f, 1.1f, COL_DIM, 1);
    text_center(c, 408, 112, "ECHO+REV", 7, 1.6f, 1.1f, COL_DIM, 1);
    text_center(c, 400, 266, "REV", 7, 1.6f, 1.1f, COL_DIM, 1);
    for (m = 1; m <= MODE_COUNT; ++m) {
        const float a = sel_angle(m);
        cv_line(c, cv_ax(w->x, w->r + 5, a), cv_ay(w->y, w->r + 5, a), cv_ax(w->x, w->r + 9, a),
                cv_ay(w->y, w->r + 9, a), 1.3f, COL_INK, 0.8f);
    }
    text_center(c, w->x, w->y + w->r + 46, w->label, 9, 2.0f, 1.6f, COL_INK, 1);
}

static void draw_foot_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 9, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 8, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 8, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 18, w->label, 10, 2.5f, 1.7f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 26, 7.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 26, 7.5f, 1.5f, 0x8a8e96, 0.8f);
}

static void draw_lamp_bezel(Canvas* c, float x, float y, float r)
{
    cv_circle(c, x + 1, y + 1.5f, r + 3, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r + 3, 0xb9bcc3, 0x45484e, -0.4f, -0.6f, 1);
    cv_circle(c, x, y, r, 0x140c04, 1);
}

static void draw_vu_static(Canvas* c)
{
    const float cx = 0.5f * (VU_X0 + VU_X1), py = VU_Y1 + 26; /* needle pivot below the window */
    const float marks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    int i;
    cv_rrect(c, VU_X0 - 3, VU_Y0 - 3, VU_X1 + 3, VU_Y1 + 3, 7, 0x000000, 0.6f);
    cv_rrect_vgrad(c, VU_X0, VU_Y0, VU_X1, VU_Y1, 5, 0xf0e6cc, 0xd6c79f, 1);
    cv_rrect_line(c, VU_X0 - 3, VU_Y0 - 3, VU_X1 + 3, VU_Y1 + 3, 7, 1.2f, 0x6d7179, 0.8f);
    /* Scale: -20 to +3 VU over +/-42 degrees, red above 0. */
    cv_arc(c, cx, py, 88, 1.6f, -0.733f, 0.733f, 0x2a2622, 0.9f);
    cv_arc(c, cx, py, 91, 4.0f, (0.0f + 20.0f) / 23.0f * 1.466f - 0.733f, 0.733f, 0xc8281e, 0.9f);
    for (i = 0; i < 11; ++i) {
        const float a = (marks[i] + 20.0f) / 23.0f * 1.466f - 0.733f;
        const uint32_t col = marks[i] > 0 ? 0xc8281e : 0x2a2622;
        char t[6];
        cv_line(c, cv_ax(cx, 84, a), cv_ay(py, 84, a), cv_ax(cx, 95, a), cv_ay(py, 95, a), 1.3f, col, 1);
        if (marks[i] == -20 || marks[i] == -10 || marks[i] == -5 || marks[i] == -3 || marks[i] == 0 || marks[i] == 3) {
            snprintf(t, sizeof t, "%d", (int)fabsf(marks[i]));
            text_center(c, cv_ax(cx, 104, a), cv_ay(py, 104, a) - 3.5f, t, 6, 1.0f, 1.0f, col, 1);
        }
    }
    text_center(c, cx, VU_Y1 - 26, "VU", 9, 2.0f, 1.5f, 0x2a2622, 1);
    text_center(c, cx, VU_Y1 - 13, "RECORD LEVEL", 6, 1.6f, 1.0f, 0x5a5048, 1);
}

/* One head block, its gap touching the tape, with its letter centred underneath. */
static void draw_head(Canvas* c, float x, const char* label, uint32_t body, uint32_t ink)
{
    cv_rrect(c, x - 6 + 1, TAPE_Y + 2, x + 6 + 1, TAPE_Y + 15, 2, 0x000000, 0.45f);
    cv_rrect_vgrad(c, x - 6, TAPE_Y, x + 6, TAPE_Y + 13, 2, body, mix(body, 0x000000, 0.45f), 1);
    cv_line(c, x, TAPE_Y + 1, x, TAPE_Y + 5, 1.0f, 0x1a1b1e, 0.9f); /* the gap */
    text_center(c, x, HEAD_LABEL_Y, label, 7, 1.0f, 1.2f, ink, 1);
}

/* The head block, read left to right as the tape runs: guide roller, erase, record, the three
 * playback heads at their true relative spacing, then the capstan and pinch roller. Three
 * aligned rows: playback lamps above the tape, heads on it, letters below. */
static void draw_tape_static(Canvas* c)
{
    const float x_first = head_x(0), x_last = head_x(2);
    int k;
    text_center(c, 0.5f * (x_first + x_last), TAPE_Y0 + 2, "PLAYBACK HEADS", 6, 1.6f, 1.0f, COL_DIM, 1);
    cv_line(c, x_first, TAPE_Y0 + 12, x_last, TAPE_Y0 + 12, 1.0f, COL_LINE, 0.9f);
    for (k = 0; k < 3; ++k)
        draw_lamp_bezel(c, head_x(k), LAMP_Y, 4.5f);

    /* Guide roller, tape, capstan and pinch roller */
    cv_shaded_circle(c, TAPE_X0, TAPE_Y + 6, 6, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.5f, 1);
    cv_shaded_circle(c, TAPE_X1, TAPE_Y + 6, 6, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.5f, 1);
    cv_circle(c, TAPE_X1, TAPE_Y + 6, 1.8f, 0x202226, 1);
    cv_shaded_circle(c, TAPE_X1, TAPE_Y - 7, 6, 0x4a4c52, 0x101012, -0.4f, -0.5f, 1); /* rubber pinch roller */
    cv_circle(c, TAPE_X1, TAPE_Y - 7, 1.6f, 0x8a8e96, 1);
    cv_line(c, TAPE_X0, TAPE_Y, TAPE_X1, TAPE_Y, 2.6f, 0x4a3420, 1);
    cv_line(c, TAPE_X0, TAPE_Y - 0.6f, TAPE_X1, TAPE_Y - 0.6f, 0.8f, 0x8a6a42, 0.8f);

    draw_head(c, ERASE_X, "E", 0x8a8e96, COL_DIM);
    draw_head(c, REC_X, "R", 0xc9ccd2, COL_INK);
    for (k = 0; k < 3; ++k) {
        char t[2] = { (char)('1' + k), 0 };
        draw_head(c, head_x(k), t, 0xc9ccd2, COL_INK);
    }
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, "DAVIDIAN ASTRO-LOOP", 20, 3.2f, 2.8f, COL_INK, 1);
    text_draw(&c, 32, 46, "THREE-HEAD TAPE ECHO WITH SPRING REVERB", 7, 2.4f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, 590, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, 616, 14, 944, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, 618, 16, 942, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, 616, 14, 944, 56, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, 76, 206, 306, "INPUT");
    draw_section(&c, 216, 76, 446, 306, "MODE SELECTOR");
    draw_section(&c, 456, 76, 746, 306, "TAPE ECHO");
    draw_section(&c, 756, 76, 944, 196, "ECHO TONE");
    draw_section(&c, 756, 214, 944, 306, "SPRING");
    draw_section(&c, 16, 326, 206, 456, "TRANSPORT");
    draw_section(&c, 756, 326, 944, 456, "LEVEL");

    draw_vu_static(&c);
    draw_tape_static(&c);
    draw_lamp_bezel(&c, REV_LAMP_X, REV_LAMP_Y, 5);
    text_center(&c, REV_LAMP_X, REV_LAMP_Y + 14, "ON", 6, 1.4f, 1.0f, COL_DIM, 1);

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_SELECTOR: draw_selector_static(&c, w); break;
        case W_FOOT: draw_foot_static(&c, w); break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob_body(Canvas* c, float x, float y, float r, float ang, int knurls, float pointer_w)
{
    int i;
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), r > 20 ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), pointer_w, COL_INK, 1);
}

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    draw_knob_body(c, w->x, w->y, w->r, ang, w->kind == W_KNOB ? 36 : 20, w->kind == W_KNOB ? 3.0f : 2.2f);
}

static void draw_selector(Canvas* c, const Widget* w, int mode)
{
    int m;
    for (m = 1; m <= MODE_COUNT; ++m) {
        const float a = sel_angle(m);
        char num[4];
        snprintf(num, sizeof num, "%d", m);
        text_center(c, cv_ax(w->x, SEL_NUM_R, a), cv_ay(w->y, SEL_NUM_R, a) - 4.0f, num, 8, 0.6f, 1.3f,
                    m == mode ? COL_AMBER : COL_DIM, 1);
    }
    /* A fluted skirt and a pointer bar, so it reads as a rotary switch rather than a pot. */
    cv_circle(c, w->x + 2, w->y + 4, w->r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (m = 0; m < 12; ++m) {
        const float a = sel_angle(mode) + 2.0f * CV_PI * m / 12.0f;
        cv_circle(c, cv_ax(w->x, w->r * 0.9f, a), cv_ay(w->y, w->r * 0.9f, a), 3.2f, 0x000000, 0.45f);
    }
    {
        const float a = sel_angle(mode);
        const float px = sinf(a), py = -cosf(a);
        cv_line(c, w->x - px * w->r * 0.7f, w->y - py * w->r * 0.7f, w->x + px * w->r * 0.7f,
                w->y + py * w->r * 0.7f, 11.0f, 0x26272b, 1);
        cv_line(c, w->x, w->y, w->x + px * w->r * 0.95f, w->y + py * w->r * 0.95f, 3.0f, COL_INK, 1);
    }
}

static void draw_foot(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 26;
    cv_shaded_circle(c, w->x, w->y, w->r, 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 5, 16, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 5.5f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 5.5f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static void draw_lamp(Canvas* c, float x, float y, float r, int on)
{
    cv_circle(c, x, y, r, on ? COL_AMBER : 0x1e1206, 1);
    if (on) {
        cv_glow(c, x, y, r * 0.5f, 14, COL_AMBER, 0.4f);
        cv_circle(c, x - 1.5f, y - 1.5f, r * 0.35f, 0xfff2d6, 0.8f);
    }
}

static void draw_vu(Canvas* c, float n)
{
    const float cx = 0.5f * (VU_X0 + VU_X1), py = VU_Y1 + 26;
    const float a = (n < 0 ? 0 : (n > 1 ? 1 : n)) * 1.466f - 0.733f;
    const int saved = c->cy1;
    c->cy1 = (int)VU_Y1; /* the needle stops at the bottom of the window */
    cv_line(c, cv_ax(cx, 30, a) + 1, cv_ay(py, 30, a) + 1.5f, cv_ax(cx, 96, a) + 1, cv_ay(py, 96, a) + 1.5f,
            1.6f, 0x000000, 0.25f);
    cv_line(c, cv_ax(cx, 30, a), cv_ay(py, 30, a), cv_ax(cx, 96, a), cv_ay(py, 96, a), 1.4f, 0x141210, 1);
    c->cy1 = saved;
}

static void draw_display(Panel* p)
{
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, 632, 22, "ASTRO-LOOP", 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, 632, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS", 7, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    text_draw(c, 632, 22, WIDGETS[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, 632, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    const unsigned bits = MODE_TABLE[p_mode(p) - 1];
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_SELECTOR: draw_selector(&p->c, w, p_mode(p)); break;
        case W_FOOT: draw_foot(&p->c, w, p->val[w->port] > 0.5f); break;
        }
    }
    for (i = 0; i < 3; ++i)
        draw_lamp(&p->c, head_x(i), LAMP_Y, 4.5f, (bits >> i) & 1);
    draw_lamp(&p->c, REV_LAMP_X, REV_LAMP_Y, 5, (bits & MODE_REVERB) != 0);
    draw_vu(&p->c, p->val[PORT_VU]);
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i)
        p->val[WIDGETS[i].port] = WIDGETS[i].def;
    p->val[PORT_ENABLED] = 1.0f;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= PORT_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    if (w->kind == W_SELECTOR || w->kind == W_FOOT)
        v = floorf(v + 0.5f);
    if (v < w->min) v = w->min;
    if (v > w->max) v = w->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        p->val[w->port] = v;
        if (p->write)
            p->write(p->host, w->port, v);
    }
}

static void gesture(Panel* p, int wi, int begin)
{
    if (p->gesture)
        p->gesture(p->host, WIDGETS[wi].port, begin);
}

/* A whole edit in one go: a click on a switch, a double-click reset, a wheel step. */
static void set_once(Panel* p, int wi, float v)
{
    gesture(p, wi, 1);
    set_value(p, wi, v);
    gesture(p, wi, 0);
}

static int hit(float x, float y)
{
    int i;
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        const float r = w->kind == W_SELECTOR ? SEL_NUM_R + 10 : w->r + 8;
        if (dx * dx + dy * dy <= r * r)
            return i;
    }
    return -1;
}

/* The selector position nearest to the point, or 0 if the point is on the knob itself. */
static int selector_pos_at(const Widget* w, float x, float y)
{
    const float dx = x - w->x, dy = y - w->y;
    float a, best = 1e9f;
    int m, pos = 0;
    if (dx * dx + dy * dy < (w->r + 4) * (w->r + 4))
        return 0;
    a = atan2f(dx, -dy);
    for (m = 1; m <= MODE_COUNT; ++m) {
        const float d = fabsf(a - sel_angle(m));
        if (d < best) {
            best = d;
            pos = m;
        }
    }
    return best < 0.2f ? pos : 0;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    switch (w->kind) {
    case W_SELECTOR: {
        const int pos = selector_pos_at(w, x, y);
        if (pos) { /* a click on a number picks that position */
            set_once(p, i, (float)pos);
            return;
        }
    }
        /* fall through: drag the knob */
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_once(p, i, w->def);
            return;
        }
        gesture(p, i, 1);
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_FOOT:
        set_once(p, i, p->val[w->port] > 0.5f ? 0.0f : 1.0f);
        break;
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    float n;
    (void)x;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    if (w->kind == W_SELECTOR) {
        /* Detents: keep the unrounded position, so slow drags still step. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = n;
    } else {
        /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = w_norm(w, p->val[w->port]);
    }
    p->drag_y = y;
}

static void panel_mouse_up(Panel* p)
{
    if (p->drag >= 0) {
        gesture(p, p->drag, 0);
        p->dirty = 1;
    }
    p->drag = -1;
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_SELECTOR)
        set_once(p, i, (float)(p_mode(p) + (steps > 0 ? 1 : -1)));
    else if (w->kind != W_FOOT)
        set_once(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
}

#endif
