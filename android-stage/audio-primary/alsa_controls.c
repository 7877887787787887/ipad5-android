/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "alsa_controls.h"

#include <errno.h>
#include <fcntl.h>
#include <sound/asound.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int open_board_control(int card) {
    if (card < 0 || card >= 32) return -EINVAL;
    char path[40];
    snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
    const int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -errno;
    struct snd_ctl_card_info info = {0};
    if (ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &info) < 0) {
        const int result = -errno;
        close(fd);
        return result;
    }
    if (info.card != card ||
        strncmp((const char *)info.name, "iPad5 Speakers", sizeof(info.name))) {
        close(fd);
        return -ENODEV;
    }
    return fd;
}

static int control_info(int fd, const char *name, snd_ctl_elem_type_t type,
                        struct snd_ctl_elem_info *info) {
    *info = (struct snd_ctl_elem_info){0};
    info->id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
    snprintf((char *)info->id.name, sizeof(info->id.name), "%s", name);
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, info) < 0) return -errno;
    if (info->type != type || info->count != 1 || !(info->access & SNDRV_CTL_ELEM_ACCESS_WRITE))
        return -EINVAL;
    return 0;
}

static int write_control(int fd, struct snd_ctl_elem_value *control) {
    return ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, control) < 0 ? -errno : 0;
}

static int write_switch(int fd, const struct snd_ctl_elem_info *info, bool enabled) {
    struct snd_ctl_elem_value value = {.id = info->id};
    value.value.integer.value[0] = enabled;
    return write_control(fd, &value);
}

int j72s_set_capture_switch(int card, bool enabled) {
    const int fd = open_board_control(card);
    if (fd < 0) return fd;
    struct snd_ctl_elem_info control;
    int result = control_info(fd, "Capture Switch", SNDRV_CTL_ELEM_TYPE_BOOLEAN, &control);
    if (result == 0) result = write_switch(fd, &control, enabled);
    close(fd);
    return result;
}

static int output_item(int fd, const struct snd_ctl_elem_info *info, const char *name,
                       unsigned *selected) {
    if (info->value.enumerated.items != 2) return -EINVAL;
    for (unsigned i = 0; i < info->value.enumerated.items; ++i) {
        struct snd_ctl_elem_info item = {.id = info->id};
        item.value.enumerated.item = i;
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &item) < 0) return -errno;
        if (!strncmp(item.value.enumerated.name, name, sizeof(item.value.enumerated.name))) {
            *selected = i;
            return 0;
        }
    }
    return -EINVAL;
}

static int apply_output(int fd, const struct snd_ctl_elem_info *route, unsigned selected,
                        const struct snd_ctl_elem_info *playback,
                        const struct snd_ctl_elem_info *pin) {
    struct snd_ctl_elem_value previous[] = {
        {.id = route->id}, {.id = playback->id}, {.id = pin->id},
    };
    struct snd_ctl_elem_value updated[] = {
        {.id = route->id}, {.id = playback->id}, {.id = pin->id},
    };
    updated[0].value.enumerated.item[0] = selected;
    updated[1].value.integer.value[0] = 1;
    updated[2].value.integer.value[0] = 1;
    const size_t count = sizeof(previous) / sizeof(previous[0]);
    for (size_t i = 0; i < count; ++i) {
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &previous[i]) < 0) return -errno;
    }
    // The codec's DAPM enum disconnects the previous amplifier before driving the new one.
    for (size_t i = 0; i < count; ++i) {
        const int result = write_control(fd, &updated[i]);
        if (result < 0) {
            while (i > 0) write_control(fd, &previous[--i]);
            return result;
        }
    }
    return 0;
}

int j72s_set_audio_output(int card, enum j72s_audio_output output) {
    if (output != J72S_AUDIO_SPEAKERS && output != J72S_AUDIO_HEADPHONES) return -EINVAL;
    const int fd = open_board_control(card);
    if (fd < 0) return fd;
    const bool headphones = output == J72S_AUDIO_HEADPHONES;
    const char *output_name = headphones ? "Headphones" : "Speakers";
    const char *playback_name = headphones ? "Headphone Playback Switch" : "Speaker Playback Switch";
    const char *pin_name = headphones ? "Headphones Switch" : "Speakers Switch";
    struct snd_ctl_elem_info route, playback, pin;
    unsigned selected;
    int result = control_info(fd, "Output Select", SNDRV_CTL_ELEM_TYPE_ENUMERATED, &route);
    if (result == 0) result = output_item(fd, &route, output_name, &selected);
    if (result == 0) result = control_info(fd, playback_name, SNDRV_CTL_ELEM_TYPE_BOOLEAN, &playback);
    if (result == 0) result = control_info(fd, pin_name, SNDRV_CTL_ELEM_TYPE_BOOLEAN, &pin);
    if (result == 0) result = apply_output(fd, &route, selected, &playback, &pin);
    close(fd);
    return result;
}
