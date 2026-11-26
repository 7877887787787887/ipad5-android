/* SPDX-License-Identifier: Apache-2.0 */
#ifndef J72S_ALSA_ENDPOINT_H
#define J72S_ALSA_ENDPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

struct j72s_pcm_endpoint {
    int card;
    int device;
};

/* stream is SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE. */
int j72s_find_pcm_endpoint(int stream, struct j72s_pcm_endpoint *endpoint);

#ifdef __cplusplus
}
#endif
#endif
