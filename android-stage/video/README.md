# J72s hardware video decoder

The candidate DTB enables `apple,s8000-vxd` at `video-codec@207f00000`.
The vendor build stages the `apple-vxd` module and its `apple/vxd-deva.bin`
firmware. The pinned driver expects stateless H.264 and HEVC output in
Annex B access units, media requests, and single-memory-plane 8-bit NV12
capture. At 1920 × 1088 coded pixels, the NV12 row stride is 2048 bytes,
with chroma starting at byte 2,228,224 in the buffer.
[Linux stateless decoder interface](https://docs.kernel.org/userspace-api/media/v4l/dev-stateless-decoder.html)

## Qualify the production ABI

In AOSP, build the standalone tool with `m j72s-vxd-probe`. On the
booted device, pass its decoder video node and the matching media node
from an account and SELinux domain with access to those nodes.

```sh
j72s-vxd-probe /dev/videoX /dev/mediaY
```

The probe checks the Apple platform address, matched media device,
multi-planar M2M capabilities, both slice formats, the NV12 layout,
per-codec control payload sizes, Annex B frame mode, DMA-BUF queue
capabilities, and the creation of an empty request. It opens fresh
read-only handles, queries buffer capabilities with zero buffers, and
closes the request without submitting it. On the first video open the
kernel can boot the staged firmware and power the decoder. The test
uses no video frames and measures no decoding performance.
[Linux V4L2 buffer capability query](https://docs.kernel.org/userspace-api/media/v4l/vidioc-reqbufs.html)

Run the fixture test with `m j72s-vxd-probe-test` and the resulting host
binary, or build it directly from this directory on Linux:

```sh
cc -std=c17 -Wall -Wextra -Werror -DJ72S_VXD_PROBE_TEST \
    -Wl,--wrap=ioctl,--wrap=close vxd_probe.c vxd_probe_test.c -o /tmp/j72s-vxd-probe-test
/tmp/j72s-vxd-probe-test
```

## Android playback

The stock Android 16 `v4l2_codec2` component expects the stateful decoder
API. Android playback needs a stateless Codec2 backend that translates
a parsed access unit and reference picture state into request controls,
and coordinates the lifetime of NV12 capture buffers with Android's
graphics allocator. A good hardware check before registering a
component is decoding known H.264 and HEVC clips through a Request API
client such as GStreamer's `v4l2codecs` plugin, then checking decoded
frames and DMA-BUF import on the iPad. Keep hardware codec advertisement
disabled until that path and device access policy are verified.
[Android v4l2_codec2 prerequisites](https://android.googlesource.com/platform/external/v4l2_codec2/+/android-16.0.0_r1/README.md)
[GStreamer stateless codecs](https://gstreamer.freedesktop.org/documentation/v4l2codecs/index.html)
