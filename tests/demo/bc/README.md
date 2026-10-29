# Small BC/D3D demos

These demos keep the BC investigation below the cost of a Steam launch.
They use the same Bionic Wine, DXVK, `VK_LAYER_HYBRIS_compat` and Mali driver
for the Windows path, but avoid the full game, Steam and its window stack.

## Layers

1. `tests/baseline/run.py` BC image cases test the Vulkan layer directly.
2. `d3d11_bc_demo.exe` tests the same BC image/transcode path through DXVK.
3. `d3d9_bc_demo.exe` checks DXT1/DXT3/DXT5 and a 2048x2048 DXT1 upload
   through the D3D9 API used by Lost Coast.

## Build the Windows demo

Install an x86_64 MinGW-w64 toolchain and run:

```sh
tests/demo/bc/build-d3d11.sh
tests/demo/bc/build-d3d9.sh
```

The outputs are `tests/demo/bc/build/d3d11_bc_demo.exe` and
`tests/demo/bc/build/d3d9_bc_demo.exe`.

## Run

Launch the executable through the ARLinux Windows host without opening Steam:

```sh
python3 /arlinux-shared/tests/run-via-windows.py \
  /arlinux-shared/tests/d3d11_bc_demo.exe
```

The wrapper calls only `windows.run()` and therefore skips desktop
accessibility and WebView setup. It writes stable `BC_DEMO_*` result lines and
exits nonzero on failure. Set these Debug-only variables in the same way as
other ARLinux Windows diagnostics:

The Debug APK also accepts an app-owned automation intent:

```sh
adb shell am start \
  -n io.taowen.arlinux.dev/io.taowen.arlinux.ArlinuxActivity \
  --ez io.taowen.arlinux.extra.AUTO_START true \
  --es io.taowen.arlinux.extra.DEBUG_WINDOWS_EXE \
    /arlinux-shared/tests/d3d11_bc_demo.exe \
  --es HYBRIS_VULKAN_TRACE 0 \
  --es HYBRIS_BC_TRANSCODE 1 \
  --es HYBRIS_BC_FAST 1
```

The app launches the EXE through its own `WindowsRuntimeHost`; no root write
or manual app-private file creation is involved.

```text
HYBRIS_VULKAN_TRACE=1
HYBRIS_BC_TRANSCODE=1  # baseline; 0 disables ETC2/ASTC transcode
HYBRIS_BC_FAST=1       # baseline; 0 forces the normal-quality encoder
```

Run one variable at a time. The demo always renders offscreen and reads back
representative pixels, so no display server or screenshot is required.

## Lost Coast fixture

`vpk_extract.py` lists or extracts one file from the installed Source VPK.
`vtf_fixture.py` selects a deterministic subset of blocks from a DXT1/DXT3/DXT5
VTF and emits a small C header. The generated header records the source and
full VTF SHA-256, but embeds only three blocks. Do not copy the complete VPK or
texture into the repository.

Example:

```sh
python3 tests/demo/bc/vpk_extract.py lostcoast_pak_dir.vpk \
  --list --match materials/
python3 tests/demo/bc/vpk_extract.py lostcoast_pak_dir.vpk \
  --extract <exact-vpk-path> --output build/asset.vtf
python3 tests/demo/bc/vtf_fixture.py build/asset.vtf \
  tests/demo/bc/lostcoast_dxt_fixture.h
```
