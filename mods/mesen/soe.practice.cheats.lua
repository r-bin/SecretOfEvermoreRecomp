-- Practice hotkeys for MesenCE 2.2.1: the recomp mod soe.practice.cheats (src/soe_cheats.c) as a Lua script.
-- Docs: docs/mods/cheats.md. Load it in Mesen's Script Window (Debug > Script Window > Open) with the US ROM running.
--
-- Hold Start and press a direction in the field (not in cutscenes or text boxes):
--   Start + Up    no clip on / off       Start + Right  Atlas glitch on / off
--   Start + Left  all items              Start + Down   weapon levels 3
-- The settings below are the mod's options; change them here.
--
-- Same rules, same moment as the recomp: the cheat runs when the CPU reaches $8E:E45C (per_frame_field_tick's JSL
-- script_scheduler_pass, after the joypad matcher read $0EC6), so with the same input both write the same WRAM at the
-- same instruction (checked with tools/compare --mesen-lua, docs/mods/cheats.md).
do
  local ACTIONS = { up = "noclip", right = "atlas", left = "items", down = "levels" }   -- noclip atlas items levels none
  local TARGET = "both"                                                                 -- both boy dog

  local wram = emu.memType.snesWorkRam
  local BOY, DOG = 0x4E89, 0x4F37
  local HELD = 0x0EC6                              -- $4218 order
  local START, UP, DOWN, LEFT, RIGHT = 0x1000, 0x0800, 0x0400, 0x0200, 0x0100
  local ATLAS = 0xEFFF
  local ATLAS_WORDS = { { 0x0A3F, 0x0A41, BOY + 0xA0, BOY + 0xA2 }, { 0x0A89, 0x0A8B, DOG + 0xA0, DOG + 0xA2 } }
  local targets = { TARGET ~= "dog", TARGET ~= "boy" }   -- Boy, Dog

  local noclip, atlas, last_chord = false, false, 0
  local kept = { {}, {} }                          -- the game's own Atlas word values while ours is held

  local function r8(a) return emu.read(a, wram, false) end
  local function w8(a, v) emu.write(a, v & 0xFF, wram) end
  local function r16(a) return r8(a) | (r8(a + 1) << 8) end
  local function w16(a, v) w8(a, v); w8(a + 1, v >> 8) end
  local function fill(from, to, v) for a = from, to do w8(a, v) end end

  local function hold_noclip(on)
    for i, rec in ipairs({ BOY, DOG }) do
      if targets[i] then
        local f = r8(rec + 0x11)
        w8(rec + 0x11, on and (f | 0x04) or (f & ~0x04))
      end
    end
  end

  local function hold_atlas(on)
    for i, rec in ipairs({ BOY, DOG }) do
      if targets[i] then
        for k, a in ipairs(ATLAS_WORDS[i]) do
          if on then
            if r16(a) ~= ATLAS then kept[i][k] = r16(a); w16(a, ATLAS) end
          elseif kept[i][k] then
            if r16(a) == ATLAS then w16(a, kept[i][k]) end
            kept[i][k] = nil
          end
        end
        local f = r8(rec + 0x14)
        w8(rec + 0x14, on and (f | 0x08) or (f & ~0x08))
      end
    end
  end

  local function all_items()                       -- unlock(ITEM.ALL)
    for _, a in ipairs({ 0x0AC6, 0x0AC9, 0x0ACC, 0x0ACF }) do
      if (r16(a) | (r8(a + 2) << 16)) < 6666666 then w16(a, 6666666 & 0xFFFF); w8(a + 2, 6666666 >> 16) end
    end
    fill(0x2258, 0x225D, 0xFF)
    w8(0x2261, 0xF4); fill(0x2262, 0x2264, 0xFF)
    fill(0x22DA, 0x22DB, 0xFF)
    fill(0x22FF, 0x2344, 99)
    fill(0x2345, 0x2347, 0xFF)
    for a = 0x2517, 0x252F, 2 do w16(a, 99) end
  end

  local function weapon_levels()                   -- unlock(ITEM.WEAPON_LEVEL_3) + DOG_LEVEL_3
    for a = 0x0ADD, 0x0AF7, 2 do if r16(a) < 0x0300 then w16(a, 0x0300) end end
    if r16(0x0B07) < 0x0300 then w16(0x0B07, 0x0300) end
  end

  local function run(action)
    if action == "noclip" then
      noclip = not noclip
      if not noclip then hold_noclip(false) end
      emu.displayMessage("Cheats", noclip and "No clip on" or "No clip off")
    elseif action == "atlas" then
      atlas = not atlas
      if not atlas then hold_atlas(false) end
      emu.displayMessage("Cheats", atlas and "Atlas glitch on" or "Atlas glitch off")
    elseif action == "items" then
      all_items(); emu.displayMessage("Cheats", "All items")
    elseif action == "levels" then
      weapon_levels(); emu.displayMessage("Cheats", "Weapon levels 3")
    end
  end

  emu.addMemoryCallback(function()
    local held = r16(HELD)
    local chord = (held & START) ~= 0 and (held & (UP | DOWN | LEFT | RIGHT)) or 0
    if chord ~= last_chord and (r8(BOY + 0x10) & 0x21) == 0 then
      if chord == UP then run(ACTIONS.up)
      elseif chord == RIGHT then run(ACTIONS.right)
      elseif chord == LEFT then run(ACTIONS.left)
      elseif chord == DOWN then run(ACTIONS.down) end
    end
    last_chord = chord
    if noclip then hold_noclip(true) end
    if atlas then hold_atlas(true) end
  end, emu.callbackType.exec, 0x8EE45C, 0x8EE45C, emu.cpuType.snes, emu.memType.snesMemory)
end
