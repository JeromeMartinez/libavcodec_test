Decodes via lavc with either software or hardware (Vulkan) decoding.
If software, each frame is uploaded into a Vulkan hardware frame.
Optionally encodes every frame with ffv1_vulkan.

Usage: dec_tx_test <input> <vulkan device> <hw decode 0|1> [encode 0|1] [frames]

  vulkan device  device index or name, "" for the default device
  hw decode      1 = use the Vulkan hwaccel when the codec has one
  encode         1 = also encode with ffv1_vulkan (encoder is drained at the end)
  frames         number of frames to process, default 1000

The first packet of the input is decoded repeatedly, so this measures
decode (+upload, +encode) throughput rather than reading a real stream.
