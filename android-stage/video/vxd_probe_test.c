/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum fault {
    NONE, WRONG_MEDIA, STATEFUL_ONLY, WRONG_STRIDE, WRONG_PAYLOAD,
    WRONG_START_CODE, NO_OUTPUT_REQUEST, NO_CAPTURE_DMABUF, NO_REQUEST_ALLOC,
};

enum { VIDEO_FD = 10, MEDIA_FD, REQUEST_FD };
static enum fault fault;
static bool interrupted;
static unsigned int requests_closed;

int j72s_vxd_probe(int video, int media);

static int invalid(void)
{
    errno = EINVAL;
    return -1;
}

static int query_control(struct v4l2_query_ext_ctrl *query)
{
#define PAYLOAD(codec, name, type_name, structure) \
    case V4L2_CID_STATELESS_##codec##_##name: \
        query->type = V4L2_CTRL_TYPE_##type_name; \
        query->elem_size = sizeof(struct structure); \
        break
    switch (query->id) {
    PAYLOAD(H264, SPS, H264_SPS, v4l2_ctrl_h264_sps);
    PAYLOAD(H264, PPS, H264_PPS, v4l2_ctrl_h264_pps);
    PAYLOAD(H264, SCALING_MATRIX, H264_SCALING_MATRIX, v4l2_ctrl_h264_scaling_matrix);
    PAYLOAD(H264, DECODE_PARAMS, H264_DECODE_PARAMS, v4l2_ctrl_h264_decode_params);
    PAYLOAD(H264, SLICE_PARAMS, H264_SLICE_PARAMS, v4l2_ctrl_h264_slice_params);
    PAYLOAD(H264, PRED_WEIGHTS, H264_PRED_WEIGHTS, v4l2_ctrl_h264_pred_weights);
    PAYLOAD(HEVC, SPS, HEVC_SPS, v4l2_ctrl_hevc_sps);
    PAYLOAD(HEVC, PPS, HEVC_PPS, v4l2_ctrl_hevc_pps);
    PAYLOAD(HEVC, SCALING_MATRIX, HEVC_SCALING_MATRIX, v4l2_ctrl_hevc_scaling_matrix);
    PAYLOAD(HEVC, SLICE_PARAMS, HEVC_SLICE_PARAMS, v4l2_ctrl_hevc_slice_params);
    PAYLOAD(HEVC, DECODE_PARAMS, HEVC_DECODE_PARAMS, v4l2_ctrl_hevc_decode_params);
    default:
        query->type = V4L2_CTRL_TYPE_MENU;
        switch (query->id) {
        case V4L2_CID_STATELESS_H264_DECODE_MODE:
            query->default_value = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED;
            break;
        case V4L2_CID_STATELESS_H264_START_CODE:
            query->default_value = V4L2_STATELESS_H264_START_CODE_ANNEX_B;
            break;
        case V4L2_CID_STATELESS_HEVC_DECODE_MODE:
            query->default_value = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED;
            break;
        case V4L2_CID_STATELESS_HEVC_START_CODE:
            query->default_value = fault == WRONG_START_CODE
                ? V4L2_STATELESS_HEVC_START_CODE_NONE : V4L2_STATELESS_HEVC_START_CODE_ANNEX_B;
            break;
        default:
            return invalid();
        }
        query->minimum = query->maximum = query->default_value;
        return 0;
    }
#undef PAYLOAD
    if (fault == WRONG_PAYLOAD && query->id == V4L2_CID_STATELESS_HEVC_SPS)
        query->elem_size--;
    query->flags = V4L2_CTRL_FLAG_HAS_PAYLOAD;
    query->elems = 1;
    return 0;
}

int __wrap_ioctl(int fd, unsigned long op, ...)
{
    va_list args;
    va_start(args, op);
    void *ptr = va_arg(args, void *);
    va_end(args);

    assert(fd == (op == MEDIA_IOC_DEVICE_INFO || op == MEDIA_IOC_REQUEST_ALLOC ? MEDIA_FD : VIDEO_FD));
    switch (op) {
    case VIDIOC_QUERYCAP: {
        if (!interrupted) {
            interrupted = true;
            errno = EINTR;
            return -1;
        }
        struct v4l2_capability *cap = ptr;
        strcpy((char *)cap->driver, "apple-vxd");
        strcpy((char *)cap->bus_info, "platform:207f00000.video-codec");
        cap->capabilities = V4L2_CAP_DEVICE_CAPS;
        cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
        return 0;
    }
    case MEDIA_IOC_DEVICE_INFO: {
        struct media_device_info *info = ptr;
        strcpy(info->model, "apple-vxd");
        strcpy(info->bus_info, fault == WRONG_MEDIA
               ? "platform:207f10000.video-codec" : "platform:207f00000.video-codec");
        return 0;
    }
    case VIDIOC_ENUM_FMT: {
        struct v4l2_fmtdesc *format = ptr;
        if (format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && format->index < 2) {
            format->pixelformat = format->index == 0
                ? (fault == STATEFUL_ONLY ? V4L2_PIX_FMT_H264 : V4L2_PIX_FMT_H264_SLICE)
                : V4L2_PIX_FMT_HEVC_SLICE;
        } else if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && format->index == 0) {
            format->pixelformat = V4L2_PIX_FMT_NV12;
        } else {
            return invalid();
        }
        return 0;
    }
    case VIDIOC_TRY_FMT: {
        struct v4l2_format *format = ptr;
        assert(format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
        struct v4l2_pix_format_mplane *pix = &format->fmt.pix_mp;
        assert(pix->width == 1920 && pix->height == 1088 && pix->pixelformat == V4L2_PIX_FMT_NV12);
        pix->num_planes = 1;
        pix->plane_fmt[0].bytesperline = fault == WRONG_STRIDE ? 1920 : 2048;
        pix->plane_fmt[0].sizeimage = pix->plane_fmt[0].bytesperline * pix->height * 3 / 2;
        return 0;
    }
    case VIDIOC_QUERY_EXT_CTRL:
        return query_control(ptr);
    case VIDIOC_REQBUFS: {
        struct v4l2_requestbuffers *query = ptr;
        assert(query->count == 0 && query->memory == V4L2_MEMORY_MMAP);
        query->capabilities = V4L2_BUF_CAP_SUPPORTS_DMABUF;
        if (query->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && fault != NO_OUTPUT_REQUEST)
            query->capabilities |= V4L2_BUF_CAP_SUPPORTS_REQUESTS;
        if (query->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && fault == NO_CAPTURE_DMABUF)
            query->capabilities = 0;
        return 0;
    }
    case MEDIA_IOC_REQUEST_ALLOC:
        if (fault == NO_REQUEST_ALLOC)
            return invalid();
        *(int *)ptr = REQUEST_FD;
        return 0;
    default:
        assert(false && "Probe issued an unexpected ioctl");
        return invalid();
    }
}

int __wrap_close(int fd)
{
    assert(fd == REQUEST_FD);
    requests_closed++;
    return 0;
}

int main(void)
{
    for (fault = NONE; fault <= NO_REQUEST_ALLOC; ++fault) {
        interrupted = false;
        requests_closed = 0;
        assert(j72s_vxd_probe(VIDEO_FD, MEDIA_FD) == (fault == NONE ? 0 : 1));
        assert(interrupted);
        assert(requests_closed == (fault == NONE ? 1U : 0U));
    }
    puts("VXD probe fixtures passed");
    return 0;
}
