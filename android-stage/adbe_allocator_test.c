/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise minigbm's real format selection with a synthetic DRM driver name. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xf86drm.h>
#include "drv_priv.h"

drmVersionPtr drmGetVersion(int fd) {
    (void)fd;
    drmVersionPtr version = calloc(1, sizeof(*version));
    if (!version) return NULL;
    version->name = strdup("adbe");
    if (!version->name) {
        free(version);
        return NULL;
    }
    version->name_len = strlen(version->name);
    return version;
}

int main(void) {
    struct driver *driver = drv_create(-1);
    if (!driver) {
        fputs("minigbm did not recognize adbe\n", stderr);
        return 1;
    }
    uint32_t format = 0;
    uint64_t usage = 0;
    drv_resolve_format_and_use_flags(driver, DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED,
        BO_USE_SCANOUT | BO_USE_RENDERING, &format, &usage);
    const int passed = format == DRM_FORMAT_XRGB8888 &&
        drv_get_combination(driver, format, usage) &&
        drv_get_combination(driver, DRM_FORMAT_ARGB8888, BO_USE_SCANOUT | BO_USE_RENDERING) &&
        !drv_get_combination(driver, DRM_FORMAT_XRGB8888, BO_USE_CURSOR) &&
        !drv_get_combination(driver, DRM_FORMAT_ABGR8888, BO_USE_SCANOUT) &&
        drv_get_combination(driver, DRM_FORMAT_ABGR8888, BO_USE_RENDERING);
    drv_destroy(driver);
    if (!passed) fputs("ADBE format policy mismatch\n", stderr);
    return passed ? 0 : 1;
}
