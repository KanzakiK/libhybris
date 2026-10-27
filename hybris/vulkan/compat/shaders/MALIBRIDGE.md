# BC kernels

[MaliBridge by ASEbrahim](https://github.com/ASEbrahim/MaliBridge).
This is a modified integration in libhybris, not an official MaliBridge build.
The upstream additional attribution/name terms are retained in NOTICE.MaliBridge.

The block decoder and ETC2/EAC/ASTC encoders come from
[MaliBridge](https://github.com/ASEbrahim/MaliBridge/tree/2d674a5f8c64cf4f7183684f58321102878d6b8b/shaders),
commit `2d674a5f8c64cf4f7183684f58321102878d6b8b`, by ASEbrahim and MaliBridge
contributors, under GPL-3.0-or-later. Keep their copyright and SPDX notices.
The BC tables/algorithms derive from Sergii Kudlai's bcdec (MIT/Unlicense);
see LICENSE.bcdec. The encoder files carry their additional attribution.
The GPL text is in the repository root LICENSE.GPL3. The Vulkan libraries
linking these kernels must be distributed under GPL-3.0-or-later, together
with corresponding source. This does not relicense unrelated upstream files;
their existing notices remain in place.

Local adaptations preserve the existing RGB8/RGB16F backing formats and
16-bit BC4/BC5 outputs. Work is scheduled per 4x4 block, with padded block
rows in scratch; the transfer adapter supplies bufferRowLength explicitly.
The existing raw BC backing remains authoritative for copies and readback.
ETC2/EAC/ASTC 4x4 are used only when their native features and the exact image
creation parameters are supported. Images requesting transfer-source usage
also require native transfer-source and source-blit support in the target format.
Mutable views use the corresponding compressed UNORM/sRGB pair. Volume images
require native 3D support for the target format. BC6H retains its floating-point output; ASTC HDR cannot preserve
negative values and is not selected. No robustness feature is emulated
or advertised by this port.

`generate-bc.py` is the existing offline kernel generator. It uses glslc and
spirv-val (GLSLC/SPIRV_VAL may select executables) and writes bc_decode.inc.
The normal product build consumes this committed output.
