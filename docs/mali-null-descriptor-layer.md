# Mali nullDescriptor compatibility

`VK_LAYER_ARLINUX_null_descriptor` is an explicit Vulkan compatibility layer
for Mali drivers that do not expose `VK_EXT_robustness2`. Mesa Zink currently
requires `VK_EXT_robustness2.nullDescriptor` before it creates its screen. On
the MediaTek Dimensity 9400 with Mali-G925 and driver `v1.r49p1-03bet0`, this
missing feature prevents Zink from initializing any GLX or EGL screen.

The layer is deliberately separate from `VK_LAYER_HYBRIS_compat`. It changes
application-visible device capabilities and descriptor contents, so it must
only be enabled for devices that need the compatibility path.

## Behavior

The layer:

1. Adds `VK_EXT_robustness2` to physical-device extension enumeration when the
   driver does not expose it.
2. Reports `nullDescriptor = VK_TRUE` from
   `vkGetPhysicalDeviceFeatures2[KHR]`.
3. Removes `VK_EXT_robustness2` from the extension list passed to Mali.
4. Creates per-device dummy buffer, buffer view, image, image view and sampler
   resources.
5. Rewrites null buffer, image, sampler and texel-buffer descriptors before
   direct updates, update templates and push-descriptor commands reach Mali.

Descriptors that are already valid pass through unchanged.

## Activation

Keep the layer DSO and manifest colocated, then opt in explicitly:

```sh
VK_LAYER_PATH=/path/to/layer \
VK_INSTANCE_LAYERS=VK_LAYER_ARLINUX_null_descriptor:VK_LAYER_HYBRIS_compat \
VK_DRIVER_FILES=/path/to/hybris_icd.json \
application
```

Do not enable the layer on Turnip or another driver that exposes
`VK_EXT_robustness2`.

## Verification

Validated on ARLinux 0.7.0 with:

- MediaTek Dimensity 9400
- Mali-G925-Immortalis MC12
- driver `v1.r49p1-03bet0.4505ac1771886c7c25f13653ad4cdff7`
- Mesa `26.3.0-devel`

`glxinfo -B` reports `direct rendering: Yes` and
`zink Vulkan 1.3(Mali-G925-Immortalis MC12)`.

SteamWebHelper reports `gpu_compositing = enabled`, `opengl = enabled_on`, no
GPU process crash count, and the ANGLE renderer
`zink Vulkan 1.3(Mali-G925-Immortalis MC12)`.

A presentation-throttled `glxgears` run reached 146-159 FPS at 800x600.
SurfaceFlinger captured mostly 16.5-17.6 ms present intervals while scrolling
Steam Big Picture, with about 3.5% of intervals crossing the next vblank.

## Limits

The layer does not specialize dummy resources for every shader-visible
storage-image format. It does not emulate acceleration structures, mutable
descriptors or inline uniform blocks. Applications using those less common
descriptor forms require additional coverage before the layer can be treated
as a general replacement for native `VK_EXT_robustness2`.
