/* Native Android userspace test, not a HAL implementation. */
#include <aaudio/AAudio.h>
#include <vulkan/vulkan.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void property(const char *key) {
    char value[PROP_VALUE_MAX]={0};
    int n=__system_property_get(key,value);
    printf("%s: %s\n",key,n>0?value:"unavailable");
}
static int graphics(void) {
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="J72s diagnostic",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance=VK_NULL_HANDLE;
    VkResult r=vkCreateInstance(&ci,NULL,&instance);
    if(r!=VK_SUCCESS){printf("Vulkan instance failed: %d\n",r);return 1;}
    uint32_t n=0;
    r=vkEnumeratePhysicalDevices(instance,&n,NULL);
    if(r!=VK_SUCCESS || n==0 || n>64){printf("Vulkan enumeration unavailable: %d (%u)\n",r,n);vkDestroyInstance(instance,NULL);return 1;}
    VkPhysicalDevice devices[64];
    r=vkEnumeratePhysicalDevices(instance,&n,devices);
    int hardware=0;
    if(r==VK_SUCCESS || r==VK_INCOMPLETE) for(uint32_t i=0;i<n;i++){
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[i],&p);
        printf("Vulkan device %s type=%u vendor=0x%x api=%u.%u\n",p.deviceName,(unsigned)p.deviceType,p.vendorID,VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion));
        if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU || p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) hardware=1;
    }
    vkDestroyInstance(instance,NULL);
    puts("Enumeration only; no rendering or performance claim.");
    return hardware?0:1;
}
static int audio(void) {
    AAudioStreamBuilder *builder=NULL;
    AAudioStream *stream=NULL;
    aaudio_result_t r=AAudio_createStreamBuilder(&builder);
    if(r!=AAUDIO_OK){printf("Audio builder failed: %d\n",r);return 1;}
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
    r=AAudioStreamBuilder_openStream(builder,&stream);
    AAudioStreamBuilder_delete(builder);
    if(r!=AAUDIO_OK){printf("Audio output unavailable: %s\n",AAudio_convertResultToText(r));return 1;}
    printf("Output stream: %d Hz, %d channels, burst %d frames, performance mode %d\n",
      AAudioStream_getSampleRate(stream),AAudioStream_getChannelCount(stream),
      AAudioStream_getFramesPerBurst(stream),AAudioStream_getPerformanceMode(stream));
    AAudioStream_close(stream);
    puts("Stream opened and closed without playing sound; physical speaker test still required.");
    return 0;
}
int main(int argc,char **argv){
    if(argc==2 && !strcmp(argv[1],"--self-test")){puts("J72s Android native probe ABI version 1");return 0;}
    struct utsname u;
    if(uname(&u)==0)printf("Kernel: %s %s, pagesize=%ld\n",u.release,u.machine,sysconf(_SC_PAGESIZE));
    property("ro.product.device"); property("ro.build.version.release"); property("ro.hardware");
    if(argc==2 && !strcmp(argv[1],"--graphics"))return graphics();
    if(argc==2 && !strcmp(argv[1],"--audio"))return audio();
    if(argc!=1){fputs("Usage: native-interfaces [--graphics|--audio|--self-test]\n",stderr);return 2;}
    puts("Use explicit graphics or audio test after Android boots.");
    return 0;
}
