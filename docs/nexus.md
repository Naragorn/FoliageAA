# FoliageAA - the Nexus page

The text for the Nexus Mods page (kept short and plain; the README says the
rest). Version 1.0 = plugin version 2 (the leaves since October, the grass
since 2026-11-03).

## Summary (the one line under the title)

Leaves and grass that stop shimmering: real supersampling of the game's own
alpha-tested edges, for the flat game and for VR. An xOBSE plugin, no game
files touched.

## Description

Oblivion draws its tree leaves and its grass with an alpha test: every pixel
is either leaf or not, blade or not. In the distance that flips back and forth
as you move, and the grass flickers in the wind even when you stand still.

FoliageAA draws each leaf and grass draw a few times, once per antialiasing
sample, each time writing only that sample and shifted so the game's own
alpha cut lands right where the sample sits. That is transparency
supersampling, the thing NVIDIA's driver used to offer for Direct3D 9. The
edges come out smooth and stay put when you move. In a VR headset (OBVR) the
difference is big; on a monitor it is the end of the leaf shimmer in the
distance.

**Needs**

- xOBSE (the plugin loader).
- Antialiasing on in the Oblivion launcher, any level. 4x is enough for the
  grass, 8x looks best for the leaves. Without antialiasing there are no
  samples to work with and the plugin does nothing (it says so in its log).
- Oblivion 1.2.416 (the Steam and GOTY build). Works with the plain Direct3D 9
  of Windows and with DXVK.

**Install**

Drop `FoliageAA.dll` and `FoliageAA.ini` into `Data\OBSE\Plugins\`, or let your
mod manager do it. To remove it, delete the two files. Nothing else changes.

**Settings** (`FoliageAA.ini`)

- `[Leaves] Passes=8`: how many passes a leaf draw gets. 8 on 8x antialiasing
  gives every sample its own alpha test; 4 halves the cost.
- `[Grass] Passes=4`: the same for the grass. Grass covers a lot of the
  picture, so it starts at 4; `Method=off` draws the grass as the game does.
- The plugin writes `FoliageAA.log` next to `Oblivion.exe` and says there what
  it found and what it does.

**Cost**

The leaves and the grass are drawn `Passes` times over. In a dense forest or
a wide meadow that can show in the frame rate; lower the passes then. Nothing
else is touched.

**Compatibility**

Only the game's own leaf and grass shaders are recognised. A mod that replaces
them (Oblivion Reloaded and the like) keeps drawing its own, and FoliageAA
leaves those draws alone.

**Source**

GPL-3.0, on GitHub: https://github.com/Naragorn/FoliageAA

Like it? Buy me a coffee on Ko-fi: https://ko-fi.com/somadb (somadb is
another project of mine; the link goes to me).

## Files

`FoliageAA-1.0.zip` with `OBSE\Plugins\FoliageAA.dll`, `FoliageAA.ini`, the
license and a README as text - Data-relative, so mod managers place it.

## Changelog

- 1.0 (2026-11-03): the grass supersampled like the leaves; the toggle key
  of the beta removed.
