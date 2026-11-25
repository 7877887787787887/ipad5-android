/* SPDX-License-Identifier: MIT
 * Linux hardware inventory. No device register writes or driver loading.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <unistd.h>
#include <sys/utsname.h>
static void files(const char *pattern) {
    glob_t g = {0};
    if (glob(pattern,0,NULL,&g)!=0) { printf("%s: unavailable\n",pattern); globfree(&g); return; }
    for (size_t i=0;i<g.gl_pathc;i++) {
        FILE *f=fopen(g.gl_pathv[i],"r");
        if (!f) continue;
        char s[512];
        printf("%s: ",g.gl_pathv[i]);
        if (fgets(s,sizeof(s),f)) { s[strcspn(s,"\n")]=0; printf("%s",s); }
        puts(""); fclose(f);
    }
    globfree(&g);
}
int main(int argc,char **argv) {
    if(argc==2 && strcmp(argv[1],"--self-test")==0) {
        struct utsname u;
        if(uname(&u)!=0 || sysconf(_SC_PAGESIZE)<4096) return 1;
        puts("runner self-test passed; not hardware validation"); return 0;
    }
    puts("Read-only hardware report; presence does not prove functionality.");
    const char *p[]={
        "/sys/class/drm/card*-*/status","/sys/class/drm/card*-*/modes",
        "/sys/class/input/event*/device/name","/sys/class/input/event*/device/capabilities/abs",
        "/sys/class/backlight/*/max_brightness","/sys/class/backlight/*/actual_brightness",
        "/sys/class/devfreq/*/available_frequencies","/sys/class/devfreq/*/cur_freq",
        "/sys/class/net/*/operstate","/sys/class/net/*/statistics/rx_bytes",
        "/sys/class/net/*/statistics/tx_bytes","/sys/class/bluetooth/*/name",
        "/sys/class/power_supply/*/type","/sys/class/power_supply/*/status",
        "/sys/class/power_supply/*/capacity","/sys/class/power_supply/*/voltage_now",
        "/sys/class/power_supply/*/current_now","/sys/class/power_supply/*/temp",
        "/sys/class/thermal/thermal_zone*/type","/sys/class/thermal/thermal_zone*/temp",
        "/sys/devices/system/cpu/cpufreq/policy*/scaling_available_frequencies",
        "/sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq"};
    for(size_t i=0;i<sizeof(p)/sizeof(p[0]);i++) files(p[i]);
    return 0;
}
