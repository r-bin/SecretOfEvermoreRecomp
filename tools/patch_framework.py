#!/usr/bin/env python3
"""Hook the beam frame driver's resume point for save states (idempotent).

snesrecomp's file snapshots carry neither g_cpu nor the beam driver's resume PC, so
src/game_rtl.c saves and restores both. The driver keeps the PC in a static; this adds
three accessors after it. CMakeLists.txt runs this at configure time, so a fresh
submodule checkout is patched before it is compiled. Fails loudly when the framework's
layout changes.
"""
import pathlib, sys

root = pathlib.Path(__file__).resolve().parent.parent
file = root / "snesrecomp" / "runner" / "src" / "beam_frame_driver.c"
s = file.read_text()
if "snes_beam_frame_driver_set_resume" in s:
    sys.exit(0)
anchor = "static bool s_wai_halted;\n"
if anchor not in s:
    sys.exit(f"patch_framework: {file} layout changed: cannot hook the resume point")
s = s.replace(anchor, anchor +
    "\n/* SecretOfEvermoreRecomp (tools/patch_framework.py): save states restore the resume point. */\n"
    "uint32_t snes_beam_frame_driver_resume_pc(void) { return s_resume_pc; }\n"
    "bool snes_beam_frame_driver_wai_halted(void) { return s_wai_halted; }\n"
    "void snes_beam_frame_driver_set_resume(uint32_t pc, bool wai_halted) { s_resume_pc = pc; s_wai_halted = wai_halted; }\n", 1)
file.write_text(s)
print(f"patch_framework: hooked {file.relative_to(root)}")
