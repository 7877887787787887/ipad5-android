/* SPDX-License-Identifier: Apache-2.0 */
#ifndef J72S_ALSA_CONTROLS_H
#define J72S_ALSA_CONTROLS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum j72s_audio_output {
    J72S_AUDIO_SPEAKERS,
    J72S_AUDIO_HEADPHONES,
};

/* These only change named CS42L81 switches. Driver gain values are retained. */
int j72s_set_audio_output(int card, enum j72s_audio_output output);
int j72s_set_capture_switch(int card, bool enabled);

#ifdef __cplusplus
}
#endif
#endif
