/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "alsa_endpoint.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>

static int find_device(int fd, int stream, int *selected) {
    *selected = -1;
    int device = -1;
    for (;;) {
        const int previous = device;
        if (ioctl(fd, SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, &device) < 0) return -errno;
        if (device == -1) return 0;
        if (device <= previous) return -EIO;
        struct snd_pcm_info info = {.device = (unsigned)device, .stream = stream};
        if (ioctl(fd, SNDRV_CTL_IOCTL_PCM_INFO, &info) == 0) {
            if (*selected != -1) return -ENOTUNIQ;
            *selected = device;
        } else if (errno != ENOENT) {
            return -errno;
        }
    }
}

int j72s_find_pcm_endpoint(int stream, struct j72s_pcm_endpoint *endpoint) {
    if (!endpoint || (stream != SNDRV_PCM_STREAM_PLAYBACK && stream != SNDRV_PCM_STREAM_CAPTURE))
        return -EINVAL;
    struct j72s_pcm_endpoint found = {-1, -1};
    int scan_error = -ENODEV;
    for (int card = 0; card < 32; ++card) {
        char path[40];
        snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
        const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            if (errno != ENOENT && errno != ENODEV) scan_error = -errno;
            continue;
        }
        struct snd_ctl_card_info info = {0};
        if (ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &info) < 0) {
            scan_error = -errno;
            close(fd);
            continue;
        }
        if (info.card != card ||
            strncmp((const char *)info.name, "iPad5 Speakers", sizeof(info.name))) {
            close(fd);
            continue;
        }
        int device;
        const int result = find_device(fd, stream, &device);
        close(fd);
        if (result < 0) return result;
        if (device < 0) continue;
        if (found.card != -1) return -ENOTUNIQ;
        found = (struct j72s_pcm_endpoint){card, device};
    }
    if (found.card == -1) return scan_error;
    *endpoint = found;
    return 0;
}
