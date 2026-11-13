/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The Astro-Loop engine as a small C API, shared by the LV2, VST3 and Audio Unit builds.
 * It follows the LV2 model: every port in ports.h is a float pointer the wrapper connects,
 * control ports read one value per block, and the VU meter output is written per block. */
#ifndef AL_ENGINE_H
#define AL_ENGINE_H

#include <stdint.h>

#include "ports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AlEngine AlEngine;

AlEngine* al_engine_new(double sample_rate); /* allocates; not real-time safe */
void al_engine_connect(AlEngine* e, uint32_t port, void* data);
void al_engine_activate(AlEngine* e); /* resets the circuit; connect the controls first */
void al_engine_run(AlEngine* e, uint32_t frames); /* in place is fine: each sample is read before it is written */
void al_engine_free(AlEngine* e);

#ifdef __cplusplus
}
#endif

#endif
