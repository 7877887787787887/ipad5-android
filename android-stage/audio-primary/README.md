# J72s Android 16 primary audio

The patches target AOSP `platform/hardware/interfaces` at
`5688f7eb1e117ed26e642a695de300b7683acb87` (`android-16.0.0_r1` and
`android-16.0.0_r2`). The policy attaches the speaker and internal
microphone and defines an external wired headphone port. Their format
is signed 16-bit, stereo, 48 kHz PCM.

The `j72s-speaker` and `j72s-mic` addresses select the board PCM.
The resolver locates the `iPad5 Speakers` card and its corresponding
playback or capture endpoint. The ALSA worker handles transfers and
positions. Microphone start and stop control the codec's `Capture
Switch`, and Android's mute silences samples returned from capture.
Output changes use the codec's mutually exclusive `Output Select`
and the selected playback switches. The mixer leaves gains at their
driver settings, including the J72s device tree's −3 dB speaker ceiling.

## Source staging

Run from the port repository in a Linux shell with `AOSP` set to the Android source root.

```sh
bash scripts/stage-j72s-audio.sh "$AOSP/hardware/interfaces"
```

The J72s vendor product installs `audio_policy_configuration.xml` and all
of its includes under `/vendor/etc`. It packages the patched service in
`com.android.hardware.audio`. Android converts the `primary` module into
`android.hardware.audio.core.IModule/default` and supplies `IConfig/default`.
The Bluetooth and remote-submix modules match the other audio APEX
declarations. Their routes come from Android 16 r1's
`frameworks/av/services/audiopolicy/config/` modules. The AIDL XML schema
requires omitting the old profile `name` attributes.

Wired headphones become available when Android receives a jack
connection. The pinned kernel has no CS42L81 jack detector, so
automatic switching requires a driver for the plug-detect signal.
The device tree routes two internal digital microphones and provides
SmartIO capture DMA. Physical PCM negotiation, recorded audio, mute,
full duplex, wired headphone playback and Bluetooth audio require
on-device validation.
