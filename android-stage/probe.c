/* SPDX-License-Identifier: MIT
 * Read-only Linux runtime probe for experimental J72s Android bring-up.
 * Not Android init, not a bootloader, not a hardware driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}
static int board_matches(void) {
    FILE *f = fopen("/proc/device-tree/compatible", "rb");
    if (!f) return 0;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    size_t i = 0;
    while (i < n) {
        size_t len = strnlen(buf + i, n - i);
        if (len == strlen("apple,j72s") && memcmp(buf+i, "apple,j72s", len)==0)
            return 1;
        i += len + 1;
    }
    return 0;
}
int main(int argc, char **argv) {
    struct utsname u;
    if (uname(&u) != 0) { perror("uname"); return 2; }
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) return 2;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        if (!exists("/proc/self") || exists("/proc/self/j72s-nonexistent-test")) return 2;
        printf("self-test passed; runner architecture=%s page=%ld; NOT a device test\n", u.machine,page);
        return 0;
    }
    printf("kernel=%s architecture=%s page_size=%ld\n",u.release,u.machine,page);
    int board = board_matches();
    printf("j72s_device_tree=%s\n", board?"present":"not detected");
    const char *paths[] = {"/dev/binder","/dev/hwbinder","/dev/vndbinder",
       "/dev/binderfs","/sys/fs/selinux","/sys/fs/cgroup","/dev/dri/card0",
       "/dev/input/event0","/sys/class/net","/sys/class/power_supply"};
    for (size_t i=0; i<sizeof(paths)/sizeof(paths[0]); i++)
        printf("%s=%s\n", paths[i], exists(paths[i])?"present":"absent or inaccessible");
    puts("Presence is not functionality. No device nodes opened or modified.");
    return board && page == 16384 ? 0 : 3;
}
