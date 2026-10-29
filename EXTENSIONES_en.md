# QBLike extensions

Reference for everything QBLike adds to QBasic 1.1: new statements and emulation of the hardware of
the time.

Programs that don't use the new statements work exactly as in QBasic. QBLike also adds the `SCREEN 3`
mode (160×200, 16 colors, from the PCjr/Tandy) and the artifact colors of composite CGA in SCREEN 2.
Since `MULTIKEY`, `MOUSE`, `CLIP`, `LOADBMP`, `LOADGIF` and `LOADPAL` are reserved words in QBLike, they
cannot be used as variable names.

The only behavior difference from QBasic is that `PUT` clips the image instead of raising
*Illegal function call* when it goes off the screen (see [CLIP](#clip)).

## Contents

- [New statements](#new-statements)
  - ['$INCLUDE](#include)
  - [MULTIKEY](#multikey)
  - [MOUSE](#mouse)
  - [PUT ... MASK](#put--mask)
  - [CLIP (and clipped PUT)](#clip)
  - [GET / PUT with image buffers and addresses](#get--put-with-image-buffers-and-addresses)
  - [FILL](#fill)
  - [MUSIC](#music)
  - [SOUND (sound effects)](#sound-sound-effects)
  - [LOADBMP](#loadbmp)
  - [LOADGIF](#loadgif)
  - [LOADPAL](#loadpal)
  - [SCREEN 3 (PCjr / Tandy 1000)](#screen-3-pcjr--tandy-1000)
  - [SCREEN FULL / SCREEN WINDOW](#screen-full--screen-window)
  - [DIR$, CURDIR$ and SETMEM (QuickBASIC functions)](#dir-curdir-and-setmem-quickbasic-functions)
- [Emulated hardware](#emulated-hardware)
  - [Video memory (PEEK / POKE)](#video-memory-peek--poke), including the planar EGA/VGA modes
  - [Arrays in memory (VARSEG / VARPTR)](#arrays-in-memory-varseg--varptr)
  - [SCREEN 13 pages](#screen-13-pages)
  - [Vertical sync (INP &H3DA, WAIT, WAIT VSYNC)](#vertical-sync-inp-h3da-wait-wait-vsync)
  - [VGA palette (OUT / INP &H3C7-&H3C9)](#vga-palette-out--inp-h3c7-h3c9)
  - [CGA composite: artifact colors (OUT &H3D8)](#cga-composite-artifact-colors-out-h3d8)
  - [Keyboard (INP &H60, PEEK &H417)](#keyboard-inp-h60-peek-h417)
  - [AdLib / Sound Blaster FM (OUT / INP &H388)](#adlib--sound-blaster-fm-out--inp-h388)
  - [BIOS data (PEEK &H46C, &H449...)](#bios-data-peek-h46c-h449)
  - [BLOAD / BSAVE](#bload--bsave)
- [DirectQB library](#directqb-library)
- [Key code table](#key-code-table)
- [Complete example](#complete-example)

---

## New statements

### '$INCLUDE

```
'$INCLUDE "filespec"
'$INCLUDE: 'filespec'
```

Pastes the text of another file where the directive is, to share code between programs (pseudo
libraries). It is not a module system: the file's lines are simply inserted, as if they had been
typed there. Both the QuickBASIC form (`'$INCLUDE: 'FILE.BI'`, also with `REM`) and the simpler
`'$INCLUDE "FILE.BI"` are accepted. The directive must be a comment at the start of a line.

- Included files can contain module-level code, `TYPE`, `CONST`, `DECLARE`, `DATA` and `SUB` /
  `FUNCTION` procedures (they become procedures of the program, although they don't appear in the
  F2 list), and they can include other files, up to 16 levels deep.
- The name is relative to the file that contains the directive; DOS-style paths
  (`'$INCLUDE "LIB\GFX.BI"`) work on every system.
- Module-level code of an included file runs where the directive is. A library whose module code
  has labels for `GOSUB` or `ON ... GOSUB` is usually included after `END`.
- Errors inside included text are reported at the `$INCLUDE` line with the file and line:
  *Division by zero (LIB\MATH.BI, line 9) in main module, line 1*. The debugger doesn't step
  into included code.
- *File > Make EXE File* puts the included text inside the executable: the `.BI` files are not
  needed to run it.

```basic
' MAIN.BAS
'$INCLUDE "LIB\MATH.BI"
PRINT Area!(2)

' LIB\MATH.BI
'$INCLUDE: 'CONST.BI'          ' relative to LIB\, defines PI
FUNCTION Area! (r!)
    Area! = PI * r! * r!
END FUNCTION
```

### MULTIKEY

```
MULTIKEY(keycode%)
```

Returns `-1` (true) while the key with that **scan code** is held down, and `0` if it isn't. Unlike
`INKEY$`, it doesn't consume keystrokes from the buffer and lets you check several keys at once, which is
what a game needs.

```basic
DO
    IF MULTIKEY(75) THEN x = x - 2          ' left arrow
    IF MULTIKEY(77) THEN x = x + 2          ' right arrow
    IF MULTIKEY(57) THEN GOSUB Fire         ' space (while moving)
LOOP UNTIL MULTIKEY(1)                      ' Esc
```

The codes are in the [key code table](#key-code-table).

---

### MOUSE

```
MOUSE x%, y%, buttons%
MOUSE ON | OFF
```

Reads the mouse and stores the result in three numeric variables.

- **`x%`, `y%`**: pointer position on the visible screen, **in the resolution of the current mode**:

  | Mode | x | y |
  |---|---|---|
  | SCREEN 13, 1, 7 | 0 – 319 | 0 – 199 |
  | SCREEN 3 | 0 – 159 | 0 – 199 |
  | SCREEN 2, 8 | 0 – 639 | 0 – 199 |
  | SCREEN 9, 10 | 0 – 639 | 0 – 349 |
  | SCREEN 11, 12 | 0 – 639 | 0 – 479 |
  | SCREEN 0 | column (from 0) | row (from 0) |

  They are **`-1, -1`** when the pointer is outside the picture (for example, over the black bars in
  fullscreen) or outside the window. Coordinates are absolute: `VIEW` and `WINDOW` are not applied.
- **`buttons%`**: bit 0 = left button, bit 1 = right button. It is `1` with the left button down,
  `2` with the right one, `3` with both and `0` with none.

The conversion takes window scaling into account and works the same with the CRT effect on or off.

```
MOUSE ON
MOUSE OFF
```

Show or hide the **mouse cursor**. Like the DOS mouse driver, QBLike draws its own cursor on the emulated
screen (the system pointer is hidden over the window): a white arrow with a black outline in graphics
modes and a block with inverted colors in SCREEN 0. It moves in the mode's resolution, looks the same in
a window and in fullscreen, and follows the CRT effect.

- It is **hidden by default** when each program starts: use `MOUSE ON` to show it. With the cursor
  hidden, `MOUSE x%, y%, b%` still reads the position and buttons (useful when the game draws its own
  pointer).
- It is overlaid on the displayed picture only: **it is not part of video memory**, so `POINT`, `GET`
  and `PEEK` never see it, and there's no need to hide it before drawing (unlike in DOS).
- The arrow uses the colors closest to white and black in the current palette.
- The editor always shows the inverted block, like QBasic.

```basic
SCREEN 13
MOUSE ON                                            ' show the cursor
DO
    MOUSE x%, y%, b%
    IF x% >= 0 THEN
        IF b% AND 1 THEN PSET (x%, y%), 15          ' left: draw
        IF b% AND 2 THEN CIRCLE (x%, y%), 5, 0      ' right: erase
    END IF
LOOP UNTIL INKEY$ = CHR$(27)
```

Since `MOUSE` reads the current state, detecting a click (press and release) needs a comparison with the
previous value:

```basic
MOUSE x%, y%, b%
IF (b% AND 1) AND NOT (before% AND 1) THEN PRINT "click at"; x%; y%
before% = b%
```

---

### PUT ... MASK

```
PUT [STEP](x!, y!), arrayname[(index%)], MASK
```

A new `PUT` mode, alongside `PSET`, `PRESET`, `AND`, `OR` and `XOR`. It copies the image **skipping the
pixels of color 0**, which become transparent. Use it to draw sprites over a background without erasing it.

```basic
SCREEN 13
DIM sprite%(130)
CIRCLE (8, 8), 7, 14: PAINT (8, 8), 14     ' draw something on background 0
GET (0, 0)-(15, 15), sprite%
CLS
LINE (0, 0)-(319, 199), 1, BF              ' blue background
PUT (100, 80), sprite%(0), MASK            ' the circle, without the black box
```

Like every `PUT` in QBLike, it is clipped to the [CLIP](#clip) area if the image goes outside it.

---

### CLIP

```
CLIP x1%, y1%, x2%, y2%
CLIP
```

Sets the clipping rectangle for `PUT`, in **absolute screen coordinates**: unlike `VIEW`, it doesn't
move the origin, so `PUT (0, 0)` is still the top-left corner of the screen.

- By default the rectangle is the whole screen. It goes back to that with `CLIP` without arguments and
  with every `SCREEN` that sets a mode (a `SCREEN ,,1,0` that only changes pages doesn't affect it).
- The corners can be given in any order. Anything outside the screen is adjusted to it.
- It needs a graphics mode; in `SCREEN 0` it raises *Illegal function call*.

**Clipped PUT.** In QBLike, `PUT` never raises an error for going off the screen: it draws only the part of
the image inside the `CLIP` rectangle (and the `VIEW` viewport, if any). With negative coordinates the
sprite leaves through the top or the left; near `x2`/`y2`, through the right or the bottom; and if it is
completely outside, nothing is drawn. It works with every `PUT` mode (`PSET`, `PRESET`, `AND`, `OR`,
`XOR` and `MASK`).

`CLIP` also clips `LOADBMP`/`LOADGIF ..., SCREEN`. It doesn't affect `PSET`, `LINE`, `CIRCLE` and the
rest, which are still clipped only to the `VIEW` viewport, as in QBasic.

```basic
SCREEN 13
LINE (0, 0)-(319, 19), 1, BF               ' score bar
CLIP 0, 20, 319, 199                       ' sprites don't draw over it
FOR x = -16 TO 320                         ' enters on the left and leaves on the right
    WAIT &H3DA, 8
    LINE (0, 20)-(319, 199), 0, BF
    PUT (x, 10), sprite%(0), MASK          ' the top part stays hidden behind the bar
NEXT
```

---

### GET / PUT with image buffers and addresses

```
GET [surface,] [STEP](x1!, y1!)-[STEP](x2!, y2!), data
PUT [surface,] [STEP](x!, y!), data [, actionverb]

surface, data:  arrayname[(index%)]  |  SEG segment%, offset%
```

Two additions to the graphic `GET` and `PUT`, for **SCREEN 13**:

- **Image buffers (`surface`).** An image stored in an array in the `GET` format (a header with
  `width * 8` and `height`, then one byte per pixel) can be used as an off-screen drawing surface: `PUT`
  draws into it and `GET` reads from it, with coordinates in pixels of that image. Drawing into a buffer is
  clipped to the buffer, never to `CLIP` or `VIEW`. A whole frame can be composed in a buffer and shown
  with a single `PUT`.
- **Addresses (`SEG segment%, offset%`).** Instead of an array name, the image (or the surface) can be
  given by its address, as returned by `VARSEG` and `VARPTR` (see
  [Arrays in memory](#arrays-in-memory-varseg--varptr)). This is how libraries that receive addresses, like
  DirectQB, call `PUT`: `PUT (x, y), SEG s%, o%, MASK`.

Every action verb works with both forms (`PSET`, `PRESET`, `AND`, `OR`, `XOR`, `MASK`). An image that does
not fit in its array raises *Illegal function call*; in other screen modes, using a surface raises
*Illegal function call* too.

```basic
SCREEN 13
DIM buf%(32001)                           ' 4 + 320 * 200 bytes
buf%(0) = 320 * 8: buf%(1) = 200          ' header: width * 8, height
DIM ship%(129): GET (0, 0)-(15, 15), ship%
DO
    PUT buf%(0), (0, 0), back%(0), PSET   ' background into the buffer
    PUT buf%(0), (x%, y%), ship%(0), MASK ' sprites into the buffer
    WAIT VSYNC
    PUT (0, 0), buf%(0), PSET             ' the whole frame at once
LOOP UNTIL MULTIKEY(1)

GET buf%(0), (16, 0)-(31, 15), frame%     ' a part of the buffer
PUT (10, 10), SEG VARSEG(ship%(0)), VARPTR(ship%(0)), MASK
```

---

### FILL

```
FILL [STEP](x!, y!)[, color%]
```

Fills with `color%` the area of pixels that have the same color as the point `(x, y)` and touch it
horizontally or vertically, whatever the color around it. Unlike `PAINT`, which stops at a single border color,
`FILL` works for areas bounded by lines of several colors and to recolor a whole shape (its outline, for
example).

- Without `color%` the foreground color is used.
- The coordinates follow `WINDOW` and `VIEW` (and `STEP`, relative to the last point), and the fill stays
  inside the `VIEW` viewport.
- If `(x, y)` already has `color%`, or is outside the viewport, nothing happens.
- It uses no recursion and has no size limit: filling the whole screen is instant.
- A graphics mode is required; in `SCREEN 0` it raises *Illegal function call*.

`FILL` **is not a reserved word**: programs that have their own `SUB`, `FUNCTION`, variable or array called
`FILL` keep working the same.

```basic
SCREEN 13
LINE (10, 10)-(60, 10), 4: LINE (60, 10)-(60, 60), 9   ' border of several colors
LINE (60, 60)-(10, 60), 4: LINE (10, 60)-(10, 10), 2
FILL (30, 30), 14                                       ' the interior
FILL (20, 10), 12                                       ' only the top line (color 4)
```

---

### MUSIC

```
MUSIC filespec$[, LOOP]
MUSIC STOP
MUSIC VOLUME volume%
playing% = MUSIC
```

Plays background music while the program goes on.

- `MUSIC file$` plays a **MOD** (ProTracker, 4 channels) or **MID** (General MIDI, played on the emulated
  OPL FM synthesizer, the sound of a Sound Blaster) file. The format is detected from the contents, not
  the extension. With `LOOP` the music starts again when it ends; without it, it plays once.
- A new `MUSIC` replaces the music that is playing. `MUSIC STOP` stops it.
- `MUSIC VOLUME v%` sets the volume, 0-255 (255 at program start). It can be changed while playing,
  for fades.
- `MUSIC` used as a function returns -1 (true) while music is playing and 0 when it has ended or was stopped.
- If the file does not exist: *File not found*. If it is not a valid MOD or MID: *Illegal function call*.
- Music stops when the program ends.

`MUSIC` **is not a reserved word**: programs with their own variable, array or `SUB` called `MUSIC`
keep working (if a program uses a variable `MUSIC`, the name refers to the variable).

```basic
MUSIC "TITLE.MOD", LOOP
DO: LOOP WHILE INKEY$ = ""
FOR v% = 255 TO 0 STEP -5: MUSIC VOLUME v%: WAIT VSYNC: NEXT    ' fade out
MUSIC "START.MID"
DO: LOOP WHILE MUSIC                                            ' wait for it to end
```

### SOUND (sound effects)

```
id% = SOUND filespec$
SOUND PLAY id%[, channel%][, volume%][, pan%][, rate&][, LOOP]
SOUND STOP [channel%]
playing% = SOUND(channel%)
```

Sound effects from WAV files, mixed with the music and with `PLAY`. QBasic's `SOUND frequency, duration`
keeps working as always.

- `id% = SOUND file$` loads a WAV file (mono or stereo, 4000-48000 Hz, 8 or 16 bits) and returns its
  number. Loading the same file again returns the same number, so it can be done inside a loop.
- `SOUND PLAY` plays it on one of **12 channels** (1-12). Without `channel%` it uses a free one (or, if all
  are busy, the one that has been playing the longest). Playing on a busy channel replaces what was playing there.
- `volume%` 0-255 (default 255); `pan%` from -100 (left) to 100 (right), 0 centered (default).
  Empty arguments keep their default: `SOUND PLAY boom%, , , -80`.
- `rate&` plays the sound as if it had been recorded at that sample rate (1000-96000 Hz): an 11025 Hz
  sound played at 22050 sounds an octave higher and twice as fast. Without it, the file's own rate is used.
- With `LOOP` the sound repeats until `SOUND STOP` (an engine, rain...).
- `SOUND STOP channel%` stops one channel; `SOUND STOP` without a number stops all 12.
- `SOUND(channel%)` returns -1 (true) while that channel is playing and 0 when it is free; `SOUND(0)`
  tells whether any channel is playing.
- Wrong file: *File not found* / *Illegal function call*; channel, volume or pan out of range or a
  number not returned by `SOUND file$`: *Illegal function call*.

```basic
laser% = SOUND "LASER.WAV"
motor% = SOUND "MOTOR.WAV"
SOUND PLAY motor%, 1, 120, , LOOP          ' channel 1: engine, all the time
DO
    IF MULTIKEY(57) THEN SOUND PLAY laser%, , , (x% - 160) * 100 \ 160   ' from where the ship is
    ...
LOOP UNTIL MULTIKEY(1)
SOUND STOP
```

### LOADBMP

```
LOADBMP file$, arrayname[(index%)]
LOADBMP file$, SCREEN, x!, y! [, PALETTE]
```

Loads an **indexed BMP** image with 2, 16 or 256 colors (1, 4 or 8 bits per pixel). Uncompressed, RLE4
and RLE8 BMPs are accepted, with or without a palette (also the old OS/2 format).

A graphics mode is required; in `SCREEN 0` it raises *Illegal function call*.

**Array form.** Stores the image in the array starting at `index%`, with the same header and pixel
format as `GET` in the current screen mode, so you can then just `PUT (x, y), array(index)`.

- The array must be numeric (INTEGER, LONG, SINGLE or DOUBLE).
- If there isn't enough room, it raises *Illegal function call*, like `GET`.
- If the image has more colors than the current mode, the indexes are reduced to the mode's bits.

**SCREEN form.** Draws the image on the screen with its top-left corner at `(x, y)`.

- The image must be representable in the current mode. Otherwise it raises *Illegal function call*:

  | Image | SCREEN 13 | SCREEN 3, 7, 8, 9, 12 | SCREEN 1 | SCREEN 2, 11 |
  |---|---|---|---|---|
  | 2 colors | yes | yes | yes | yes |
  | 16 colors | yes | yes | no | no |
  | 256 colors | yes | no | no | no |

- The coordinates follow `WINDOW` and `VIEW` like `PUT`, and the part outside the graphics viewport or
  the [CLIP](#clip) area is clipped.
- With `PALETTE` the file's palette is applied too (the first 2, 16 or 256 colors, depending on the mode).

**Size needed in the array**, in bytes:

| Mode | Bytes |
|---|---|
| SCREEN 13 (256 colors) | `4 + width * height` |
| SCREEN 3, 7, 8, 9, 12 (16 colors) | `4 + height * 4 * ((width + 7) \ 8)` |
| SCREEN 1 (4 colors) | `4 + height * ((width + 3) \ 4)` |
| SCREEN 2, 11 (2 colors) | `4 + height * ((width + 7) \ 8)` |

To get elements, divide by the element size (INTEGER 2, LONG and SINGLE 4, DOUBLE 8), rounding up.

```basic
SCREEN 13
LOADBMP "back.bmp", SCREEN, 0, 0, PALETTE
DIM ship%((4 + 16 * 16 + 1) \ 2)
LOADBMP "ship.bmp", ship%(0)
PUT (150, 170), ship%(0), MASK
```

---

### LOADGIF

```
LOADGIF file$, arrayname[(index%)]
LOADGIF file$, SCREEN, x!, y! [, PALETTE]
```

Same as [LOADBMP](#loadbmp), but with **GIF** files (interlaced too). To check whether the image fits the
mode, the number of colors is the size of the GIF palette (2, 4, 8… 256).

**Animated GIFs.** In the array form, each frame is composed on the GIF's logical canvas (honoring each
frame's transparency and disposal method) and stored **right after the previous one**. All frames have
the same size, the canvas size, so each one takes:

```
elements_per_frame = (image_bytes + element_size - 1) \ element_size
```

In SCREEN 13 with an INTEGER array: `(4 + width * height + 1) \ 2`.

The `SCREEN` form draws only the first frame.

```basic
SCREEN 13
CONST WIDE = 32, HIGH = 32, FRAMES = 8
size = (4 + WIDE * HIGH + 1) \ 2
DIM explosion%(size * FRAMES)
LOADGIF "explosion.gif", explosion%(0)
FOR f = 0 TO FRAMES - 1
    WAIT &H3DA, 8
    PUT (144, 84), explosion%(f * size), PSET
NEXT
```

Pixels keep the color indexes from the file. If each GIF frame has its own local palette, the indexes
may mean different colors in each one.

---

### LOADPAL

```
LOADPAL file$, arrayname&[(index%)]
```

Loads **only the palette** of a BMP or GIF (the format is detected from the contents) into an array,
starting at `index%`. It stores one element per color with the same format used by `PALETTE USING` in
SCREEN 12 and 13:

```
value = 65536 * blue + 256 * green + red        ' components from 0 to 63
```

- The array must be **LONG** (SINGLE or DOUBLE also work), as for `PALETTE USING`; an INTEGER array
  raises *Type mismatch*, because the values don't fit.
- If there's no room for all the colors, it raises *Illegal function call*.
- If the file has no palette, it raises *Illegal function call*.

```basic
SCREEN 13
DIM pal&(255)
LOADPAL "sunset.gif", pal&(0)
PALETTE USING pal&(0)

' fade to black
FOR stp = 63 TO 0 STEP -1
    WAIT &H3DA, 8
    OUT &H3C8, 0
    FOR i = 0 TO 255
        r = pal&(i) AND 63: g = (pal&(i) \ 256) AND 63: b = (pal&(i) \ 65536) AND 63
        OUT &H3C9, r * stp \ 63: OUT &H3C9, g * stp \ 63: OUT &H3C9, b * stp \ 63
    NEXT
NEXT
```

---

### SCREEN 3 (PCjr / Tandy 1000)

```
SCREEN 3
```

The 160×200, 16-color mode of the IBM PCjr and Tandy 1000 (it didn't exist in QBasic, but it did in some
versions of GW-BASIC). Pixels are twice as wide as they are tall, and text is 20×25.

- Colors: the 16 of the IBM IRGB palette, as in SCREEN 7. `COLOR foreground, background` and
  `PALETTE attribute, color` (color 0-15) work as in SCREEN 7.
- 2 pages (`SCREEN 3, , 1, 0`, `PCOPY`).
- `GET`/`PUT` use the same array format as the other 16-color modes; `LOADBMP`/`LOADGIF` accept images
  with 2 or 16 colors.
- `MOUSE` returns `x` from 0 to 159.
- Video memory at `&HB800`, as on the original hardware: each byte holds two 4-bit pixels (the high
  nibble is the left one) with their color directly, without artifacts. Even lines start at offset 0 and
  odd lines at `&H2000` (8192), 80 bytes per line. A full screen takes 16384 bytes and can be saved and
  loaded with `BSAVE`/`BLOAD`.

```basic
SCREEN 3
FOR i = 0 TO 15: LINE (i * 10, 0)-(i * 10 + 9, 120), i, BF: NEXT
DEF SEG = &HB800
POKE 8192 + 40 * 80 + 30, &H9A     ' line 81: pixel 60 = color 9, pixel 61 = color 10
BSAVE "TANDY.PIC", 0, 16384
DEF SEG
```

### SCREEN FULL / SCREEN WINDOW

```
SCREEN FULL
SCREEN WINDOW
```

Switch QBLike to full screen or to a window from the program, the same as pressing Alt+Enter or F11.
The screen mode, its contents and the palette don't change, and the program goes on at once. The
state stays as it is when the program ends; the user can always change it with Alt+Enter.

`FULL` is not a reserved word: in a program with a variable called `FULL`, `SCREEN full` still sets
the screen mode stored in that variable.

```basic
SCREEN 13
SCREEN FULL                ' the game in full screen
...
SCREEN WINDOW              ' back to a window before showing the scores
```

### DIR$, CURDIR$ and SETMEM (QuickBASIC functions)

```
f$ = DIR$[(filespec$[, attributes%])]
d$ = CURDIR$[(drive$)]
size& = SETMEM(numeric-expression&)
```

Functions from QuickBASIC 4.5 / PDS 7.1 that many programs of the time used:

- `DIR$(spec$)` returns the first file that matches `spec$` (wildcards `*` and `?`, with or without a
  path); `DIR$` without arguments returns the next one, and `""` when there are no more. With
  `attributes%` `&H10` directories are listed too. `.` and `..` are never returned.
- `CURDIR$` returns the current directory as a native path (`C:\GAMES` on Windows, `/home/user/games` on
  Linux). The drive argument is accepted and ignored.
- `SETMEM(n&)` enlarged or reduced the far heap to make room for assembly libraries. QBLike has no such
  limit: it only keeps the count (393216 bytes at start) and returns it, so `dummy& = SETMEM(-66000)`
  works as before.

None of them is a reserved word: a program with its own variable, array or `FUNCTION` with one of these
names keeps using it.

```basic
f$ = DIR$("*.BAS")
DO WHILE LEN(f$)
    PRINT f$
    f$ = DIR$
LOOP
PRINT "in "; CURDIR$
```

---

## Emulated hardware

These tricks already existed in QBasic on a real PC. QBLike emulates them so old programs and new games
work the same.

### Video memory (PEEK / POKE)

| Segment (`DEF SEG =`) | Mode | Contents |
|---|---|---|
| `&HA000` | SCREEN 13 | linear, one byte per pixel; offset = `y * 320 + x` (64000 bytes), on the active page |
| `&HA000` | SCREEN 7, 8, 9, 12 | planar EGA/VGA: 4 bit planes (blue, green, red, intensity); each byte is 8 consecutive pixels of one plane; offset = `y * (width \ 8) + x \ 8` |
| `&HB800` | SCREEN 1, 2, 3 | CGA: even lines from 0 and odd lines from `&H2000`, 80 bytes per line (4 pixels per byte in SCREEN 1, 8 in SCREEN 2, 2 in SCREEN 3) |
| `&HB800` | SCREEN 0 | character/attribute pairs; offset = `(row * columns + column) * 2`; each page takes 4096 bytes in 80×25 (2048 in 40×25) |

```basic
SCREEN 13
DEF SEG = &HA000
POKE 100 * 320& + 160, 15        ' white pixel at (160, 100)
c = PEEK(100 * 320& + 160)
DEF SEG
```

#### Planar modes (SCREEN 7, 8, 9, 12)

As on an EGA/VGA card, each memory location corresponds to 8 pixels in four planes, one per color bit.
Which planes are written and which one is read is chosen with the card's registers:

| Ports | Register | Use |
|---|---|---|
| `OUT &H3C4, 2: OUT &H3C5, m` | sequencer *map mask* | `POKE`/`BLOAD` write to the planes whose bit is set in `m` (1 blue, 2 green, 4 red, 8 intensity); default `15`, all of them |
| `OUT &H3CE, 4: OUT &H3CF, p` | *read map select* | `PEEK`/`BSAVE` read plane `p` (0-3); default 0 |

Each plane takes `width * height / 8` bytes: 8000 in SCREEN 7, 16000 in SCREEN 8, 28000 in SCREEN 9 and
38400 in SCREEN 12. Pages follow the first one every 8 KB (SCREEN 7), 16 KB (SCREEN 8) or 32 KB
(SCREEN 9); the offset selects the page, not `SCREEN ,,apage`. The registers go back to their defaults
with every `SCREEN`.

### Arrays in memory (VARSEG / VARPTR)

QBLike gives every numeric array an address in the emulated conventional memory the first time `VARSEG`
or `VARPTR` is used on one of its elements. From then on `PEEK`, `POKE`, `BLOAD`, `BSAVE` and
`GET`/`PUT ... SEG` see the bytes of the array at that address, exactly like in QBasic:

```basic
DIM spr%(129)
DEF SEG = VARSEG(spr%(0))
o& = VARPTR(spr%(0))
POKE o& + 4, 15                  ' first pixel of a GET image
PRINT PEEK(o&), PEEK(o& + 1)     ' low and high byte of spr%(0)
DEF SEG
```

- Addresses are in the ranges `&H1000`-`&H9FFF` and `&HC000`-`&HEFFF` (segments), aligned to 16 bytes,
  and they don't change while the array exists. `VARSEG` and `VARPTR` return INTEGER values, as in QBasic
  (offsets over 32767 are negative); `PEEK`/`POKE` accept them either way.
- An address that doesn't belong to any array (outside the video, BIOS and array areas) raises
  *Illegal function call* in `PUT ... SEG`/`GET ... SEG`; `PEEK` returns 0 and `POKE` does nothing there.
- `REDIM` and `ERASE` of a dynamic array free its address; strings and simple variables have no address
  (`VARSEG`/`VARPTR` return 0).

### SCREEN 13 pages

In QBLike `SCREEN 13` has **64 pages** (QBasic had one), used as in SCREEN 7: `SCREEN 13, , apage, vpage`
or, keeping the mode, `SCREEN , , apage` to draw on a hidden page, and `PCOPY` to copy pages. `PEEK`/`POKE`
on `&HA000` and every drawing statement work on the active page. Pages take memory only when used.

```basic
SCREEN 13, , 1, 0                ' draw on page 1, show page 0
DO
    LINE (0, 0)-(319, 199), 0, BF
    ... draw the frame ...
    WAIT VSYNC
    PCOPY 1, 0
LOOP UNTIL MULTIKEY(1)
```

### Vertical sync (INP &H3DA, WAIT, WAIT VSYNC)

`INP(&H3DA)` returns the VGA status register. Bit 3 (value 8) is set during the vertical retrace, which
is emulated at 70 Hz as on a real VGA. At the start of each retrace, whatever has been drawn so far is
sent to the window, so syncing with it avoids flicker and tearing.

```basic
WHILE (INP(&H3DA) AND 8) <> 0: WEND     ' wait for the current retrace to end
WHILE (INP(&H3DA) AND 8) = 0: WEND      ' wait for the next one to start
' ...draw here...
```

`WAIT &H3DA, 8` does the same in a single statement, and QBLike adds a more readable alias:

```basic
WAIT VSYNC                              ' = WAIT &H3DA, 8
```

`VSYNC` is not a reserved word: programs that have their own `SUB VSync` keep working. The three forms (`INP`,
`WAIT &H3DA` and `WAIT VSYNC`) use the same 70 Hz clock, so they can be mixed. While waiting, the program
doesn't use CPU unnecessarily.

### VGA palette (OUT / INP &H3C7-&H3C9)

| Port | Use |
|---|---|
| `OUT &H3C8, i` | selects color `i` for writing |
| `OUT &H3C9, v` | writes red, green and blue (0-63) one after another; then moves on to the next color |
| `OUT &H3C7, i` | selects color `i` for reading |
| `INP(&H3C9)` | reads red, green and blue one after another |

```basic
OUT &H3C8, 0
OUT &H3C9, 10: OUT &H3C9, 20: OUT &H3C9, 50     ' color 0 = dark blue
```

It works in all graphics modes; in 16-color modes it acts on colors 0-15.

### CGA composite: artifact colors (OUT &H3D8)

On a CGA connected to an NTSC composite monitor or TV, the black-and-white 640×200 mode could show colors:
when the *color burst* signal was turned on, the monitor read the patterns of white and black pixels as
colors. Each group of 4 pixels was seen as a single wide pixel of one of 16 colors, for an effective
resolution of 160×200. QBLike emulates it:

```basic
SCREEN 2            ' 640x200, burst off (register = &H1E)
OUT &H3D8, 26       ' &H1A: burst on, artifact colors
OUT &H3D8, 30       ' &H1E: back to black and white
```

The CGA mode register (`&H3D8`) is emulated in SCREEN 2: with bit 2 cleared, colors are turned on. It
goes back to its default when the mode is changed with `SCREEN`. Only the displayed picture changes:
`POINT`, `GET`, `PEEK` and `BSAVE` still see pixels of 0 and 1. Each group starts at a column that is a
multiple of 4, and the first pixel is the most significant bit:

| Pattern | Value | Color | RGB |
|---|---|---|---|
| 0000 | &H0 | black | #000000 |
| 0001 | &H1 | dark green | #006E2D |
| 0010 | &H2 | blue | #2D02FF |
| 0011 | &H3 | light blue | #008BFF |
| 0100 | &H4 | dark red | #A9002D |
| 0101 | &H5 | medium gray | #777677 |
| 0110 | &H6 | magenta | #EC09FF |
| 0111 | &H7 | light violet | #BB92FD |
| 1000 | &H8 | brown | #2D5A00 |
| 1001 | &H9 | bright green | #00DC00 |
| 1010 | &HA | light gray | #767777 |
| 1011 | &HB | aqua | #45F4B9 |
| 1100 | &HC | orange | #EA6502 |
| 1101 | &HD | yellow | #BCE500 |
| 1110 | &HE | pink | #FF80BC |
| 1111 | &HF | white | #FFFFFF |

To paint in color you draw the patterns. For example, a rectangle of color `c` (0-15):

```basic
SCREEN 2: OUT &H3D8, 26
c = 12                                   ' orange
FOR x = 160 TO 319
    IF c AND 2 ^ (3 - (x MOD 4)) THEN LINE (x, 50)-(x, 149), 1
NEXT
```

With `DEF SEG = &HB800` and `POKE` it's even more direct: each byte is two color pixels (high nibble on
the left), so `POKE offset, &HC9` paints an orange pixel and a bright green one.

Without burst, `OUT &H3D9, color` (0-15) changes the color of the lit pixels in SCREEN 2, as on the CGA.

### Keyboard (INP &H60, PEEK &H417)

- `INP(&H60)` returns the code of the last key pressed; when it is released, the same code + 128.
- With `DEF SEG = 0`, `PEEK(&H417)` returns the modifier state: bit 0 right Shift, bit 1 left Shift,
  bit 2 Ctrl, bit 3 Alt.

To read several keys at once, [MULTIKEY](#multikey) is more convenient.

### AdLib / Sound Blaster FM (OUT / INP &H388)

QBLike emulates the Yamaha OPL FM synthesizer of AdLib and Sound Blaster cards (through dos-like's
OPL3 emulator), so programs that played FM music by writing to the card's ports work unchanged,
for example Bisqwit's S3M player for QuickBASIC (`fm.inc`).

| Ports | Use |
|---|---|
| `OUT &H388, reg` / `OUT &H389, value` | select an OPL register and write it (also `&H220/&H221` and `&H228/&H229`) |
| `OUT &H38A, reg` / `OUT &H38B, value` | second OPL3 register bank (also `&H222/&H223`) |
| `INP(&H388)` | status register: the two timers (registers 2, 3 and 4) work, so the classic AdLib detection routine finds the card |

```
SUB FMwrite (reg%, value%)
    OUT &H388, reg%: x% = INP(&H388)     ' the INP reads are the delay real cards needed
    OUT &H389, value%: x% = INP(&H388)
END SUB

FMwrite &H20, &H01: FMwrite &H40, &H10: FMwrite &H60, &HF0: FMwrite &H80, &H77
FMwrite &HA0, &H98: FMwrite &HB0, &H31   ' key on: a 440 Hz-ish tone on channel 0
```

Writes are played with a fixed latency of about 80 ms, so the rhythm is kept exactly even if the
program is momentarily busy. The FM voices are silenced when the program ends. The MPU-401 (MIDI) ports
`&H330/&H331` are not emulated: reading them returns `&HFF`, so detection routines report that there is
no MIDI interface instead of hanging.

`examples/ADLIB.BAS` is a complete example (detection, an instrument and a two-voice tune).

These players usually take their timing from `ON PLAY(n) GOSUB` and `PLAY "MB..."` with `VARPTR$`;
QBLike implements all of it as QBasic does (a real background music queue of up to 32 notes).

### BIOS data (PEEK &H46C, &H449...)

With `DEF SEG = 0`, as in a PC BIOS:

| Address | Contents |
|---|---|
| `&H46C`-`&H46F` | clock tick counter since midnight (18.2 per second) |
| `&H417` | keyboard flags (see [Keyboard](#keyboard-inp-h60-peek-h417)) |
| `&H449` | current video mode: 3 for SCREEN 0 (1 in 40 columns), `&H13` for SCREEN 13, `&H12` for SCREEN 12... |
| `&H44A` | text columns |
| `&H462` | visual page |
| `&H484` | text rows minus 1 |

### BLOAD / BSAVE

```
BSAVE file$, offset%, length%
BLOAD file$[, offset%]
BSAVE file$, VARPTR(array(index)), length%
BLOAD file$, VARPTR(array(index))
```

They use the QBasic file format: a `&HFD` byte and three 16-bit (*little-endian*) words with the segment,
offset and length, followed by the data. This way you can load files from old games and programs, and
save files that QBasic also understands.

A modern PC has no segments, so QBLike emulates the two useful cases:

**1. Arrays** (sprites, maps, tables…). If the offset is written as `VARPTR(array(index))`, the data is
saved from the array or loaded into it as is, byte by byte, starting at that element, regardless of
`DEF SEG` or the segment stored in the file. It works with any numeric array (INTEGER, LONG, SINGLE,
DOUBLE, with one or more dimensions); string arrays and `TYPE` arrays raise *Type mismatch*.

- `BLOAD` raises *Illegal function call* if the data doesn't fit in the array from that element
  (like `GET`), and changes nothing.
- `BSAVE` raises *Illegal function call* if asked to save more bytes than the array holds.
- The length is in bytes: 2 per INTEGER, 4 per LONG or SINGLE, 8 per DOUBLE. For a `GET` sprite, it is
  the image size (for example, `4 + width * height` in SCREEN 13).
- `VARPTR(...)` must be written directly as the second argument; if it is part of an expression
  (`VARPTR(a(0)) + 2`) or a variable, it is treated as a normal offset.

```basic
SCREEN 13
DIM ship%(129)                                 ' 16x16 in SCREEN 13: 4 + 256 bytes = 130 INTEGERs
GET (0, 0)-(15, 15), ship%
BSAVE "SHIP.SPR", VARPTR(ship%(0)), 260        ' save the sprite

DIM other%(129)
DEF SEG = VARSEG(other%(0))                    ' classic form; optional in QBLike
BLOAD "SHIP.SPR", VARPTR(other%(0))            ' load it
DEF SEG
PUT (100, 100), other%, MASK
```

Several sprites in a row in the same array are saved or loaded the same way, with the index of the first
one and the sum of their sizes.

**2. Video memory.** Without `VARPTR`, the segment is the `DEF SEG` one when it points to video memory
(`&HA000` or `&HB800`) and an offset is given; otherwise, the one stored in the file. The data goes to
the screen according to the [video memory table](#video-memory-peek--poke):

- **SCREEN 13**, segment `&HA000`: copied to the screen; anything past 64000 bytes is ignored.
- **SCREEN 7, 8, 9, 12**, segment `&HA000`: each file is one bit plane, and it goes to the planes
  selected with `OUT &H3C4, 2: OUT &H3C5, m`. `BSAVE` saves the plane selected with `OUT &H3CE, 4: OUT &H3CF, p`.
- **SCREEN 1, 2, 3**, segment `&HB800`: CGA or PCjr/Tandy memory (16384 bytes, even and odd lines apart).
- **SCREEN 0**, segment `&HB800`: text screen, character/attribute pairs (4000 bytes in 80×25).

Files with any other segment, or with a video segment that doesn't match the current mode, are ignored
without an error.

```basic
SCREEN 13
DEF SEG = &HA000
BSAVE "SCREEN.BIN", 0, 64000       ' &HFA00 works too, even though it is negative as an INTEGER
DEF SEG
BLOAD "SCREEN.BIN"                 ' segment and offset from the file
```

A 16-color EGA screen saved in four files, one per plane:

```basic
SCREEN 7
DEF SEG = &HA000
OUT &H3C4, 2: OUT &H3C5, 1: BLOAD file$ + ".BLU", 0    ' blue plane
OUT &H3C4, 2: OUT &H3C5, 2: BLOAD file$ + ".GRN", 0    ' green
OUT &H3C4, 2: OUT &H3C5, 4: BLOAD file$ + ".RED", 0    ' red
OUT &H3C4, 2: OUT &H3C5, 8: BLOAD file$ + ".INT", 0    ' intensity
OUT &H3C4, 2: OUT &H3C5, 15
DEF SEG
```

And to save it:

```basic
DEF SEG = &HA000
FOR p = 0 TO 3
    OUT &H3CE, 4: OUT &H3CF, p
    BSAVE file$ + "." + MID$("BLUGRNREDINT", p * 3 + 1, 3), 0, 8000
NEXT
DEF SEG
```

---

## DirectQB library

The `lib/directqb` folder contains `DIRECTQB.BI`, a compatibility layer for **DirectQB 1.61**, the assembly
game library for QuickBASIC by Angelo Mottola (Enhanced Creations, 1998-99). It implements every SUB and
FUNCTION of DirectQB in plain QBLike BASIC on top of the extensions above (SCREEN 13 pages as layers,
`GET`/`PUT ... SEG`, `VARSEG`/`VARPTR`, `MULTIKEY`, `MOUSE`, `SOUND`), so old games run by keeping their
`'$INCLUDE: 'DIRECTQB.BI'` line. See [lib/directqb/README.md](lib/directqb/README.md).

---

## Key code table

PC keyboard scan codes, valid for `MULTIKEY` and `INP(&H60)`. They correspond to the physical position of
the key on a US keyboard; on a Spanish keyboard, for example, `Ñ` is key 39.

| Key | Code | Key | Code | Key | Code |
|---|---|---|---|---|---|
| Esc | 1 | 1 … 9, 0 | 2 … 11 | Backspace | 14 |
| Tab | 15 | Q W E R T Y U I O P | 16 … 25 | Enter | 28 |
| Ctrl | 29 | A S D F G H J K L | 30 … 38 | Left Shift | 42 |
| Z X C V B N M | 44 … 50 | Right Shift | 54 | Alt | 56 |
| Space | 57 | Caps Lock | 58 | F1 … F10 | 59 … 68 |
| F11 | 87 | F12 | 88 | Home | 71 |
| ↑ | 72 | PgUp | 73 | ← | 75 |
| → | 77 | End | 79 | ↓ | 80 |
| PgDn | 81 | Insert | 82 | Delete | 83 |

---

## Complete example

The `examples` folder includes `NAVES.BAS`, a small game that uses `MULTIKEY`, the retrace wait with
`INP(&H3DA)`, `PUT ... MASK`, the palette through ports and `SOUND`.

```basic
' Skeleton of a SCREEN 13 game with the QBLike extensions
DEFINT A-Z
SCREEN 13
LOADBMP "back.bmp", SCREEN, 0, 0, PALETTE
DIM back(32002)
GET (0, 0)-(319, 199), back                  ' keep the whole background
DIM ship((4 + 16 * 16 + 1) \ 2)
LOADBMP "ship.bmp", ship(0)
x = 152: y = 170
DO
    IF MULTIKEY(75) AND x > 0 THEN x = x - 2
    IF MULTIKEY(77) AND x < 304 THEN x = x + 2
    WAIT &H3DA, 8
    PUT (0, 0), back, PSET
    PUT (x, y), ship(0), MASK
LOOP UNTIL MULTIKEY(1)
```
