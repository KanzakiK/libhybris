# One source branch, two libc builds

`arlinux` is the maintained integration branch for both Linux and Bionic Wine.
Both consumers pin the same libhybris commit. The branch contains the shared
Vulkan ICD, compatibility layer, BC kernels and graphics protocols; fixes are
made here once, not carried on separate libc-specific source branches.

| Product | Build mode | Owner of the build graph and operating guide |
| --- | --- | --- |
| `linux-hybris` | ARM64 glibc on the standard ARM64 runner | [arlinux-rootfs](https://github.com/taowen/arlinux-rootfs/blob/main/docs/BUILD.md) |
| `wine-vulkan` | ARM64 Bionic cross-compilation with the Android NDK on the standard x64 runner | [Wine](https://github.com/taowen/wine/blob/arlinux/arlinux/BUILD.md) |

The workstation entry for both is the Android repository's
[build.ps1](https://github.com/taowen/arlinux/blob/main/docs/BUILD.md).
The libc bridge and Linux build adapters stay separate from Android-native
loading, while the compatibility-layer source list and kernels are shared.
Each product retains its independent workflow dispatch, compiler cache and
artifact identity. A common source commit does not imply interchangeable
binaries or allow one product to consume another product's cache.

The two workflow wrappers in this branch are generated from their owners'
canonical templates. They only dispatch the selected pinned framework recipe;
they do not implement another build graph. Keep the rootfs and Wine submodule
pins synchronized when integrating a graphics change. Update both pins and
validate each product before promoting its public release.
