/* SPDX-License-Identifier: Apache-2.0 */
#include "alsa_endpoint.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

static int scenario;

int __wrap_open(const char *name, int flags, ...) {
    int card = -1;
    assert((flags & O_ACCMODE) == O_RDONLY);
    assert(sscanf(name, "/dev/snd/controlC%d", &card) == 1);
    if (card == 0 || (scenario != 1 && card == 5) || (scenario == 3 && card == 6)) return card;
    errno = ENOENT;
    return -1;
}

int __wrap_close(int fd) {
    assert(fd == 0 || fd == 5 || fd == 6);
    return 0;
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    if (request == SNDRV_CTL_IOCTL_CARD_INFO) {
        struct snd_ctl_card_info *info = arg;
        info->card = fd;
        snprintf((char *)info->name, sizeof(info->name), "%s",
                 fd == 0 ? "USB audio" : "iPad5 Speakers");
        return 0;
    }
    assert(fd == 5 || fd == 6);
    if (request == SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE) {
        int *device = arg;
        if (*device < 0) *device = 3;
        else if (*device == 3) *device = 7;
        else if (*device == 7 && scenario == 2) *device = 9;
        else *device = -1;
        return 0;
    }
    assert(request == SNDRV_CTL_IOCTL_PCM_INFO);
    const struct snd_pcm_info *pcm = arg;
    if ((pcm->device == 3) == (pcm->stream == SNDRV_PCM_STREAM_CAPTURE)) return 0;
    errno = ENOENT;
    return -1;
}

int main(void) {
    struct j72s_pcm_endpoint endpoint;
    assert(j72s_find_pcm_endpoint(SNDRV_PCM_STREAM_PLAYBACK, &endpoint) == 0);
    assert(endpoint.card == 5 && endpoint.device == 7);
    assert(j72s_find_pcm_endpoint(SNDRV_PCM_STREAM_CAPTURE, &endpoint) == 0);
    assert(endpoint.card == 5 && endpoint.device == 3);
    scenario = 1;
    assert(j72s_find_pcm_endpoint(SNDRV_PCM_STREAM_PLAYBACK, &endpoint) == -ENODEV);
    scenario = 2;
    assert(j72s_find_pcm_endpoint(SNDRV_PCM_STREAM_PLAYBACK, &endpoint) == -ENOTUNIQ);
    scenario = 3;
    assert(j72s_find_pcm_endpoint(SNDRV_PCM_STREAM_PLAYBACK, &endpoint) == -ENOTUNIQ);
    puts("ALSA endpoint identity, direction and ambiguity checks passed.");
}
