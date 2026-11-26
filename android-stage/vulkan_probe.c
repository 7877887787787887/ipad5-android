/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

enum { PROBE_FAILED = 1, PROBE_TIMED_OUT = 2, NO_DEVICE = 77 };

static int submit_and_read(VkPhysicalDevice physical, uint32_t family) {
    enum { word_count = 1024 };
    const VkDeviceSize byte_count = word_count * sizeof(uint32_t);
    const uint32_t pattern = UINT32_C(0x4a373273);
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
    };
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;
    int status = PROBE_FAILED;
    VkResult result;
    const char *operation;

#define CHECK(call) do { operation = #call; result = (call); if (result != VK_SUCCESS) goto done; } while (0)
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = byte_count, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t type = 0;
    while (type < properties.memoryTypeCount &&
           (!(req.memoryTypeBits & (UINT32_C(1) << type)) ||
            !(properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) {
        ++type;
    }
    if (type == properties.memoryTypeCount) {
        operation = "Select host-visible memory";
        result = VK_ERROR_FEATURE_NOT_PRESENT;
        goto done;
    }
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type,
    };
    CHECK(vkAllocateMemory(device, &memory_info, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family,
    };
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    CHECK(vkBeginCommandBuffer(command, &begin));
    vkCmdFillBuffer(command, buffer, 0, byte_count, pattern);
    const VkMemoryBarrier readback = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &readback, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    const VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    const VkSubmitInfo submission = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command,
    };
    CHECK(vkQueueSubmit(queue, 1, &submission, fence));
    result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(5000000000));
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
        // The submission may still be pending. Process exit reclaims its resources.
        fprintf(stderr, "Vulkan fence did not complete: %d\n", result);
        return PROBE_TIMED_OUT;
    }
    if (result != VK_SUCCESS) {
        operation = "vkWaitForFences";
        goto done;
    }
    CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    if (!(properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        const VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = memory, .size = VK_WHOLE_SIZE,
        };
        CHECK(vkInvalidateMappedMemoryRanges(device, 1, &range));
    }
    const uint32_t *words = mapped;
    for (unsigned i = 0; i < word_count; ++i) {
        if (words[i] != pattern) {
            fprintf(stderr, "Vulkan readback mismatch at word %u\n", i);
            goto done;
        }
    }
    printf("Vulkan submission and readback verified (%u bytes).\n", (unsigned)byte_count);
    status = 0;
done:
    if (result != VK_SUCCESS) fprintf(stderr, "%s failed: %d\n", operation, result);
    if (mapped) vkUnmapMemory(device, memory);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (pool) vkDestroyCommandPool(device, pool, NULL);
    if (buffer) vkDestroyBuffer(device, buffer, NULL);
    if (memory) vkFreeMemory(device, memory, NULL);
    if (device) vkDestroyDevice(device, NULL);
    return status;
#undef CHECK
}

int main(int argc, char **argv) {
    bool allow_software = false;
#ifndef __ANDROID__
    allow_software = argc == 2 && strcmp(argv[1], "--allow-software") == 0;
#endif
    if (argc != 1 && !allow_software) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return PROBE_FAILED;
    }
    const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "J72s Vulkan probe", .apiVersion = VK_API_VERSION_1_0,
    };
    const VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
    };
    VkInstance instance;
    VkResult result = vkCreateInstance(&info, NULL, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Vulkan instance unavailable: %d\n", result);
        return allow_software && result == VK_ERROR_INCOMPATIBLE_DRIVER ? NO_DEVICE : PROBE_FAILED;
    }
    VkPhysicalDevice devices[64];
    uint32_t count = sizeof(devices) / sizeof(devices[0]);
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    int status = allow_software ? NO_DEVICE : PROBE_FAILED;
    if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
        for (uint32_t i = 0; i < count; ++i) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[i], &properties);
            if (!allow_software && properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
                properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, NULL);
            if (!families) continue;
            VkQueueFamilyProperties *queues = calloc(families, sizeof(*queues));
            if (!queues) {
                perror("Allocate Vulkan queue-family list");
                status = PROBE_FAILED;
                break;
            }
            vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, queues);
            uint32_t family = 0;
            while (family < families &&
                   !(queues[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) ++family;
            free(queues);
            if (family == families) continue;
            printf("Vulkan device: %s (vendor %#x, type %u)\n", properties.deviceName,
                   properties.vendorID, (unsigned)properties.deviceType);
            status = submit_and_read(devices[i], family);
            break;
        }
    } else {
        fprintf(stderr, "Vulkan device enumeration failed: %d\n", result);
        status = PROBE_FAILED;
    }
    if (status != PROBE_TIMED_OUT) vkDestroyInstance(instance, NULL);
    return status;
}
