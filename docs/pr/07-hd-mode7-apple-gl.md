# 7. HD Mode 7 on Apple's OpenGL (no double precision)

**Status:** draft, no PR yet. Independent of 1-6: it touches only `desktop/hd_gpu.c`, `desktop/hd_gpu.h`,
`tests/ppu/ppu_hd_gpu_test.c` and `tests/ppu/validate_hd_glsl.py`, and applies to main `816bc5ac` alone.

Branch: `hd-mode7-no-fp64` in the fork (on main), `3f55bcdb`; on `develop` as `01904551`.

## To do before opening

1. Run `tests/ppu/validate_hd_glsl.py` with glslangValidator (not installed on the Mac it was made on).
2. Run `ppu_hd_gpu_test` on a GL4 host that is not a Mac, also with `SNESRECOMP_HD_NO_FP64=1`: the double path must stay
   exact and the single path must stay under the 0.1 % bound there too.

## PR title

HD Mode 7: draw camera rows without double precision on Apple's OpenGL

## PR text

```markdown
On macOS the HD Mode 7 presenter draws a black picture. Apple's OpenGL 4.1 compiles the GL4 double-precision affine and
coordinate shaders, but cannot build them for Metal and falls back to software processing, which draws black. No GL error
is reported. The driver logs:

    Fallback to SW vertex processing because buildPipelineState failed
    Fallback to SW fragment processing because buildPipelineState failed

`ppu_hd_gpu_test` shows it on an Apple M4: the first camera-row comparison reads `000000`.

On Apple, without GL4, or with `SNESRECOMP_HD_NO_FP64` set, the presenter no longer needs doubles on the GPU:

- the coordinate plane (texel and Q16 fraction at every native column edge of every enhanced row) is computed on the CPU
  with the coordinate shader's double expression and uploaded instead of rendered;
- the affine shader is a GL 3.3 single-precision variant (`HD_AFFINE` without `HD_FP64`). A sample within 2/65536 of a
  texel edge keeps the Q16 interpolation instead of the double re-evaluation, and rows without the plane use float.

Such a sample may take the neighbouring texel. `SnesHdGpuAffineExact` tells callers which path runs. The double path is
unchanged.

Also:
- Timer queries are off when `GL_TIMESTAMP` has zero `GL_QUERY_COUNTER_BITS`: Apple reports zero bits and returns zeros.
- The parity test skips full-raster targets above `GL_MAX_TEXTURE_SIZE` (16384 on Apple M-series; 64x is 18432 wide)
  and does not compare ROIs on admitted targets that were halved. On the single-precision path it allows edge differences
  up to 0.1 %: 301 of 927,376 samples on an Apple M4.
- `validate_hd_glsl.py` compiles all three variants.

Checked in a game (Secret of Evermore, its Mode 7 flight at 8x, OpenGL output on an Apple M4): black before, a full HD
picture after.
```

## Required

Not for sync: the presenter changes no emulated state. Required for HD Mode 7 to show anything on a Mac.

## Not game specific?

Yes. Host GPU capability only, no game value.
