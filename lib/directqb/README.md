# DirectQB for QBLike

[Español](README_es.md)

`DIRECTQB.BI` is a compatibility layer for **DirectQB 1.61**, the assembly game library for QuickBASIC 4.5
by Angelo Mottola (Enhanced Creations, 1998-99). It contains every SUB and FUNCTION of DirectQB, with the
same names, parameters and results, written in plain QBLike BASIC. A game written for DirectQB runs in
QBLike without changes: there is no `.LIB` or `.QLB` to load, the include file is the whole library.

## Usage

1. Copy `DIRECTQB.BI` next to the game, replacing the original one.
2. Open the main `.BAS` in QBLike and run it. The program already has the line

   ```basic
   '$INCLUDE: 'DIRECTQB.BI'
   ```

   at the top, as the DirectQB manual required. Keep it before any other code: the include file
   declares the constants (`TRUE`, `VIDEO`, `B0`, `KEYESC`...), `DEFINT A-Z` and the library state.
3. *File > Make EXE File* works too: the library goes inside the executable.

```basic
DEFINT A-Z
'$INCLUDE: 'DIRECTQB.BI'
IF DQBinit(1, 0, 0) THEN DQBclose: PRINT DQBerror$: END
DQBinitVGA
DQBinstallKeyboard
DIM spr(DQBsize(0, 0, 15, 15) \ 2)
DQBboxf VIDEO, 0, 0, 15, 15, 4
DQBget VIDEO, 0, 0, 15, 15, VARSEG(spr(0)), VARPTR(spr(0))
DO
  DQBclearLayer 1
  DQBput 1, x, 100, VARSEG(spr(0)), VARPTR(spr(0))
  x = (x + 1) MOD 320
  DQBwait 1
  DQBcopyLayer 1, VIDEO
LOOP UNTIL DQBkey(KEYESC)
DQBclose
```

## How it works

| DirectQB | QBLike |
|---|---|
| VGA mode 13h, `VIDEO` (`&HA000`) | `SCREEN 13`, page 0 |
| Layers in EMS (1 to n) | `SCREEN 13` pages 1 to n (QBLike has 64 pages in SCREEN 13) |
| Base layers `B0`-`B9` | the pages after the layers |
| Sprites in QB arrays, passed with `VARSEG`/`VARPTR` | the same: QBLike emulates array addresses, and `GET`/`PUT ... SEG s, o` draw them natively |
| Clipping box | the `CLIP` rectangle (for `PUT`) and `VIEW SCREEN` (for lines, text...) |
| EMS data area (`DQBpeek`/`DQBpoke`) | an array |
| Keyboard handler | `MULTIKEY` |
| Mouse driver | `MOUSE` |
| Sound Blaster mixer | `SOUND` / `SOUND PLAY` (12 channels, any sample rate) |
| Timer (`DQBsetFrameRate`) | `TIMER` |
| `DQBwait` (vertical retrace) | `WAIT VSYNC` |

Drawing sprites (`DQBput`, `DQBget`, `DQBfPut`, `DQBxPut`, `DQBmPut`, `DQBsPut`, `DQBputOver`), copying
and clearing layers, scrolling, boxes, lines, ellipses, flood fill and text use native QBLike statements
(`PUT`/`GET` with image buffers, `PCOPY`, `LINE` with style patterns, `FILL`...), so they are fast.
Per-pixel effects are written in BASIC: rotation (`DQBrPut`), `DQBhPut`, `DQBpPut`, `DQBtPut` with
`BIT.AND`, blending (`DQBbPut`, `DQBfilterBox`, `DQBcopyBlendLayer`, blended or textured text), gouraud
and textured triangles, and image/FLI decoding. A full-screen `DQBrPut` takes about 50 ms; sprite-sized
ones are instant.

## Differences from the original

Everything a game can see works as in DirectQB, with these exceptions:

- **Layers need SCREEN 13.** Up to 53 layers can be requested (`DQBinit` returns error 3 above that).
  Drawing on a layer before `DQBinitVGA` switches to SCREEN 13 at that moment (on DOS the text screen
  stayed visible until then); `DQBinitVGA` doesn't clear the layers afterwards.
