/* SPDX-License-Identifier: Apache-2.0 */
#include "alsa_endpoint.h"
#include <stdio.h>
#include <string.h>
#include <sound/asound.h>

int main(void) {
    const int streams[] = {SNDRV_PCM_STREAM_PLAYBACK, SNDRV_PCM_STREAM_CAPTURE};
    const char *const names[] = {"playback", "capture"};
    int status = 0;
    for (unsigned i = 0; i < sizeof(streams) / sizeof(streams[0]); ++i) {
        struct j72s_pcm_endpoint endpoint;
        const int result = j72s_find_pcm_endpoint(streams[i], &endpoint);
        if (result) {
            fprintf(stderr, "J72s ALSA %s unavailable: %s\n", names[i], strerror(-result));
            status = 1;
        } else {
            printf("J72s ALSA %s: card %d, device %d\n", names[i], endpoint.card, endpoint.device);
        }
    }
    return status;
}
