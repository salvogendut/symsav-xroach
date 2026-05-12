# xroach — SymbOS screensaver

A screensaver for [SymbOS](https://www.symbos.org/) inspired by the classic Unix/Linux `xroach` program, which displayed cockroaches scattering across the screen under open windows.

This version runs fullscreen on the Amstrad CPC, rendering cockroaches directly to video RAM while the desktop is suspended. A config dialog lets the user choose the number of roaches and animation speed.

> **Requirements:** CPC Mode 1 only — 320×200 pixels, 4 colours. Will not work on other screen modes for now.

**Work in progress.**

---

## Building

```bash
./build.sh
```

Requires the SCC compiler (set `SCC=` env var if not at `../scc/bin/cc`) and Python 3.
Build steps:

1. SCC compiles `xroach.c` → `xroach.sav` (raw binary)
2. `add_preview.py` patches the preview image into the binary at offset 256

Output: `xroach.sav`

---

## Screensaver protocol

SymbOS screensavers are processes that respond to messages from the screensaver manager:

| Message | Value | Action |
|---------|-------|--------|
| `MSC_SAV_INIT` | 1 | Manager sends saved config data via `Bank_Copy`. Load it. |
| `MSC_SAV_START` | 2 | Start the animation fullscreen. |
| `MSC_SAV_CONFIG` | 3 | Open the config dialog. `_symmsg[1]` = caller PID. |
| `MSR_SAV_CONFIG` | 4 | Send config data back to the manager after OK. |

### Config data format

A 6-byte struct stored as `_transfer char cfgdat[6]`:

```
Byte 0-3: magic "XRCH"
Byte 4:   roach count (3, 6, or 9)
Byte 5:   speed (1=slow, 3=normal, 6=fast)
```

On `MSC_SAV_INIT`, the manager sends its copy of this struct. On config OK, the screensaver sends its updated copy back with `MSR_SAV_CONFIG`.

### Animation loop

1. Open a fullscreen black window (`WIN_NOTTASKBAR | WIN_NOTMOVEABLE | WIN_NOTRESIZEABLE`)
2. `DSK_SRV_DSKSTP` (service 5) to freeze the desktop — **wait for the response** with `Msg_Wait(…, MSR_DSK_DSKSRV)`
3. Clear VRAM: 8 interleaved planes × `Bank_Copy(bank0, 0xC000 + k*0x800, _symbank, zeros, 2000)`
4. Per frame: erase old roach positions + draw new positions directly into bank 0 (VRAM)
5. Exit on keypress: `DSK_SRV_DSKCNT` (service 6) to resume desktop, then close window and `Screen_Redraw()`

---

## CPC Mode 1 screen format

- 320×200 pixels, 4 inks (2 bits per pixel), 80 bytes per row
- VRAM base: bank 0, `0xC000`
- Address formula: `0xC000 + (y/8)*80 + (y%8)*0x800 + x/4` (x must be multiple of 4)
- Screen is 8 interleaved "character planes"; plane k starts at `0xC000 + k*0x800` (2000 bytes each)
- Mode 1 byte encoding for 4 consecutive pixels p0–p3 (each a 2-bit ink value):
  ```
  byte = (p0_lo<<7)|(p1_lo<<6)|(p2_lo<<5)|(p3_lo<<4)
       | (p0_hi<<3)|(p1_hi<<2)|(p2_hi<<1)|(p3_hi)
  ```
  Common values: ink 0 → `0x00`, ink 1 → `0xF0`, ink 2 → `0x0F`, ink 3 → `0xFF`

Sprites must be X-aligned to multiples of 4. Horizontal movement uses discrete offsets `{3,2,0,-2,-3,-2,0,2}` snapped to 4-pixel boundaries.

---

## .sav binary format

SymbOS `.sav` (screensaver) files use the same binary layout as `.com` executables. The file is divided into four contiguous regions:

```
[0           .. code_len)        Code segment
[code_len    .. code_len+data_len)        Data segment
[code_len+data_len .. code_len+data_len+trans_len)   Transfer segment (always loaded at 0xC000)
[code_len+data_len+trans_len .. end)     Relocation table
```

Header (first bytes of code segment):

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | `code_len` — length of code segment |
| 2 | 2 | `data_len` |
| 4 | 2 | `trans_len` |
| 6 | 2 | original origin (0x0000 for SCC) |
| 8 | 2 | `reloc_count` — number of reloc table entries |
| 10 | 2 | stack size |
| 15 | 25 | program name (null-terminated) |
| 48 | 8 | `"SymExe10"` identifier |
| 90 | 19 | small icon (format 2, 8×8) |
| 109 | 147 | large icon (format 6, 24×24) |
| 0–255 | 256 | **256-byte program header total** |

SCC extended header (code segment bytes 256–285, all set by the runtime):

| Code offset | Field |
|-------------|-------|
| 276 | `__segcode` |
| 280 | `__heapsize` |
| 282 | `__debugtrace` |
| 284 | `__debugstack` |

Code execution begins at byte 286 of the code segment (`start2`). The process entry point is stored as a `.word start2` in the transfer segment and is covered by the reloc table.

### Relocation table

Each entry is a 16-bit **file offset** (not a code-segment-only offset) pointing to a 16-bit value somewhere in the code, data, or transfer segment that needs to be adjusted at load time. The table covers all three segments.

All addresses in the reloc values are stored relative to base 0 (the code segment load address). At runtime, SymbOS adds the actual load address to each value:

- Values in `[0, code_len)` → point into the code segment
- Values in `[code_len, code_len+data_len)` → point into the data segment (data is loaded immediately after code in the same bank)
- Values `≥ 0xC000` → point into the transfer segment (fixed at 0xC000; no runtime adjustment needed)

---

## Preview image

The screensaver manager displays a thumbnail when listing screensavers. The preview image is embedded in the `.sav` file starting at **file offset 256** (immediately after the 256-byte program header).

Format:

```
Byte 0:    image format (16 = Mode 1)
Byte 1:    width in pixels (64)
Byte 2:    height in pixels (40)
Bytes 3–642: pixel data (640 bytes, Mode 1 encoding, 16 bytes per row × 40 rows)
```

Total: **643 bytes**.

### Inserting the preview (post-build patching)

SCC does not provide a way to place data at a specific fixed offset within the code segment. Instead, `add_preview.py` patches the compiled binary after the build:

1. Extract 643 preview bytes from the source (currently the starfield placeholder from `../symsav-starfield/Scr-Starfield.asm`)
2. Insert them at file offset 256, shifting everything that follows by 643 bytes
3. Update the relocation table:
   - Every reloc **entry** (file offset) ≥ 256 → `+= 643`
   - Every 16-bit **value** at a reloc entry that is ≥ 256 → `+= 643`
4. Update `code_len` in the header by `+= 643`

The rule `value >= 256 → value += 643` covers all three categories of pointer:
- Code pointers to bytes 256+ (the code that moved)
- Data pointers (data segment shifted because `code_len` grew)
- Transfer-internal pointers (transfer segment shifted too)
- The `start2` entry point stored in the transfer segment is a code pointer and is handled automatically via its reloc entry

To replace the placeholder with a custom xroach preview, edit the `parse_preview()` function in `add_preview.py` to supply different pixel data and re-run `./build.sh`.

---

## SCC notes

- `_transfer` segment: always loaded at `0xC000`. Use for variables shared with the SymbOS kernel (window structs, control arrays, message buffer).
- `_data` segment: zero-initialised globals go here (BSS).
- `Ctrl` structs for `Ctrl_Group` **must be consecutive** in the `_transfer` segment.
- Nested `switch` inside another `switch`'s `default:` case triggers a compiler bug ("two default cases"). Use `if/else` chains instead.
- `C_AREA` param `COLOR_ORANGE` (= 2, no `AREA_16COLOR` flag) gives the standard SymbOS orange window background.
- Radio button `status` pointer is written by the framework when the button is selected; initialise `rg_xxx[4] = {-1,-1,-1,-1}` per group before opening a window.
