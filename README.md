Decodes via lavc with either software or hardware (Vulkan) decoding.
If software, each frame is uploaded into a Vulkan hardware frame.
Optionally encodes every frame with ffv1_vulkan.

Usage: dec_tx_test <input> <vulkan device> <hw decode 0|1> [encode 0|1] [frames] [output]

  vulkan device  device index or name, "" for the default device
  hw decode      1 = use the Vulkan hwaccel when the codec has one
  encode         1 = also encode with ffv1_vulkan (encoder is drained at the end)
  frames         number of frames to process, default 1000
  output         when encoding, the first encoded frame is written here as MKV
                 (default first_frame.mkv, "" to skip)

FFV1 decode mode: if <input> is a .mkv file, its first FFV1 packet is decoded
<frames> times (Vulkan FFV1 hwaccel with hw decode = 1, multi-threaded software
FFV1 with hw decode = 0). Nothing is uploaded or encoded; the encode flag is
ignored. The first decoded frame is saved as DPX to <output>
(default first_frame.dpx, "" to skip).

  dec_tx_test 6k5k16bit.dpx.mkv 0 1 1 1000

The first packet of the input is decoded repeatedly, so this measures
decode (+upload, +encode) throughput rather than reading a real stream.