- **`DQBmapLayer(layer)` returns `&HA000`** and makes that layer the active page, so
  `DEF SEG = DQBmapLayer(n): POKE ...` writes on layer n as before. While a layer other than `VIDEO` is
  mapped, QBasic statements (`PSET`, `PRINT`, `POKE &HA000`...) also go to that layer; call
  `DQBmapLayer(VIDEO)` to go back.
- **`DEF SEG`.** DirectQB calls that read sprite data (and `DQBpeek`, `DQBsort`, the image routines...)
  leave `DEF SEG` at its default value.
- **Sound.** `DQBinstallSB` accepts any settings and always succeeds (if `DQBinit` reserved sound slots).
  Sounds are 8-bit mono WAV files, as in DirectQB. Voices above 12 share QBLike's 12 channels.
  `DQBsetVoiceVol` and `DQBsetVolume` apply from the next `DQBplaySound`. QBLike can't pause sounds:
  `DQBpauseSound` stops them and `DQBresumeSound` does nothing.
- **Joystick.** There is none: `DQBjoyDetected` returns `FALSE` and the other joystick functions 0.
- **Mouse.** The cursor is QBLike's arrow: `DQBsetMouseShape` and `DQBsetMouseSpeed` are ignored.
  `DQBsetMousePos` can't move the system pointer, so it shifts the position the functions return.
- **Keyboard.** `DQBkey` works even without `DQBinstallKeyboard`, and `INKEY$` keeps working while the
  handler is "installed" (it's emptied when it is installed or removed).
- **Disk.** `DQBnumDrives` returns 3, `DQBsetDrive` does nothing and `DQBdir$` doesn't return volume labels.
  `DQBpath$` uses backslashes on every system.
- **Errors.** `DQBsaveImage` and `DQBsaveBMap` always return 0; a disk error stops the program with a
  QBLike error message. DirectQB functions that crashed the machine with bad arguments (layers that
  don't exist, sprites out of range) draw on `VIDEO` or raise *Illegal function call* instead.
- **Bugs fixed.** A few bugs of DirectQB 1.61 behave as the manual describes instead: `DQBgetBMap` with
  any foreground color, the length of sound and user packets in datafiles, `DQBcollideOnLayer` with the
  sprite partly above the clipping box, `DQBpPut` in solid mode and saving blender maps.
- Programs that also call the mouse or EMS drivers directly (`CALL INTERRUPT`, `CALL ABSOLUTE`) need
  those parts rewritten: QBLike doesn't run machine code.

The QBLike extensions this library is built on are documented in
[EXTENSIONES_en.md](../../EXTENSIONES_en.md): image buffers and `GET`/`PUT ... SEG`, array addresses with
`VARSEG`/`VARPTR`, SCREEN 13 pages, `SOUND(channel)`, `SETMEM`, `DIR$` and `CURDIR$`.

## Files

| File | Contents |
|---|---|
| `DIRECTQB.BI` | the library |
| `tests/T_CORE.BAS` | automatic test: layers, drawing, sprites, clipping, collisions, bits, palette, text, blender maps, triangles (writes `T_CORE.LOG`) |
| `tests/T_IO.BAS` | automatic test: BMP/PCX/BSAVE images, fonts, datafiles, sounds, FLI animations, disk functions (writes `T_IO.LOG`) |
| `tests/T_VISUAL.BAS` | a screen showing text styles, primitives, triangles and every kind of sprite put |
| `tests/ECLOGO.BAS` | the Enhanced Creations logo animation that came with DirectQB (`ECLOGO.PCX`) |
| `tests/*.FNT` | the fonts that came with DirectQB |
| `tests/mkdata.py` | creates the test datafile, FLI and WAV (`T_DATA.DQB`, `T_ANIM.FLI`, `T_SOUND.WAV`) |

Run the tests from the `tests` folder; each log ends with the number of failed checks.

## Credits

DirectQB is freeware by Angelo Mottola - Enhanced Creations 1998-99. Its author asked games made with it
to credit him; please keep doing so. This compatibility layer was written from the DirectQB 1.61 manual
and source code; the fonts and `ECLOGO.PCX` in `tests` come from the DirectQB package.
