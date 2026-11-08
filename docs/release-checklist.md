# Before the first release

What to settle before `.github/workflows/release.yml` is run with `publish_release` checked. Pushing source is not a
release: the repository is already public. A release ships binaries, and binaries carry extra duties. Tick each item off here
in the commit that settles it; delete this file once the first release is out and every item holds as a rule elsewhere.

## Licenses

- [x] `LICENSE` carries `Required Notice:` lines for this project and for snesrecomp. PolyForm only obliges others to pass
  on lines that start with `Required Notice:`. A plain `Copyright` line is not passed on.
- [ ] A top-level `THIRD_PARTY_NOTICES.md` (or a section in the README) lists everything a zip contains and the license of
  each: this project and snesrecomp (PolyForm Noncommercial 1.0.0), snesrev's zelda3/smw code inside snesrecomp (MIT,
  see `snesrecomp/THIRD_PARTY_ATTRIBUTION.md`), recomp-ui (MIT), SDL3 (zlib) and anything else the host links in.
  The zip already holds every tracked file, submodules included (`snesrecomp/tools/ci/stage_setup_host.sh`), so the
  license files are inside it, but several levels down. MIT asks for its notice with every binary, so put it at the top.
- [ ] With `embed_toolchain` on, the zip also holds the cmake-clang-v1 pack (CMake, Clang/LLVM, Ninja, maybe more). List
  their licenses (BSD-3, Apache-2.0 with LLVM exception, ...), or ship only lean zips that download the pack.
- [ ] `framework_pins.txt` names `recomp-net` and `retcomm-rbengine`. Find out whether a build links code from either.
  If one does, check its license and add it to the notices. If either is private, its code must not end up in a public zip.
- [ ] Find out whether snesrecomp's author wants anything beyond the license (credit in the README, a notice in the
  release text). The license does not require it, but it's polite in a small community.

## The game

- [ ] The README states that the project is not affiliated with or endorsed by Square Enix, that *Secret of Evermore* is
  their trademark, and that the player needs a legally obtained ROM of their own. Put the same lines in the release text.
- [ ] The release text and the launcher do not use the game's logo, box art, screenshots or music. The executable name
  and the zip name may use the title as plain text.
- [ ] A zip from a dry run (`publish_release` unchecked) has been unpacked and searched: no `*.sfc`/`*.smc`/`*.srm`, no
  `src/gen/`, no `recomp/funcs.h`, no `imported/`, no save states (`*.mss`) or `.mmo` replays with memory in them. The
  packager checks most of this on its own. Check by hand once anyway.

## Names and data from elsewhere

- [x] The WRAM names in `src/bound-c/include/soe_ram.h` are our own. We worked out from playing the game what each
  address does and named it from that; the game's original names are not known. The routine names in `recomp/bank*.cfg`
  come from the same research.
- [ ] everscript-vscode still holds room, enemy, item and flag names taken from third-party tools (its `docs/cleanup.md`
  D3, D4). Do not import any of them here until they have been rewritten there.
- [ ] No byte tables from the ROM are in tracked files. Opcode lists and other CPU facts are fine; graphics, text, tables
  and code bytes are not. One way to check: `git grep -nIE '(0x[0-9A-Fa-f]{2}, *){16}'`, then read each hit.

## Identity and hygiene

- [ ] Every commit on the released branch has the noreply author address, with no local paths (`/Users/...`) or private
  emails in tracked files (`git log --format='%ae %ce' | sort -u`, `git grep -nI '/Users/'`).
- [ ] `VERSION` says what the first release is; `main` holds the release commit (AGENTS.md rule 8).
- [ ] The README says how to report a problem and that the project is noncommercial, with no donation links that could
  read as selling the game.
