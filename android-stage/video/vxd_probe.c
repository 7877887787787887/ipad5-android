/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <time.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define J72S_BUS_PREFIX "platform:207f00000."
#define VXD_DRIVER "apple-vxd"

struct control {
    uint32_t id;
    uint32_t type;
    size_t size;
};

#define PAYLOAD(codec, control, type, structure) \
    { V4L2_CID_STATELESS_##codec##_##control, V4L2_CTRL_TYPE_##type, sizeof(struct structure) }

static const struct control controls[] = {
    PAYLOAD(H264, SPS, H264_SPS, v4l2_ctrl_h264_sps),
    PAYLOAD(H264, PPS, H264_PPS, v4l2_ctrl_h264_pps),
    PAYLOAD(H264, SCALING_MATRIX, H264_SCALING_MATRIX, v4l2_ctrl_h264_scaling_matrix),
    PAYLOAD(H264, DECODE_PARAMS, H264_DECODE_PARAMS, v4l2_ctrl_h264_decode_params),
    PAYLOAD(H264, SLICE_PARAMS, H264_SLICE_PARAMS, v4l2_ctrl_h264_slice_params),
    PAYLOAD(H264, PRED_WEIGHTS, H264_PRED_WEIGHTS, v4l2_ctrl_h264_pred_weights),
    PAYLOAD(HEVC, SPS, HEVC_SPS, v4l2_ctrl_hevc_sps),
    PAYLOAD(HEVC, PPS, HEVC_PPS, v4l2_ctrl_hevc_pps),
    PAYLOAD(HEVC, SCALING_MATRIX, HEVC_SCALING_MATRIX, v4l2_ctrl_hevc_scaling_matrix),
    PAYLOAD(HEVC, SLICE_PARAMS, HEVC_SLICE_PARAMS, v4l2_ctrl_hevc_slice_params),
    PAYLOAD(HEVC, DECODE_PARAMS, HEVC_DECODE_PARAMS, v4l2_ctrl_hevc_decode_params),
};

static int call_ioctl(int fd, unsigned long op, void *arg)
{
    int result;
    do {
        result = ioctl(fd, op, arg);
    } while (result == -1 && errno == EINTR);
    return result;
}

static bool check_nodes(int video, int media)
{
    struct v4l2_capability cap = {0};
    struct media_device_info info = {0};
    if (call_ioctl(video, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return false;
    }
    if (call_ioctl(media, MEDIA_IOC_DEVICE_INFO, &info) < 0) {
        perror("MEDIA_IOC_DEVICE_INFO");
        return false;
    }

    uint32_t flags = cap.capabilities & V4L2_CAP_DEVICE_CAPS
        ? cap.device_caps : cap.capabilities;
    const uint32_t required = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
    if (strncmp((const char *)cap.driver, VXD_DRIVER, sizeof(cap.driver)) != 0 ||
        strncmp(info.model, VXD_DRIVER, sizeof(info.model)) != 0 ||
        strncmp((const char *)cap.bus_info, J72S_BUS_PREFIX, sizeof(J72S_BUS_PREFIX) - 1) != 0 ||
        !memchr(cap.bus_info, '\0', sizeof(cap.bus_info)) ||
        strncmp((const char *)cap.bus_info, info.bus_info, sizeof(info.bus_info)) != 0 ||
        (flags & required) != required) {
        fputs("Expected the same J72s apple-vxd device on both nodes, with multi-planar M2M streaming\n",
              stderr);
        return false;
    }

    printf("Decoder: %s, media hardware revision: 0x%" PRIx32 "\n", cap.bus_info, info.hw_revision);
    return true;
}

static bool has_format(int video, uint32_t type, uint32_t fourcc)
{
    struct v4l2_fmtdesc format = {.type = type};
    for (format.index = 0; format.index < 32; ++format.index) {
        if (call_ioctl(video, VIDIOC_ENUM_FMT, &format) < 0)
            return false;
        if (format.pixelformat == fourcc)
            return true;
    }
    return false;
}

static bool check_formats(int video)
{
    if (!has_format(video, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_H264_SLICE) ||
        !has_format(video, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_HEVC_SLICE) ||
        !has_format(video, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_PIX_FMT_NV12)) {
        fputs("Expected H.264 and HEVC slice formats on OUTPUT and NV12 on CAPTURE\n", stderr);
        return false;
    }

    struct v4l2_format request = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        .fmt.pix_mp = {.width = 1920, .height = 1088, .pixelformat = V4L2_PIX_FMT_NV12},
    };
    if (call_ioctl(video, VIDIOC_TRY_FMT, &request) < 0) {
        perror("VIDIOC_TRY_FMT (NV12)");
        return false;
    }

    const struct v4l2_pix_format_mplane *pix = &request.fmt.pix_mp;
    const uint32_t stride = pix->plane_fmt[0].bytesperline;
    if (pix->pixelformat != V4L2_PIX_FMT_NV12 || pix->num_planes != 1 ||
        pix->width != 1920 || pix->height != 1088 || stride != 2048 ||
        pix->plane_fmt[0].sizeimage != stride * pix->height * 3 / 2) {
        fputs("Unexpected J72s 1088p coded NV12 buffer layout\n", stderr);
        return false;
    }

    printf("CAPTURE: %" PRIu32 "x%" PRIu32 " NV12, one memory plane, stride %" PRIu32
           ", chroma offset %" PRIu32 ", buffer %" PRIu32 " bytes\n",
           pix->width, pix->height, stride, stride * pix->height, pix->plane_fmt[0].sizeimage);
    return true;
}

static bool check_menu(int video, uint32_t id, int64_t expected)
{
    struct v4l2_query_ext_ctrl query = {.id = id};
    if (call_ioctl(video, VIDIOC_QUERY_EXT_CTRL, &query) < 0 ||
        query.type != V4L2_CTRL_TYPE_MENU || query.flags & V4L2_CTRL_FLAG_DISABLED ||
        query.minimum != expected || query.maximum != expected || query.default_value != expected) {
        fprintf(stderr, "Control 0x%08" PRIx32 " must select the VXD fixed menu value %" PRId64 "\n",
                id, expected);
        return false;
    }
    return true;
}

static bool check_controls(int video)
{
    for (size_t i = 0; i < ARRAY_SIZE(controls); ++i) {
        const struct control *expected = &controls[i];
        struct v4l2_query_ext_ctrl query = {.id = expected->id};
        if (call_ioctl(video, VIDIOC_QUERY_EXT_CTRL, &query) < 0 ||
            query.type != expected->type || query.elem_size != expected->size ||
            query.flags & V4L2_CTRL_FLAG_DISABLED || !(query.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) ||
            !query.elems) {
            fprintf(stderr, "Missing or incompatible V4L2 payload for control 0x%08" PRIx32 "\n",
                    expected->id);
            return false;
        }
    }

    return check_menu(video, V4L2_CID_STATELESS_H264_DECODE_MODE, V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED) &&
           check_menu(video, V4L2_CID_STATELESS_H264_START_CODE, V4L2_STATELESS_H264_START_CODE_ANNEX_B) &&
           check_menu(video, V4L2_CID_STATELESS_HEVC_DECODE_MODE, V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED) &&
           check_menu(video, V4L2_CID_STATELESS_HEVC_START_CODE, V4L2_STATELESS_HEVC_START_CODE_ANNEX_B);
}

static bool check_requests(int video, int media)
{
    /* Ask about queue capabilities on the fresh file handle, with zero buffers. */
    struct v4l2_requestbuffers output = {
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (call_ioctl(video, VIDIOC_REQBUFS, &output) < 0) {
        perror("VIDIOC_REQBUFS (OUTPUT count=0)");
        return false;
    }
    if ((output.capabilities & (V4L2_BUF_CAP_SUPPORTS_REQUESTS | V4L2_BUF_CAP_SUPPORTS_DMABUF)) !=
        (V4L2_BUF_CAP_SUPPORTS_REQUESTS | V4L2_BUF_CAP_SUPPORTS_DMABUF)) {
        fputs("OUTPUT queue must support media requests and DMA-BUF\n", stderr);
        return false;
    }

    struct v4l2_requestbuffers capture = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (call_ioctl(video, VIDIOC_REQBUFS, &capture) < 0) {
        perror("VIDIOC_REQBUFS (CAPTURE count=0)");
        return false;
    }
    if (!(capture.capabilities & V4L2_BUF_CAP_SUPPORTS_DMABUF)) {
        fputs("CAPTURE queue must support DMA-BUF\n", stderr);
        return false;
    }

    int request = -1;
    if (call_ioctl(media, MEDIA_IOC_REQUEST_ALLOC, &request) < 0) {
        perror("MEDIA_IOC_REQUEST_ALLOC");
        return false;
    }
    if (request < 0) {
        fputs("Media device returned no request descriptor\n", stderr);
        return false;
    }
    close(request);
    return true;
}

int j72s_vxd_probe(int video, int media)
{
    if (!check_nodes(video, media) || !check_formats(video) ||
        !check_controls(video) || !check_requests(video, media))
        return 1;
    puts("J72s decode ABI qualified: H.264 and HEVC, Annex B frames, media requests. No pictures decoded.");
    return 0;
}

#ifndef J72S_VXD_PROBE_TEST
static int open_node(const char *path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    struct stat node;
    if (fstat(fd, &node) < 0) {
        perror(path);
        close(fd);
        return -1;
    }
    if (!S_ISCHR(node.st_mode)) {
        fprintf(stderr, "%s: expected a character device\n", path);
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s /dev/videoX /dev/mediaY\n", argv[0]);
        return 2;
    }

    const int video = open_node(argv[1]);
    if (video < 0)
        return 1;
    const int media = open_node(argv[2]);
    const int result = media < 0 ? 1 : j72s_vxd_probe(video, media);
    if (media >= 0)
        close(media);
    close(video);
    return result;
}
#endif
