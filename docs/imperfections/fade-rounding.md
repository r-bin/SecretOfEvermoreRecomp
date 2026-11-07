# Fades Round Differently

**Status:** open, in snesrecomp's renderer. The only known difference in the picture.

**2026-11-07, the committed replays:** the picture differs at 289 of 8,592 keys of 01 and 948 of 23,756 of 02, by 8 of 255
where MesenCE has 0 (01 from key 365). `--fade` does not remove them: with it 309 and 972 keys differ. So either the rewrite
models the rounding wrongly for these frames, or some of them are not this rounding. Not traced yet.

## What happens
Below full brightness a pixel differs from MesenCE's by up to 8 of 255 per channel. MesenCE scales the 5-bit colour and
then widens it to 8 bits (`SnesPpu::ApplyBrightness`); snesrecomp's legacy renderer widens first and then scales
(`ppu_legacy.c`, `((c << 3) | (c >> 2)) * brightness / 15`). At brightness 10, colour 1 is 0 on MesenCE and 5 here.
Dark colours are therefore a little brighter here during fades and in dimmed scenes.

## Measured
`compare --full --picture` on all five replays: the picture differs only below brightness 15. With `--fade`, which rewrites
the recomp's pixels by MesenCE's rule before hashing (`SOE_COMPARE_FADE`, compare runs only, nothing changes in play), see
[../progress-details.md](../progress-details.md), "Picture and sound".

With `--fade` the picture matches at every key of all five replays but one. That key is a different thing: key 2992 of
replay 05, the last of a fade to black, differs in rows 0-24 alone, MesenCE 8 where the recomp has 0, and key 2993 matches.
MesenCE drew those rows at brightness 1 and the rest at 0; the recomp has 0 everywhere, so a brightness write during the
frame reached rows already past. Why those rows were not yet drawn here has not been traced.

## What would fix it
The order of the two steps in the renderer, in snesrecomp (for upstream). The glue could rewrite each finished frame
with the same table as `SOE_COMPARE_FADE`, but brightness can change between lines, so that is only exact when it does
not.
