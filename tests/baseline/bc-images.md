# Experimental BC1–BC7 image fallback

The standard-loader ICD now intercepts application BC image creation, memory
queries/binding, views, transfers and synchronization. This is a partial G07
implementation; BC6H was subsequently added as described in [BC6H evidence](bc6h.md). This is not full Vulkan format conformance.
BC7 image sampling passes on Mali; Redmi retains a native sRGB filtering
reference failure. See [BC7 evidence and remaining failures](bc7.md).
The separate [decoder probe](bc-decode.md) still tests only the internal kernel.
BC1 RGB now uses native RGB8 storage where supported. The expanded border
probe passes on Mali and exposes an unresolved Redmi alpha failure; see the
[RGB8 correction and remaining failure](bc-rgb8.md).

## Policy and supported domain

The default is `missing`. `HYBRIS_BC_TEXTURES=missing` selects BC1 RGB/RGBA,
BC2/BC3/BC7 UNORM/sRGB, BC4/BC5 UNORM/SNORM and BC6H UFLOAT/SFLOAT formats
lacking native optimal sampled-image support. `force` selects all sixteen
formats for development comparisons. Environment options
are ignored in secure execution and read once per process.

The initial fallback requires an application requesting Vulkan 1.1 or newer,
a physical device reporting Vulkan 1.1–1.3, and compute support with single-texel
transfer granularity on every transfer-capable queue family. Unsupported
physical devices keep their native format behavior. Vulkan 1.4's additional
state commands are not covered yet.

Selected formats support optimal 2D and 3D images, single sampling and mip levels,
with sampled/transfer usage. 2D images also support array layers and cubes.
BC1/BC2/BC3/BC7 allow mutable UNORM/sRGB views within their format pair.
Linear, sparse, other mutable reinterpretations, storage, attachment, external-memory and host-copy images
are not advertised. Sparse format and maintenance4 sparse-memory queries return
no requirements for selected emulated formats. The image-format queries use the corresponding native
RGB8, RGBA8, R16, RG16 or RGB/RGBA16F format's limits and restrict sample counts
and usages consistently. Format properties expose sampled, linear-filter,
transfer and source-blit support when available in the backing format.
`textureCompressionBC` is advertised only when all sixteen formats satisfy
the required sampled, linear-filtered and source-blit domain for 2D and 3D
images. Missing native RGB backing also prevents the flag from being raised,
so absent-alpha and border behavior are preserved. Individual optional image
creation flags and view reinterpretations must still be queried separately, as described by
[VkPhysicalDeviceFeatures](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html).

When emulation is active, pipeline statistics and protected memory are disabled;
protected queue flags are removed too. Injected compute must not contaminate
application pipeline-statistics queries. Unsupported descriptor-state and
related extensions are excluded from enumeration and rejected at device
creation: descriptor buffer, push descriptor, shader object, maintenance8,
host image copy, QCOM rotated copy, EXT/NV generated commands (including NV
compute generated commands), NV indirect memory copy, and NV dedicated allocation. Feature queries for
the excluded feature structures agree with these decisions. These restrictions
apply only to the opt-in device with selected emulated formats.

## Implementation and resource ownership

Each selected image has a compressed storage/transfer buffer and a native
decoded image: RGB8 for BC1 RGB where optimal native sampled/transfer support
exists, otherwise RGBA8; RGBA8 for BC1 RGBA/BC2/BC3, R16 for BC4, RG16 for BC5.
The RGB8 choice is per physical device and sRGB variant, and is retained on
each image for view creation and upload packing. The RGBA8 alternative still
has the documented BC1 RGB border-alpha defect on Redmi. The compressed backing is bound to the application's allocation;
the decoded image has an internal allocation. Dedicated requirements are
queried independently. Image memory requirements, dedicated allocation chains,
BindImageMemory2 device-group indices, and maintenance4's allocation-free memory
queries are translated to the compressed buffer. Application pNext memory is
not modified. Multi-physical-device groups remain unsupported in this mode.

