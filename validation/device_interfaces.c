/* SPDX-License-Identifier: MIT
 * Query Linux DRM and input capabilities without modesetting or event injection.
 * A successful query is not proof of rendering or touch functionality.
 */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <drm.h>

struct touch_caps {
    bool direct;
    bool contact_key;
    bool tracking;
    struct input_absinfo x;
    struct input_absinfo y;
    struct input_absinfo slot;
};

static bool has_bit(const unsigned char *bits, unsigned int code)
{
    return (bits[code / 8] & (1U << (code % 8))) != 0;
}

static bool is_direct_multitouch(const struct touch_caps *caps)
{
    return caps->direct && caps->contact_key && caps->tracking &&
        caps->x.maximum > caps->x.minimum && caps->y.maximum > caps->y.minimum &&
        caps->slot.minimum == 0 && caps->slot.maximum >= 1 &&
        caps->slot.maximum < 64;
}

static bool has_board_buttons(const unsigned char *keys)
{
    return has_bit(keys, KEY_HOMEPAGE) && has_bit(keys, KEY_POWER) &&
        has_bit(keys, KEY_VOLUMEDOWN) && has_bit(keys, KEY_VOLUMEUP);
}

static void describe_board_controls(int fd, const char *name, const unsigned char *keys)
{
    if (strcmp(name, "gpio-keys") == 0 && keys && has_board_buttons(keys))
        puts("  iPad GPIO Home, Power, Volume Down and Volume Up keys advertised");

    if (strcmp(name, "iPad Smart Cover") != 0)
        return;
    unsigned char switches[SW_MAX / 8 + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof(switches)), switches) < 0 ||
        !has_bit(switches, SW_MACHINE_COVER))
        return;
    unsigned char state[SW_MAX / 8 + 1] = {0};
    if (ioctl(fd, EVIOCGSW(sizeof(state)), state) >= 0)
        printf("  Smart Cover raw switch state %d\n", has_bit(state, SW_MACHINE_COVER));
    else
        puts("  Smart Cover switch advertised; current state unavailable");
}

static void describe_input(int fd, const char *path)
{
    char name[128] = {0};
    unsigned char properties[INPUT_PROP_MAX / 8 + 1] = {0};
    unsigned char keys[KEY_MAX / 8 + 1] = {0};
    unsigned char axes[ABS_MAX / 8 + 1] = {0};
    struct touch_caps caps = {0};

    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0)
        printf("%s: %s\n", path, name);
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &caps.x) == 0)
        printf("  multitouch X range %d..%d\n", caps.x.minimum, caps.x.maximum);
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &caps.y) == 0)
        printf("  multitouch Y range %d..%d\n", caps.y.minimum, caps.y.maximum);
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &caps.slot) == 0)
        printf("  slot range %d..%d\n", caps.slot.minimum, caps.slot.maximum);

    const bool have_keys = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0;
    describe_board_controls(fd, name, have_keys ? keys : NULL);
    if (!have_keys ||
        ioctl(fd, EVIOCGPROP(sizeof(properties)), properties) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(axes)), axes) < 0)
        return;

    caps.direct = has_bit(properties, INPUT_PROP_DIRECT);
    caps.contact_key = has_bit(keys, BTN_TOUCH);
    caps.tracking = has_bit(axes, ABS_MT_TRACKING_ID);
    if (is_direct_multitouch(&caps))
        printf("  direct Type-B multitouch advertised (%d slots)\n",
               caps.slot.maximum + 1);
}

static bool buttons_self_test(void)
{
    static const unsigned int codes[] = {KEY_HOMEPAGE, KEY_POWER, KEY_VOLUMEDOWN, KEY_VOLUMEUP};
    unsigned char keys[KEY_MAX / 8 + 1] = {0};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        if (has_board_buttons(keys))
            return false;
        keys[codes[i] / 8] |= 1U << (codes[i] % 8);
    }
    return has_board_buttons(keys);
}

static bool touch_self_test(void)
{
    const struct touch_caps valid = {
        .direct = true,
        .contact_key = true,
        .tracking = true,
        .x = {.minimum = -100, .maximum = 2000},
        .y = {.maximum = 1500},
        .slot = {.maximum = 9},
    };
    struct touch_caps caps = valid;
    if (!is_direct_multitouch(&caps))
        return false;
    caps.direct = false;
    if (is_direct_multitouch(&caps))
        return false;
    caps = valid;
    caps.contact_key = false;
    if (is_direct_multitouch(&caps))
        return false;
    caps = valid;
    caps.tracking = false;
    if (is_direct_multitouch(&caps))
        return false;
    caps = valid;
    caps.x.maximum = caps.x.minimum;
    if (is_direct_multitouch(&caps))
        return false;
    caps = valid;
    caps.y.maximum = -1;
    if (is_direct_multitouch(&caps))
        return false;
    caps = valid;
    caps.slot.minimum = 1;
    if (is_direct_multitouch(&caps))
        return false;
    caps = valid;
    caps.slot.maximum = 0;
    if (is_direct_multitouch(&caps))
        return false;
    caps.slot.maximum = 64;
    return !is_direct_multitouch(&caps);
}

static int device_fd(const char *p) {
    int fd=open(p,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    struct stat st;
    if(fd>=0 && (fstat(fd,&st)!=0 || !S_ISCHR(st.st_mode))) {
        close(fd); errno=ENODEV; return -1;
    }
    return fd;
}
static void scan(const char *pattern,int is_input) {
    glob_t g={0};
    if(glob(pattern,0,NULL,&g)!=0) {
        printf("%s: no nodes found\n",pattern); globfree(&g); return;
    }
    for(size_t i=0;i<g.gl_pathc;i++){
        const char *p=g.gl_pathv[i];
        int fd=device_fd(p);
        if(fd<0){printf("%s: %s\n",p,strerror(errno));continue;}
        if(is_input){
            describe_input(fd, p);
        }else{
            struct drm_version v={0};
            char name[128]={0};
            v.name=name;v.name_len=sizeof(name)-1;
            if(ioctl(fd,DRM_IOCTL_VERSION,&v)==0)
                printf("%s: DRM %s %d.%d.%d\n",p,name,v.version_major,v.version_minor,v.version_patchlevel);
            else printf("%s: DRM version query failed: %s\n",p,strerror(errno));
            struct drm_get_cap c={.capability=DRM_CAP_DUMB_BUFFER};
            if(ioctl(fd,DRM_IOCTL_GET_CAP,&c)==0)
                printf("  dumb-buffer capability %llu (not GPU acceleration)\n",(unsigned long long)c.value);
        }
        close(fd);
    }
    globfree(&g);
}
int main(int argc,char **argv){
    if(argc==2 && strcmp(argv[1],"--self-test")==0){
        int fd=device_fd("/proc/self/status");
        if(fd>=0){close(fd);return 1;}
        fd=device_fd("/dev/null");
        if(fd<0)return 1;
        close(fd);
        if (!touch_self_test()) {
            fputs("multitouch capability self-test failed\n", stderr);
            return 1;
        }
        if (!buttons_self_test()) {
            fputs("GPIO buttons capability self-test failed\n", stderr);
            return 1;
        }
        puts("read-only device access, multitouch and GPIO buttons capability self-tests passed");
        return 0;
    }
    scan("/dev/dri/card*",0);
    scan("/dev/dri/renderD*",0);
    scan("/dev/input/event*",1);
    puts("No hardware function or performance claim is made by this inventory.");
    return 0;
}
