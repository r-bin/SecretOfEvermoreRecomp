# 7. HD Mode 7 on Apple's OpenGL (no double precision)

**Status:** open, https://github.com/RetroPortingToolKit/snesrecomp/pull/279 (2026-11-10). CI: `Native analyzer` / `Linux x86-64` / `Run C runtime tests` fails, as it does on main
`e9c20732` itself and on every PR since (run 42715630813); `tests/run_c_tests.sh` builds none of the files this PR
changes. Windows and macOS jobs pass.

Branch: `hd-mode7-no-fp64-main` in the fork, `4e838c47` on main `e9c20732` (2026-11-10; the older `hd-mode7-no-fp64` is on `816bc5ac`);
on `develop` as `30c4fb1f`.

Checked on main `e9c20732`, Apple M4 (2026-11-10). snesrecomp has no build rule for `ppu_hd_gpu_test`; it was built by hand: the test,
`desktop/hd_gpu.c`, `snes/hd_jobs.c`, `snes/ppu.c`, `snes/ppu_legacy.c`, `third_party/gl_core/gl_core_3_1.c`, with
`-DSNESRECOMP_SDL3=1 -DSNESRECOMP_HD_GPU=1`, linked to SDL3 (this repo's `build/_deps`). Main: Metal pipeline fallback, then
`GPU mismatch scale2 pixel0,0 cpu424242 gpu000000`. Branch: `affine single + CPU plane`, 301 of 927,376 samples differ at texel
edges, 660 ROI comparisons at 1-64x pass.

## To do before opening

1. Done 2026-11-10: `tests/ppu/validate_hd_glsl.py --validator glslangValidator` (Khronos glslang 16.6.0, Homebrew) passes all four
   programs on the branch: register, affine single, affine double, coordinate (main: its three).
2. Run `ppu_hd_gpu_test` on a GL4 host that is not a Mac, also with `SNESRECOMP_HD_NO_FP64=1`: the double path must stay
   exact and the single path must stay under the 0.1 % bound there too.

## PR title

HD Mode 7: draw camera rows without double precision on Apple's OpenGL

## PR text

````markdown
## Summary

On macOS the HD Mode 7 presenter draws a black picture. Apple's OpenGL 4.1 compiles the GL4 double-precision affine and
coordinate shaders, but cannot build them for Metal. It falls back to software processing, which draws black, and reports
no GL error.

```diff
 SnesHdGpuCreate
   if GL4 and not Apple and not SNESRECOMP_HD_NO_FP64
     affine shader: GL4, double precision      (unchanged)
     coordinate plane: GL4 shader
+  else
+    affine shader: GL 3.3, single precision
+      a sample within 2/65536 of a texel edge keeps the Q16 interpolation
+    coordinate plane: computed on the CPU with the same double expression, uploaded
+  SnesHdGpuAffineExact tells callers which path runs
```

Also:
- Timer queries are off when `GL_TIMESTAMP` has 0 `GL_QUERY_COUNTER_BITS` (Apple reports 0 and returns zeros).
- `ppu_hd_gpu_test` skips full-raster targets above `GL_MAX_TEXTURE_SIZE` (16384 on Apple M-series; 64x is 18432 wide)
  and does not compare ROIs on admitted targets that were halved. On the single-precision path it allows texel-edge
  differences up to 0.1 %.
- `validate_hd_glsl.py` compiles all three shader variants.

## Evidence

`ppu_hd_gpu_test`, Apple M4, macOS:

- **Before** (main `e9c20732`):
  ```
  FALLBACK (log once): Fallback to SW fragment processing because buildPipelineState failed
  GPU mismatch scale2 pixel0,0 cpu424242 gpu000000
  ```
  **After:**
  ```
  SNES HD GPU available: Apple M4, max texture16384, source layers256, affine single + CPU plane
  single-precision camera rows: 301 of 927376 samples differ at texel edges
  GPU parity: 660 ROI comparisons at1..64x (including33/40/48/56x) and shared target/foreground/state checks passed
  ```

`validate_hd_glsl.py` with glslangValidator 16.6.0: register, affine single, affine double and coordinate all compile and
link.

In a game (Secret of Evermore's Mode 7 flight at 8x, OpenGL output, M4): black before, a full HD picture after.

## Merge Danger

**Door:** two-way. Presentation only; no emulated state changes, and netplay peers present locally.

**Blast Radius:** small. Only hosts on the single-precision path (macOS, no GL4, or `SNESRECOMP_HD_NO_FP64=1`). Those
were black before; now 0.03 % of samples may take the neighbouring texel at an edge. The GL4 double path is unchanged.

Not yet checked: a GL4 run on a non-Mac host. Could someone with Linux or Windows run `ppu_hd_gpu_test` as is and with
`SNESRECOMP_HD_NO_FP64=1`? The first must stay exact, the second under the 0.1 % bound.
````

## Required

Not for sync: the presenter changes no emulated state. Required for HD Mode 7 to show anything on a Mac.

## Not game specific?

Yes. Host GPU capability only, no game value.