Uploads copy compressed bytes on the GPU, then decode bounded tiles into a
command-buffer scratch buffer and copy those pixels into the decoded image. sRGB
uses an sRGB RGBA image, so sampling performs the native transfer-function
conversion. BC4/BC5 use integer rational interpolation followed by one rounding
step to 16-bit UNORM/SNORM; the conversion error is at most half a normalized
16-bit step. Signed endpoints are saturated from -128 to -127 before comparison.
The -127/-128 endpoint pair has implementation-dependent ordering in the
[RGTC specification](https://github.com/KhronosGroup/DataFormat/blob/main/rgtc.txt);
this implementation selects the saturated, equal-endpoint branch. BC4 packs
two texels per storage word, zero-padding an odd last texel, then copies into
an R16 image. Preserving the native channel count is necessary for border
replacement and missing-component defaults. Image-to-buffer copies return compressed bytes. Image-to-image
copies transfer those bytes and decode the destination as needed, including
size-compatible native color formats and 2D-array/3D slice copies. The fallback
records commands without submitting queues, mapping upload data or waiting for
GPU completion. It adds memory use and dispatch overhead; performance is not
accepted yet.

The command journal preserves the application's compute pipeline, descriptor
bind history, dynamic offsets and push constants. Compatible pipeline-layout
clones survive application layout destruction until their command recordings
retire. Decoder descriptor sets are immutable for a recording; scratch and
pools are reclaimed on command-buffer/pool reset, free or destruction. Image
barriers and event dependencies also cover the compressed backing, by mip and
array-layer range. Classic and synchronization2/KHR entry points are wrapped.

RGB8 output packs four pixels into three storage words without overlapping
byte writes; a partial final word is zero-padded. It retains the existing
BC1 color bytes and native sRGB conversion, and adds no host decoding.

The modules separate image storage, transfer/decoding, command state, object
registries, resource entry points, image copies, barriers and capability policy.
The existing scaled-vertex implementation retains its own shader/pipeline hooks.

## Probe and reproduction

### Core BC feature enablement (2026-10-11)

The capability predicate checks the complete required format domain, not just
the decoder mask. `bc-feature-policy.c` verifies that missing linear filtering,
source blits, RGB backing, 3D support or single-sample support keeps the feature
disabled. At device creation, the compatibility layer consumes the emulated
BC feature using the same immutable input-chain copy used for other emulated
features; the native driver receives its own supported feature set.

On x300, the normal-build layer
`f19eec2c306a1da4bdc9a30a8b9423625606c0050f01ad7a33f8b44a01a0db07`
reports `compression_bc=1`. The volume probe enables BC through
`pEnabledFeatures`; the maintenance6 probe enables it through `Features2`.
Both device creation paths passed their 384 readbacks with synchronization
validation enabled and zero errors. These are isolated library deployments,
not APK/game acceptance. Earlier results below record revisions that kept the
core flag disabled. Fill mode and CullDistance are separate outstanding
requirements for the stock DXVK game path.

Build the host capability regression from the repository root:

```sh
cc -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
  tests/baseline/bc-feature-policy.c hybris/vulkan/compat/bc_image.c \
  -Wl,--gc-sections -lpthread -o /tmp/bc-feature-policy
/tmp/bc-feature-policy
```

### Three-dimensional images (2026-10-11)

`bc-images-volume-validation` uploads and samples three depth slices at each
tested mip, using `sampler3D` against an independently uploaded native 3D golden
reference. It covers all sixteen formats, two shapes, four mip levels, three
submissions, a GPU-generated patch at nonzero z, BC-to-BC copies and copies
through native 2D-array/3D images. On x300 all 384 readbacks matched and
synchronization validation reported zero errors. The same layer then passed
the 384-readback 2D maintenance6 regression with zero validation errors.

Tested isolated layer SHA-256:
`ce164d97a9e0745e2fa3158307ae2a62e1f7c008713e2813b1a43809bda5c11f`.
No diagnostic feature-advertisement switch was enabled. This is not a production
APK test. 3D blits, large volumes and other devices still need coverage. The layout-only
`bc-volume-layout.c` also verifies shrinking mip depth and unchanged 2D-array
offsets. Full `textureCompressionBC` remains unadvertised.

The normal `arlinux-rootfs/tools/build/linux-gpu.sh` build then regenerated the
library from the tracked source and working-tree diff, replacing the old build
directory. Its compatibility layer SHA-256 is
`4c28ae88f224dc046899089777e8f5b7d0f019db28a060848874931413cde4a0`.
With this library, both the volume and maintenance6 synchronization-validation
probes passed again: 384 readbacks each, zero mismatches and zero validation
errors. The library contains neither the temporary Hell Clock feature switch
nor the removed image-bounds transformation. These runs validate the normal
library build, but still use an isolated deployment rather than a rebuilt APK.

The volume probe also samples between depth slices with 3/4 and 1/4 weights.
The BC/native comparison remains bit exact. The CPU filtered oracle checks the
weighted eight-texel neighborhood, including sRGB conversion, signed channels
and half-float values; nearest samples and raw compressed blocks retain their
separate golden checks. This extended probe passed all 384 readbacks with zero
validation errors on the normal-build library above. Manually mixing two
filtered array samples was rejected as the bit-exact oracle because its
rounding can differ from a single native 3D texture sample.

### Batch binding results

`bc-bind-status.c` exercises the actual resource wrapper with a mock driver.
It covers native/emulated images in both orders, core/KHR entry points,
success, an injected first-binding failure, and optional per-binding results.
All 16 cases pass. A failure must not prevent the later binding from running;
the emulated image reports its compressed buffer's actual binding result.
This supplies the per-binding result behavior required by maintenance6.

Build from the repository root with recent Vulkan headers:

```sh
cc -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
  tests/baseline/bc-bind-status.c -Wl,--gc-sections -o /tmp/bc-bind-status
/tmp/bc-bind-status
```

See the [batch binding requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkBindImageMemory2.html).

### maintenance6 compute-state restoration (2026-10-11)

`bc-images-maintenance6` binds application descriptors (including a dynamic
offset) and push constants using the KHR maintenance6 entry points before BC
uploads. Sampling after decoding does not rebind application state. It also
destroys the application pipeline layout before submission, exercising the
retained layout clone. x300 passed 384 readbacks across sixteen BC formats,
two shapes, four mip levels and three submissions, both without validation
and with synchronization validation enabled (zero validation errors).

The BC policy now preserves native maintenance6 support. Push descriptors,
descriptor buffers and `VK_NV_per_stage_descriptor_set` remain filtered: the
latter enables per-stage binding and null/inline pipeline layouts that the
current journal cannot restore. This test does not establish complete Vulkan
or BC format conformance; `textureCompressionBC` remains false.

### Source blits (2026-10-11)

Emulated BC formats now retain `BLIT_SRC` when their decoded native format
supports it. The public image handle already names that decoded GPU image,
so the driver's ordinary blit handles format conversion and filtering; no
additional command wrapper or CPU readback is needed. `BLIT_DST` remains
unavailable because a destination write would also need to update compressed
backing storage. The complete `textureCompressionBC` feature remains false.

Run `probe-glibc bc-images-blit` to build the sampled
reference image using a BC-to-native blit instead of a CPU-golden upload.
CPU-golden verification remains enabled. On x300 (V2509A / Mali-G1-Ultra),
the new layer passed 384 readbacks over sixteen formats, two sizes, four mip
levels and three submissions, including array layers and nearest/linear
filters. The installed old layer reports unsupported at the first format.
This run did not enable Vulkan validation layers. These are equal-extent
blits. `bc-images-blit-scale` additionally magnifies twofold with nearest
filtering and reverses both axes, then reduces/reverses back with alternating
nearest/linear filters. Its 384 readbacks also passed on x300. Arbitrary
ratios, linear-filtered BC enlargement and broader image types remain untested.
The original upload/sampling test and the blit test through `dlsym` also
passed 384 readbacks each (1,152 total across the three runs).
The standard runner includes `icd-bc-images-blit`,
`icd-bc-images-blit-scale`, and `icd-bc-images-blit-dlsym` cases.

`bc-images` uses the existing baseline executable and runner. The application
creates two BC images plus a size-compatible native UINT image, uploads data,
copies BC-to-BC and through the native image, samples into an SSBO, and reads
compressed bytes back. A fourth, native RGBA8/R16/RG16 image receives the independent
golden palette pixels and supplies a bit-exact sampling reference. Views cover
identity components and an R/B swizzle. The fixture has fourteen formats, 9×7 and 32×32 base
images, four mip levels and three layers. Native-to-BC copies cover only whole
blocks that fit the destination; partial compressed edge blocks are exercised
by BC-to-BC copies. The aligned shape also exercises native 3D slices. Its base-level recording
requires 73 decoder sets across uploads/copies and crosses the 64-set pool
boundary; it is still far below device image-size or memory limits.

Each case submits the same recording three times with changed host data first
written after recording. Eligible regions additionally receive a GPU-produced
fixed block (valid mode 6 for BC7, zero for BC1–BC5) at a nonzero x offset and array layer. Sampling runs without an
application compute rebind after uploads. The output buffer uses a nonzero
dynamic descriptor offset, allocations use nonzero bindings where permitted,
and selected images force dedicated allocations through a pNext prefix.
Where available, image-format-list chains and maintenance4's pre-creation
memory query are checked against actual image requirements.
The application pipeline layout is destroyed after recording, before submission.
Explicit command-buffer reset, pool reset and free/reallocation alternate with
implicit reset. Classic events are used on both devices; copy2 and
synchronization2 are conditional on queried/enabled extensions.

All 288 readbacks check sampling, compressed bytes and surrounding sentinels
across the complete 256 KiB output buffer. BC sampling must match the native
reference bit for bit. BC4/BC5 sample all four components at 16-bit precision,
including negative values, default zero/one components and swizzles. The
reference also matches the fixed nearest-texel palette exactly; only its sRGB RGB conversion
check allows one 8-bit step against the mathematical transfer function.
Compressed bytes, alpha, nearest UNORM/SNORM values and untouched sentinels
are exact. When linear filtering is advertised, BC4/BC5 also sample halfway
between texel centers, using clamp-to-edge, opaque-white and transparent-black
borders. These BC/native filtered samples must match bit for bit. The native
result is separately checked against the four-texel arithmetic average; only
its interpolated R/G values permit one 16-bit step for filtering/packing
rounding. Default components remain exact. BC1 RGB now additionally checks
clamp-to-edge plus white, transparent-black and opaque-black borders. Its
filtered RGB values use the independent native RGBA8 image as the exact oracle
and alpha must remain one. The reference view supplies alpha one only where
that swizzle has defined behavior; opaque-black cases use identity views.
The expanded case is intentionally FAIL on Redmi until its border behavior
is corrected. CPU pixel/sentinel checks now live in `bc_image_verify.h`,
separate from Vulkan recording. Validation modes
cover GIPA/GDPA and the standard loader's exported core ELF/link entry points.
KHR aliases use GIPA/GDPA because the standard loader does not export those
aliases as ELF symbols. These modes enable synchronization validation at the
standard loader's application boundary;
they do not inspect ICD-internal commands bypassing that layer.

After building the library and baseline executable:

```sh
python3 tests/baseline/run.py --serial 29854870 \
  --icd-hal /vendor/lib64/hw/vulkan.adreno.so \
  --vulkan-loader tests/baseline/build/icd-spike/libvulkan-loader.so.1 \
  --validation-layer tests/baseline/build/validation-build/install/lib/libVkLayer_khronos_validation.so \
  --validation-manifest tests/baseline/build/validation-build/install/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json \
  --validation-build-manifest tests/baseline/build/validation-build/install/manifest.json \
  --bc-textures force \
  --case icd-bc-images-validation --case icd-bc-images-gdpa-validation \
  --case icd-bc-images-dlsym-validation --case icd-linked-bc-images-linked-validation
```

For Mali use serial `10AFA31610002QH`, HAL `/vendor/lib64/hw/vulkan.mali.so`
and the explicit `--icd-mali-loader-quirk` flag. Omit `--bc-textures` for the
native-behavior control; use `missing` for the normal opt-in selection policy.

## Earlier BC1–BC3 evidence

The initial application-path run (`20260908T085934-5ae46145`, Redmi;
`20260908T085934-e192caea`, Mali) had correct sampled/compressed readbacks but
failed validation because the probe attempted native-to-BC copies extending
past partial-block mip edges. Those failures are retained. Correcting the
copy ranges and adding aligned images produced two passing validation routes
on each device: `20260908T090636-d9e4612d` and
`20260908T090636-8b7259a8`, each with 192 readbacks per route and zero errors.
Those runs predate the GPU subregion and explicit reset/free additions.

The initial ELF-route expansion (`20260908T101037-190776b8`, Redmi;
`20260908T101037-1d389ca4`, Mali) incorrectly required KHR aliases as exported
symbols. The linked process failed to load, and Mali's dlsym route stopped at
a missing copy2 alias. The probe now uses the required proc lookup for those
extension commands. These failed runs remain recorded.

BC1–BC3 batch build and device results, 2026-09-08:

| Mode | Redmi 29854870 | Mali 10AFA31610002QH |
| --- | --- | --- |
| `force`, four BC validation routes plus regressions | `20260908T103207-85bb0c79`: 8 PASS, 1 UNSUPPORTED | `20260908T103207-e7a7de4c`: 9 PASS |
| `missing`, GIPA BC validation plus version check | `20260908T103540-d78f7b5c`: 2 PASS | `20260908T103540-c3cae2d6`: 2 PASS |
| Default off, native/frontend/ICD controls plus regressions | `20260908T103831-cae1a003`: 3 PASS, 3 expected UNSUPPORTED | `20260908T103831-a00b3906`: 3 PASS, 3 expected UNSUPPORTED |

The ten enabled BC image executions have 1,920 complete readbacks, zero
mismatches and zero validation errors, including 720 GPU subregion updates.
All use the native RGBA8 reference and exercise the reset/free paths. Both
devices cover image-format-list creation. Mali additionally covers 32 copy2
and 32 synchronization2 cases per execution, and maintenance4 queries matching
actual requirements before every image creation; Redmi does not advertise
those three extensions and does not claim their execution.

The force runs also enable the scaled-vertex fallback. Its validation case,
instance initialization and device lifecycle regressions pass on both devices.
The non-coherent memory-range case passes on Mali; Redmi has no matching
host-visible non-coherent memory type, so its UNSUPPORTED result remains a
coverage gap. Default-off native, frontend and ICD BC image queries all return
unsupported as expected; capability and internal decoder validation cases pass.
No capability is inferred from an expected-unsupported control.

The final clean library build took 55.062 seconds, staged 17 runtime ELFs and
checked 58 installed ELF paths. The three probe binaries were rebuilt using
the existing glibc builder and Android NDK 27.3.13750724. The 67 Vulkan source
inputs in the library manifest and all copied probe source assets match the
worktree; all six final runs retain the same library and probe manifests.
Final SHA256 values:

- ICD: `57600764dece9df9e6709533eb445669efa28490e1bc7377b2695f6197144ac4`
- probe-glibc: `8059aa9d04dee1c68aab4b18f14f1d58e0fd77fdd23987baf2d928482bb772f7`
- probe-glibc-linked: `517a2dcee14863321609edb9c390a7cef98fea0fc5ed2d7a69f5cd6db50b37ed`
- probe-bionic: `61909f89a197124017f8091292bd8dedd9cf27afe27ab181697d18a4b1aef8f8`

Raw logs, mappings, source snapshots and manifests remain under the listed
`build/results/` run directories. The evidence is headless, not a window or
application-rendering acceptance result.

## BC4/BC5 extension evidence, 2026-09-08

The initial extension runs, Redmi `20260908T105656-ea03b016` and Mali
`20260908T105656-3e95fb9d`, are retained as FAIL: all 288 image and 192 kernel
readbacks matched with zero validation errors, but the executable's final
checks still required the old 192/128 counts. The checks now derive their
expected counts from the format array. That initial implementation also used
RG16 for BC4; code/specification review identified the channel-count issue
before adding the border fixture, and BC4 now uses R16. These initial results
are not evidence for the final packing or border behavior.

The final library clean build took 53.426 seconds, staged 17 runtime ELFs and
checked 58 installed ELF paths. Both shader assets were regenerated and passed
SPIR-V validation for Vulkan 1.0. The final glibc/linked/bionic probes use the
existing glibc builder and NDK 27.3.13750724. All 72 Vulkan C/C++/header/include/
build-rule inputs and all 84 probe source assets match the current worktree;
the following six runs retain identical final library and probe manifests.

| Mode | Redmi 29854870 | Mali 10AFA31610002QH |
| --- | --- | --- |
| `force`, four BC validation routes plus regressions | `20260908T110839-aba01798`: 8 PASS, 1 UNSUPPORTED | `20260908T110839-e34478ab`: 9 PASS |
| `missing`, GIPA image validation plus version | `20260908T110900-862debb3`: 2 PASS | `20260908T110901-bf0feccc`: 2 PASS |
| Default off, native/frontend/ICD controls and kernel regressions | `20260908T110906-e040de4e`: 5 PASS, 3 UNSUPPORTED | `20260908T110907-97f8cbea`: 5 PASS, 3 UNSUPPORTED |

The ten enabled image executions complete 2,880 full-buffer readbacks with
zero mismatches or validation errors. This includes 960 BC4/BC5 readbacks,
all also checking linear filtering: 480 use clamp-to-edge, 240 opaque-white
border, and 240 transparent-black border. There are 1,440 GPU-written
subregion updates across all twelve formats. The 32×32 base mip crosses the
internal descriptor-pool boundary in all ten image executions. Mali covers
48 copy2 and 48 synchronization2 cases per image execution plus maintenance4
requirements; Redmi does not expose those extensions. Both cover format lists.

The force matrix also enables scaled vertex conversion; its validation case,
device lifecycle and concurrent initialization regressions pass on both.
Non-coherent memory-range validation passes on Mali and remains UNSUPPORTED
on Redmi because no matching host-visible memory type exists. In default-off
mode, each native/frontend/ICD image probe queries all twelve formats before
returning UNSUPPORTED; all twelve have zero optimal features and image-query
FORMAT_NOT_SUPPORTED on these devices. BC6/BC7 format queries remain identical
and unsupported in every enabled and disabled route. The BC feature flag is
not raised. The three internal kernel routes per device separately complete
1,152 readbacks (384 BC4/BC5), with the ICD route under VVL/SyncVal; these are
kernel evidence, not additional application-image executions.

Final SHA256 values:

- ICD: `435193d74c843adf8ea9ceaa4e7515b4f4fff6c2097f1137b6dbd3097d4ad675`
- probe-glibc: `6796fe48bf27c92d7dbbb72230820b004d9e0a84040bf22873275da5af3f2df8`
- probe-glibc-linked: `5d5ccaa4f61420632760e6d84aeac197c1fad061b6512016205725b250196596`
- probe-bionic: `7620a28088ad4a4e15c7c832eb799861265491205c018380c6a6028ab9efb6d8`

## Remaining scope

Unresolved BC6H/BC7 reference precision and RGB borders on Redmi, mutable views, general external/sparse/host-copy
resources, excluded state extensions, multi-device groups, all queue-ownership
and aliasing combinations, cube sampling, allocation-failure stress, maximum
resource limits, performance and CTS coverage remain open. The 32×32 case
covers descriptor-pool rollover, not general large-image limits. BC1 RGB's
RGBA backing still fails border/default-alpha semantics on devices without
the required native RGB8 support; the new RGB8 path fixes the tested Mali
cases. Custom border colors and general sampler/view combinations remain
outside the fixed-border probe's coverage. Creation support
or these probes must not be read as completion of G07 as a whole.
