# Idea: `.evs` Script Debugging in the Recomp

While `snes9x2005-wasm` remains the primary fast-loop environment for F5 debugging inside VS Code,
the recompiled native desktop executable (`snesrecomp`) can also support `.evs` script debugging.

## Script VM Opcode Interception
In Secret of Evermore, `.evs` code does not compile into native 65816 assembly; it is bytecode executed by
the in-game interpreter at `$8C:D0A6` (`lda [$82]`):
* `$82..$84`: 24-bit pointer to the current script instruction about to run.
* `$7E`: Running slot pointer (one of 20 slots at `$7E28FC`, each 0x4F bytes).

In the recompilation host (`src/game_rtl.c`), a native C hook at `$8C:D0A6` can intercept
every bytecode fetch:
> **Reconstructed C Decompilation**:
> Source moved to [`reconstructed-c/80/808020_debugging.c`](https://github.com/r-bin/everscript-vscode/blob/develop/reconstructed-c/80/808020_debugging.c).
> [View in VFS: soe://rom/code/808020](soe://rom/code/808020?c=1&asm=0)


## Native Trusted Plugin vs. `.snesmod`
Because `.snesmod` archives follow a strict data-only security model (no arbitrary shared libraries or executable code),
a `.snesmod` alone cannot open network sockets or control process execution threads:
1. **The Engine (Native Trusted Plugin):** Written in C/C++ in `src/`, statically linked into the executable. Runs a local TCP server listening for Debug Adapter Protocol (DAP) messages and synchronizes breakpoints across threads.
2. **The Front-End (`soe-evs-debugger.snesmod`):** Exposes options in the launcher UI to toggle the debug server, select the TCP listening port, and choose whether to halt execution on game boot.

## Workflow Comparison: Recomp vs. snes9x Webview

| Characteristic | `snes9x2005-wasm` Webview | `snesrecomp` Desktop Host |
|---|---|---|
| **Best For** | 5-second edit-compile-step iteration loop | Playtesting in 16:9 60fps with active `.snesmod` addons |
| **Launch Setup** | Zero setup; runs entirely inside VS Code process | Requires building native desktop binary |
| **Hot Reload** | Instant in-memory ROM re-patching | In-memory patch reloading or restarted executable |
| **Transport** | Webview `postMessage` | Local TCP socket (e.g. `localhost:54321`) |

