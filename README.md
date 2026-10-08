# FoliageAA

Leaves, grass and fences shimmering in the distance in Oblivion (2006)? This
little xOBSE plugin turns on alpha-to-coverage for them, the thing Fallout 3
and New Vegas call "Transparency Multisampling" in their launcher and Oblivion
never got.

## Install

Needs [xOBSE](https://github.com/llde/xOBSE) and antialiasing on in the
Oblivion launcher (any level; 8x looks best). Drop `FoliageAA.dll` and
`FoliageAA.ini` into `Data\OBSE\Plugins\`. To remove it, delete them.

Works with the plain Direct3D 9 of Windows and with DXVK.

## What it does

Oblivion draws leaves with an alpha test: every pixel is either leaf or not.
Far away, that flips back and forth as you move, which is the shimmer.
Alpha-to-coverage lets the GPU spread a leaf's edge over the antialiasing
samples instead, so the edges go soft and stop flickering. The plugin asks
the driver for it through the vendor back doors Direct3D 9 has for exactly
this (NVIDIA/Intel `ATOC`, AMD `A2M1`), once per frame. No game files are
touched.

## Settings

`FoliageAA.ini` next to the DLL:

- `[Main] Enable=1` turns it on, `0` makes the plugin do nothing.
- `[Main] Mode=auto` picks the vendor route by what the driver reports.
  `nvidia`, `amd` force one, `off` is the same as `Enable=0`.
- `[Leaves] SharpenLeaves=1` keeps distant leaves solid: coverage is alpha,
  and the leaf textures' mipmaps average leaf and gap, so without this the
  trees go see-through at a distance. The plugin hands the engine a copy of
  its two leaf shaders that sharpens the alpha around the engine's own alpha
  test threshold first.
- `[Leaves] Threshold=engine` follows that threshold; a number from 0 to 1
  fixes it. `Steepness=4.0` is how hard the edge is (1 to 64).
- `[Diagnostics] DumpShaders=1` writes every pixel shader the engine sets
  into `FoliageAA-shaders\` once and logs the render states of that moment.
  For finding out which shader draws what; off for play.

`FoliageAA.log` in the game folder says what the plugin found and did.
## Heads up

- Without antialiasing there are no coverage samples, so nothing changes.
  The log says so.
- Under DXVK the GPU dithers the soft edges a little (a fine checkerboard,
  weakest at 8x). That is how Vulkan drivers do alpha-to-coverage; DXVK
  cannot change it.
- Oblivion 1.2.416 only. The plugin reads the renderer through an address of
  that build and does nothing on any other.

## Building

32-bit MSVC, CMake 3.20+:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release .
cmake --build build
ctest --test-dir build
```

Two of the tests create a real Direct3D 9 device on the build machine.

## License

GPL-3.0, see LICENSE.
