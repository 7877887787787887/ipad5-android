/* SPDX-License-Identifier: MIT
 * Query Linux DRM and input capabilities without modesetting or event injection.
 * A successful query is not proof of rendering or touch functionality.
 */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <drm.h>
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
            char name[128]={0};
            struct input_absinfo a={0};
            if(ioctl(fd,EVIOCGNAME(sizeof(name)-1),name)>=0)
                printf("%s: %s\n",p,name);
            if(ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&a)==0)
                printf("  multitouch X range %d..%d\n",a.minimum,a.maximum);
            if(ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&a)==0)
                printf("  multitouch Y range %d..%d\n",a.minimum,a.maximum);
            if(ioctl(fd,EVIOCGABS(ABS_MT_SLOT),&a)==0)
                printf("  slot range %d..%d\n",a.minimum,a.maximum);
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
        close(fd); puts("read-only device access self-test passed");return 0;
    }
    scan("/dev/dri/card*",0);
    scan("/dev/dri/renderD*",0);
    scan("/dev/input/event*",1);
    puts("No hardware function or performance claim is made by this inventory.");
    return 0;
}
