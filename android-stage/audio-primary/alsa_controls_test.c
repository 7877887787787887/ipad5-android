/* SPDX-License-Identifier: Apache-2.0 */
#include "alsa_controls.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sound/asound.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

static const char *const names[] = {
    "Output Select", "Speaker Playback Switch", "Speakers Switch",
    "Headphone Playback Switch", "Headphones Switch", "Capture Switch",
};
static unsigned writes[8];
static unsigned values[8];
static unsigned settings[6];
static unsigned num_writes;
static unsigned fail_write_id;
static const char *missing_control;
static bool wrong_card;
static bool capture_readonly;

int __wrap_open(const char *path, int flags, ...) {
    assert(!strcmp(path, "/dev/snd/controlC5"));
    assert((flags & O_ACCMODE) == O_RDWR);
    assert((flags & (O_CLOEXEC | O_NOFOLLOW)) == (O_CLOEXEC | O_NOFOLLOW));
    return 5;
}

int __wrap_close(int fd) {
    assert(fd == 5);
    return 0;
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
    assert(fd == 5);
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    switch (request) {
    case SNDRV_CTL_IOCTL_CARD_INFO: {
        struct snd_ctl_card_info *card = arg;
        card->card = fd;
        snprintf((char *)card->name, sizeof(card->name), "%s",
                 wrong_card ? "unrelated" : "iPad5 Speakers");
        return 0;
    }
    case SNDRV_CTL_IOCTL_ELEM_INFO: {
        struct snd_ctl_elem_info *info = arg;
        assert(info->id.iface == SNDRV_CTL_ELEM_IFACE_MIXER);
        unsigned idx;
        for (idx = 0; idx < sizeof(names) / sizeof(names[0]); ++idx)
            if (!strcmp((const char *)info->id.name, names[idx])) break;
        assert(idx < sizeof(names) / sizeof(names[0]));
        if (missing_control && !strcmp(names[idx], missing_control)) {
            errno = ENOENT;
            return -1;
        }
        info->id.numid = idx + 1;
        info->type = idx ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_ENUMERATED;
        info->count = 1;
        info->access = idx == 5 && capture_readonly ? SNDRV_CTL_ELEM_ACCESS_READ
                                                    : SNDRV_CTL_ELEM_ACCESS_READWRITE;
        if (!idx) {
            info->value.enumerated.items = 2;
            assert(info->value.enumerated.item < 2);
            // Reverse the real order so the test proves label lookup.
            snprintf(info->value.enumerated.name, sizeof(info->value.enumerated.name), "%s",
                     info->value.enumerated.item == 0 ? "Headphones" : "Speakers");
        }
        return 0;
    }
    case SNDRV_CTL_IOCTL_ELEM_READ: {
        struct snd_ctl_elem_value *value = arg;
        assert(value->id.numid >= 1 && value->id.numid <= sizeof(settings) / sizeof(settings[0]));
        if (value->id.numid == 1) value->value.enumerated.item[0] = settings[0];
        else value->value.integer.value[0] = settings[value->id.numid - 1];
        return 0;
    }
    case SNDRV_CTL_IOCTL_ELEM_WRITE: {
        const struct snd_ctl_elem_value *value = arg;
        assert(num_writes < sizeof(writes) / sizeof(writes[0]));
        assert(value->id.numid >= 1 && value->id.numid <= sizeof(names) / sizeof(names[0]));
        if (value->id.numid == fail_write_id) {
            errno = EIO;
            return -1;
        }
        writes[num_writes] = value->id.numid;
        values[num_writes] = value->id.numid == 1 ? value->value.enumerated.item[0]
                                                : (unsigned)value->value.integer.value[0];
        settings[value->id.numid - 1] = values[num_writes];
        ++num_writes;
        return 0;
    }
    default:
        assert(!"Unexpected ALSA ioctl");
        return -1;
    }
}

int main(void) {
    assert(j72s_set_audio_output(5, J72S_AUDIO_SPEAKERS) == 0);
    assert(num_writes == 3);
    assert(writes[0] == 1 && values[0] == 1);
    assert(writes[1] == 2 && values[1] == 1);
    assert(writes[2] == 3 && values[2] == 1);

    assert(j72s_set_audio_output(5, J72S_AUDIO_HEADPHONES) == 0);
    assert(num_writes == 6);
    assert(writes[3] == 1 && values[3] == 0);
    assert(writes[4] == 4 && values[4] == 1);
    assert(writes[5] == 5 && values[5] == 1);

    assert(j72s_set_capture_switch(5, true) == 0);
    assert(j72s_set_capture_switch(5, false) == 0);
    assert(num_writes == 8);
    assert(writes[6] == 6 && values[6] == 1);
    assert(writes[7] == 6 && values[7] == 0);

    num_writes = 0;
    missing_control = "Headphones Switch";
    assert(j72s_set_audio_output(5, J72S_AUDIO_HEADPHONES) == -ENOENT);
    assert(!num_writes);
    capture_readonly = true;
    assert(j72s_set_capture_switch(5, true) == -EINVAL);
    wrong_card = true;
    assert(j72s_set_capture_switch(5, false) == -ENODEV);
    assert(j72s_set_capture_switch(-1, true) == -EINVAL);
    assert(!num_writes);

    wrong_card = false;
    missing_control = NULL;
    settings[1] = 0;
    fail_write_id = 3;
    assert(j72s_set_audio_output(5, J72S_AUDIO_SPEAKERS) == -EIO);
    assert(num_writes == 4);
    assert(writes[0] == 1 && values[0] == 1);
    assert(writes[1] == 2 && values[1] == 1);
    assert(writes[2] == 2 && values[2] == 0);
    assert(writes[3] == 1 && values[3] == 0);
}
