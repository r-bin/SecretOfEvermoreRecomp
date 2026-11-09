# Fog of war

Everything outside the original 4:3 columns behind fog. Proposal; no game facts beyond those
linked. Marks: [README](README.md).

## How to draw it

A presenter post-process over the margin columns of the composed frame (`draw_frame` /
`end_sim_frame` hooks, `snesrecomp/runner/src/desktop/host_main.h:142`), not PPU colour math or
windows: the guest's own colour math and windows stay untouched, and the fog never enters the
emulated state.

| Option | Look |
|---|---|
| off | — |
| dim | multiply |
| fog | desaturate + dim + animated noise, world-anchored (scrolled with `$59`/`$5B`) so it does not slide with the screen |
| black | margins black (like a pillarbox, but ghosts can show) |

Settings: strength 0..100, feather 0..32 px at the 4:3 seam, "ghosts through fog".

## Order

background margins → fog → ghosts (above the fog when "ghosts through fog" is on, else below) →
HUD (never fogged). Since ghosts are composed inside the PPU, "ghosts above fog" means the
presenter skips fogging pixels that came from ghost objects: keep a mask of their screen rects.

## Interactions

* Fades: multiply the fog by the brightness ([fades](fades.md)).
* Dark rooms (lighting modes 4/5, colour-math darkening): the fog darkens on top; lower the
  strength there or turn it off per camera mode ([backgrounds-and-parallax](backgrounds-and-parallax.md)).
* Enemy modes ([modes](modes.md)): vanilla + fog = calm default; hybrid + fog = silhouettes in the
  mist; E1/E2 + fog hide what the game now simulates, which defeats them.
* Camera-limit boxes and small rooms: outside the box the margin is already blank; fog only applies
  to authored margin pixels.
