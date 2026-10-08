# FoliageAA

Tree leaves shimmering in the distance in Oblivion (2006)? This little xOBSE
plugin supersamples them: the game's own leaf edges, antialiased for real,
the way "Transparency Supersampling" in the graphics driver used to do it.
Works flat and in VR.

## Install

Needs [xOBSE](https://github.com/llde/xOBSE) and antialiasing on in the
Oblivion launcher (any level; 8x looks best). Drop `FoliageAA.dll` and
`FoliageAA.ini` into `Data\OBSE\Plugins\`. To remove it, delete them.

Works with the plain Direct3D 9 of Windows and with DXVK.

## What it does

Oblivion draws leaves with an alpha test: every pixel is either leaf or not.
Far away, that flips back and forth as you move, which is the shimmer.

The plugin draws each tree's leaves several times, once per antialiasing
sample, each time writing only that sample and shifted so the game's own
alpha test is evaluated right where the sample sits. That is transparency
supersampling, the thing NVIDIA's driver used to offer for Direct3D 9: with
8x antialiasing every pixel gets eight real leaf-or-not decisions instead of
one, the edges come out smooth and stay put when you move your head. No game
files are touched; the plugin only hooks the Direct3D device Oblivion made.

There is also an alpha-to-coverage mode (`Method=coverage`), cheaper, which
uses the vendor back doors Direct3D 9 has for it (NVIDIA/Intel `ATOC`, AMD
`A2M1`). Under DXVK on NVIDIA that coverage is dithered, and in a VR headset
the dither glitters, so supersampling is the default.

## Settings

`FoliageAA.ini` next to the DLL:

- `[Main] Enable=1` turns it on, `0` makes the plugin do nothing.
- `[Leaves] Method=supersample` (default) or `coverage`.
- `[Leaves] Passes=8` is how many passes a leaf draw gets, 1 to 8. 8 on 8x
  antialiasing gives every sample its own alpha test; 4 halves the cost.
- `[Leaves] SharpenLeaves=1`, `Threshold=engine`, `Steepness=4.0` belong to
  the coverage method: the leaf shaders get a copy that sharpens the alpha
  around the engine's own threshold, or distant leaves go see-through.
- `[Coverage] Enable=0` would turn alpha-to-coverage on for everything else
  that is alpha-tested (grass, hair, fences). Off by default because of the
  dither; try it on a monitor if you like.
- `[Main] Mode=auto` picks the vendor route for coverage by what the driver
  reports; `nvidia`, `amd` force one.
- `[Diagnostics] DumpShaders=1` writes every pixel shader the engine sets
  into `FoliageAA-shaders\` once and logs the render states of that moment.
  For finding out which shader draws what; off for play.

`FoliageAA.log` in the game folder says what the plugin found and did.

## Heads up

- Without antialiasing there are no samples to work with, so nothing changes.
  The log says so.
- Supersampling costs the leaf drawing `Passes` times over. In a dense forest
  that can show; `Passes=4` is the first thing to try then.
- Only the game's own leaf shaders are recognised. A mod that replaces them
  (Oblivion Reloaded and the like) leaves the trees as that mod draws them.
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
