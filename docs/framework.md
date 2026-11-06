# The framework

`snesrecomp/` is a submodule on public upstream, at the commit in `framework_pins.txt`. The changes this port needs that are
not upstream yet are one file, `framework/snesrecomp.patch`. `tools/regen.sh` applies it through
`tools/apply-framework-patch.sh`, so a clean clone needs only `git submodule update --init --recursive`. The submodule
then shows as modified, which `.gitmodules` ignores (`ignore = dirty`).

To run on another checkout of the framework, set `SNESRECOMP_ROOT` for `regen.sh` and pass `-DSNESRECOMP_ROOT=...` to CMake. It
is used as it is, unpatched.

## Moving to a new framework state

1. Make the patch: in a framework checkout, `git diff --binary <upstream commit> <your state> > framework/snesrecomp.patch`.
2. Set `snesrecomp` and `recomp-net` in `framework_pins.txt`, and the submodule, to `<upstream commit>`.
3. `git -C snesrecomp checkout -f <upstream commit> && git -C snesrecomp clean -fd`, then `bash tools/regen.sh` and build.
4. Run replay 01 ([testing.md](testing.md)). When a state changes what the replays measure, record the reference again.

What is in the patch and how it goes upstream: [pr/README.md](pr/README.md).
