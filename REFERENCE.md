# QBLike language reference

Complete reference of the language accepted by QBLike: every statement, function and operator of QBasic 1.1, with the behavior of QBLike where it differs, plus the QBLike extensions. It is the same text as the built-in help of the editor (F1), generated from it with `tools/help2md.py`.

For a guided description of the extensions and the emulated hardware, see [EXTENSIONES_en.md](EXTENSIONES_en.md) ([EXTENSIONES_es.md](EXTENSIONES_es.md) in Spanish).

## Contents

- [QBLike compared with QBasic](#qblike-compared-with-qbasic)
- [Keywords by task](#keywords-by-task)
- [Index](#index)
- [Language topics](#language-topics): [Data Types](#topic-data-types), [Operators](#topic-operators), [Errors](#topic-errors), [Game Programming](#topic-game-programming)
- [Keyword reference](#keyword-reference)
- [The editor](#the-editor): [Editing Keys](#topic-editing-keys), [Debugging](#topic-debugging), [Menus](#topic-menus), [Using Help](#topic-using-help)

## QBLike compared with QBasic

Programs written for QBasic 1.1 run unchanged. The differences are:

- **Extensions** (new statements and functions): [CLIP](#kw-clip), [FILL](#kw-fill), [LOADBMP](#kw-loadbmp), [LOADGIF](#kw-loadgif), [LOADPAL](#kw-loadpal), [MOUSE](#kw-mouse), [MULTIKEY](#kw-multikey), [MUSIC](#kw-music); also [PUT](#kw-put) `MASK`, [WAIT](#kw-wait) `VSYNC`, [SOUND](#kw-sound) `file$` / `SOUND PLAY` / `SOUND STOP`, `SCREEN FULL` / `SCREEN WINDOW` and `SCREEN 3` (PCjr/Tandy), see [SCREEN](#kw-screen).
- **Emulated hardware**: video memory ([DEF SEG](#kw-def-seg), [PEEK](#kw-peek), [POKE](#kw-poke)), VGA/CGA ports and the AdLib FM synthesizer ([INP](#kw-inp), [OUT](#kw-out)), vertical sync ([WAIT](#kw-wait)), BIOS clock and keyboard flags.
- **No memory limits**: strings up to 1 GB, arrays up to 64 million elements with LONG subscripts, about 100000 levels of SUB/FUNCTION recursion (see [FRE](#kw-fre)).
- **Behavior changes**: [PUT](#kw-put) clips images that go off the screen instead of failing; [LEN](#kw-len) and [INSTR](#kw-instr) return LONG.
- **Not available**: [CHAIN](#kw-chain), `CALL ABSOLUTE`/`INTERRUPT`, [RUN](#kw-run) with a file or line, printing ([LPRINT](#kw-lprint) output is discarded), serial ports, light pen and joystick ([COM](#kw-com), [PEN](#kw-pen), [STICK](#kw-stick)), and [KEY](#kw-key) events. They are accepted where possible so old programs still run.

## Keywords by task

| Task | Keywords |
|---|---|
| Program flow | [IF](#kw-if) · [SELECT](#kw-select) · [FOR](#kw-for) · [DO](#kw-do) · [WHILE](#kw-while) · [GOSUB](#kw-gosub) · [GOTO](#kw-goto) · [SUB](#kw-sub) · [FUNCTION](#kw-function) · [CALL](#kw-call) · [EXIT](#kw-exit) · [END](#kw-end) · [ON ERROR](#kw-on-error) |
| Variables | [DIM](#kw-dim) · [REDIM](#kw-redim) · [CONST](#kw-const) · [TYPE](#kw-type) · [SHARED](#kw-shared) · [STATIC](#kw-static) · [DEFINT](#kw-defint) |
| Text screen | [PRINT](#kw-print) · [PRINT USING](#kw-print-using) · [INPUT](#kw-input) · [LINE INPUT](#kw-line-input) · [LOCATE](#kw-locate) · [COLOR](#kw-color) · [CLS](#kw-cls) |
| Graphics | [SCREEN](#kw-screen) · [PSET](#kw-pset) · [LINE](#kw-line) · [CIRCLE](#kw-circle) · [PAINT](#kw-paint) · [DRAW](#kw-draw) · [GET](#kw-get) · [PUT](#kw-put) · [PALETTE](#kw-palette) · [VIEW](#kw-view) · [WINDOW](#kw-window) · [PCOPY](#kw-pcopy) · [POINT](#kw-point) · [LOADBMP](#kw-loadbmp) · [LOADGIF](#kw-loadgif) · [LOADPAL](#kw-loadpal) · [CLIP](#kw-clip) · [FILL](#kw-fill) |
| Keyboard/mouse | [INKEY\$](#kw-inkey-s) · [MULTIKEY](#kw-multikey) · [MOUSE](#kw-mouse) · [INPUT\$](#kw-input-s) · [SLEEP](#kw-sleep) |
| Sound | [BEEP](#kw-beep) · [SOUND](#kw-sound) · [PLAY](#kw-play) · [ON PLAY](#kw-on-play) · [MUSIC](#kw-music) |
| Strings | [LEFT\$](#kw-left-s) · [MID\$](#kw-mid-s) · [INSTR](#kw-instr) · [LEN](#kw-len) · [STR\$](#kw-str-s) · [VAL](#kw-val) · [CHR\$](#kw-chr-s) · [ASC](#kw-asc) |
| Math | [ABS](#kw-abs) · [INT](#kw-int) · [FIX](#kw-fix) · [SQR](#kw-sqr) · [SIN](#kw-sin) · [RND](#kw-rnd) · [RANDOMIZE](#kw-randomize) |
| Files | [OPEN](#kw-open) · [CLOSE](#kw-close) · [GET](#kw-get) · [PUT](#kw-put) · [EOF](#kw-eof) · [KILL](#kw-kill) · [FILES](#kw-files) |
| Hardware | [DEF SEG](#kw-def-seg) · [PEEK](#kw-peek) · [POKE](#kw-poke) · [INP](#kw-inp) · [OUT](#kw-out) · [WAIT](#kw-wait) |

## Index

<a id="index"></a>

**A** — [ABS](#kw-abs) · ACCESS → [OPEN](#kw-open) · AND → [Operators](#topic-operators) · ANY → [DECLARE](#kw-declare) · APPEND → [OPEN](#kw-open) · AS → [DIM](#kw-dim) · [ASC](#kw-asc) · [ATN](#kw-atn)

**B** — BASE → [OPTION BASE](#kw-option-base) · [BEEP](#kw-beep) · BINARY → [OPEN](#kw-open) · [BLOAD](#kw-bload) · [BSAVE](#kw-bsave)

**C** — [CALL](#kw-call) · [CASE](#kw-case) · [CDBL](#kw-cdbl) · [CHAIN](#kw-chain) · [CHDIR](#kw-chdir) · [CHR\$](#kw-chr-s) · [CINT](#kw-cint) · [CIRCLE](#kw-circle) · [CLEAR](#kw-clear) · [CLIP](#kw-clip) · [CLNG](#kw-clng) · [CLOSE](#kw-close) · [CLS](#kw-cls) · [COLOR](#kw-color) · [COM](#kw-com) · [COMMON](#kw-common) · [CONST](#kw-const) · [COS](#kw-cos) · [CSNG](#kw-csng) · [CSRLIN](#kw-csrlin) · [CVD](#kw-cvd) · CVDMBF → [CVSMBF](#kw-cvsmbf) · [CVI](#kw-cvi) · [CVL](#kw-cvl) · [CVS](#kw-cvs) · [CVSMBF](#kw-cvsmbf)

**D** — [DATA](#kw-data) · [DATE\$](#kw-date-s) · [DECLARE](#kw-declare) · DEF → [DEF FN](#kw-def-fn) · [DEF FN](#kw-def-fn) · [DEF SEG](#kw-def-seg) · DEFDBL → [DEFINT](#kw-defint) · [DEFINT](#kw-defint) · DEFLNG → [DEFINT](#kw-defint) · DEFSNG → [DEFINT](#kw-defint) · DEFSTR → [DEFINT](#kw-defint) · [DIM](#kw-dim) · [DO](#kw-do) · DOUBLE → [DIM](#kw-dim) · [DRAW](#kw-draw)

**E** — ELSE → [IF](#kw-if) · ELSEIF → [IF](#kw-if) · [END](#kw-end) · [ENVIRON](#kw-environ) · ENVIRON\$ → [ENVIRON](#kw-environ) · [EOF](#kw-eof) · EQV → [Operators](#topic-operators) · [ERASE](#kw-erase) · [ERDEV](#kw-erdev) · ERDEV\$ → [ERDEV](#kw-erdev) · ERL → [ERR](#kw-err) · [ERR](#kw-err) · [ERROR](#kw-error) · [EXIT](#kw-exit) · [EXP](#kw-exp)

**F** — [FIELD](#kw-field) · [FILEATTR](#kw-fileattr) · [FILES](#kw-files) · [FILL](#kw-fill) · [FIX](#kw-fix) · [FOR](#kw-for) · [FRE](#kw-fre) · [FREEFILE](#kw-freefile) · [FUNCTION](#kw-function)

**G** — [GET](#kw-get) · [GOSUB](#kw-gosub) · [GOTO](#kw-goto)

**H** — [HEX\$](#kw-hex-s)

**I** — [IF](#kw-if) · IMP → [Operators](#topic-operators) · [INKEY\$](#kw-inkey-s) · [INP](#kw-inp) · [INPUT](#kw-input) · [INPUT\$](#kw-input-s) · [INSTR](#kw-instr) · [INT](#kw-int) · INTEGER → [DIM](#kw-dim) · [IOCTL](#kw-ioctl) · IOCTL\$ → [IOCTL](#kw-ioctl) · IS → [SELECT](#kw-select)

**K** — [KEY](#kw-key) · [KILL](#kw-kill)

**L** — [LBOUND](#kw-lbound) · [LCASE\$](#kw-lcase-s) · [LEFT\$](#kw-left-s) · [LEN](#kw-len) · [LET](#kw-let) · [LINE](#kw-line) · [LINE INPUT](#kw-line-input) · LIST → [KEY](#kw-key) · [LOADBMP](#kw-loadbmp) · [LOADGIF](#kw-loadgif) · [LOADPAL](#kw-loadpal) · [LOC](#kw-loc) · [LOCATE](#kw-locate) · [LOCK](#kw-lock) · [LOF](#kw-lof) · [LOG](#kw-log) · LONG → [DIM](#kw-dim) · LOOP → [DO](#kw-do) · LPOS → [LPRINT](#kw-lprint) · [LPRINT](#kw-lprint) · [LSET](#kw-lset) · [LTRIM\$](#kw-ltrim-s)

**M** — [MID\$](#kw-mid-s) · MKD\$ → [MKI\$](#kw-mki-s) · [MKDIR](#kw-mkdir) · MKDMBF\$ → [CVSMBF](#kw-cvsmbf) · [MKI\$](#kw-mki-s) · MKL\$ → [MKI\$](#kw-mki-s) · MKS\$ → [MKI\$](#kw-mki-s) · MKSMBF\$ → [CVSMBF](#kw-cvsmbf) · [MOD](#kw-mod) · [MOUSE](#kw-mouse) · [MULTIKEY](#kw-multikey) · [MUSIC](#kw-music)

**N** — [NAME](#kw-name) · NEXT → [FOR](#kw-for) · NOT → [Operators](#topic-operators)

**O** — [OCT\$](#kw-oct-s) · OFF → [ON TIMER](#kw-on-timer) · ON → [ON ERROR](#kw-on-error) · [ON ERROR](#kw-on-error) · [ON GOSUB](#kw-on-gosub) · [ON PLAY](#kw-on-play) · [ON TIMER](#kw-on-timer) · [OPEN](#kw-open) · OPTION → [OPTION BASE](#kw-option-base) · [OPTION BASE](#kw-option-base) · OR → [Operators](#topic-operators) · [OUT](#kw-out) · OUTPUT → [OPEN](#kw-open)

**P** — [PAINT](#kw-paint) · [PALETTE](#kw-palette) · [PCOPY](#kw-pcopy) · [PEEK](#kw-peek) · [PEN](#kw-pen) · [PLAY](#kw-play) · [PMAP](#kw-pmap) · [POINT](#kw-point) · [POKE](#kw-poke) · [POS](#kw-pos) · PRESERVE → [REDIM](#kw-redim) · PRESET → [PSET](#kw-pset) · [PRINT](#kw-print) · [PRINT USING](#kw-print-using) · [PSET](#kw-pset) · [PUT](#kw-put)

**R** — RANDOM → [OPEN](#kw-open) · [RANDOMIZE](#kw-randomize) · [READ](#kw-read) · [REDIM](#kw-redim) · [REM](#kw-rem) · [RESET](#kw-reset) · [RESTORE](#kw-restore) · [RESUME](#kw-resume) · [RETURN](#kw-return) · [RIGHT\$](#kw-right-s) · [RMDIR](#kw-rmdir) · [RND](#kw-rnd) · RSET → [LSET](#kw-lset) · RTRIM\$ → [LTRIM\$](#kw-ltrim-s) · [RUN](#kw-run)

**S** — [SCREEN](#kw-screen) · [SEEK](#kw-seek) · SEG → [DEF SEG](#kw-def-seg) · [SELECT](#kw-select) · [SGN](#kw-sgn) · [SHARED](#kw-shared) · [SHELL](#kw-shell) · [SIN](#kw-sin) · SINGLE → [DIM](#kw-dim) · [SLEEP](#kw-sleep) · [SOUND](#kw-sound) · [SPACE\$](#kw-space-s) · [SPC](#kw-spc) · [SQR](#kw-sqr) · [STATIC](#kw-static) · STEP → [FOR](#kw-for) · [STICK](#kw-stick) · [STOP](#kw-stop) · [STR\$](#kw-str-s) · STRIG → [STICK](#kw-stick) · STRING → [DIM](#kw-dim) · [STRING\$](#kw-string-s) · [SUB](#kw-sub) · [SWAP](#kw-swap) · [SYSTEM](#kw-system)

**T** — [TAB](#kw-tab) · [TAN](#kw-tan) · THEN → [IF](#kw-if) · [TIME\$](#kw-time-s) · [TIMER](#kw-timer) · TO → [FOR](#kw-for) · TROFF → [TRON](#kw-tron) · [TRON](#kw-tron) · [TYPE](#kw-type)

**U** — [UBOUND](#kw-ubound) · [UCASE\$](#kw-ucase-s) · UNLOCK → [LOCK](#kw-lock) · UNTIL → [DO](#kw-do) · USING → [PRINT USING](#kw-print-using)

**V** — [VAL](#kw-val) · [VARPTR](#kw-varptr) · [VARPTR\$](#kw-varptr-s) · [VARSEG](#kw-varseg) · [VIEW](#kw-view) · [VIEW PRINT](#kw-view-print)

**W** — [WAIT](#kw-wait) · WEND → [WHILE](#kw-while) · [WHILE](#kw-while) · [WIDTH](#kw-width) · [WINDOW](#kw-window) · [WRITE](#kw-write)

**X** — XOR → [Operators](#topic-operators)

## Language topics

<a id="topic-data-types"></a>

### Data Types

```
Suffix  Type      Range
  %     INTEGER   -32768 to 32767
  &     LONG      -2147483648 to 2147483647
  !     SINGLE    7 digits (default type)
  #     DOUBLE    15-16 digits
  $     STRING    up to 1 GB (QBasic: 32767); STRING * n fixed length
```

Use [DEFINT](#kw-defint) or [DIM](#kw-dim) AS to change types. [TYPE](#kw-type) defines records.

<a id="topic-operators"></a>

### Operators (highest precedence first)

```
^              exponentiation
-              negation
* /            multiplication, floating-point division
\              integer division
MOD            remainder
+ -            addition, subtraction (+ also joins strings)
= <> < > <= >= relational (true = -1, false = 0)
NOT AND OR XOR EQV IMP   logical / bitwise
```

<a id="topic-errors"></a>

### Run-time Error Codes

```
  1 NEXT without FOR                 37 Argument-count mismatch
  2 Syntax error                     38 Array not defined
  3 RETURN without GOSUB             40 Variable required
  4 Out of DATA                      50 FIELD overflow
  5 Illegal function call            51 Internal error
  6 Overflow                         52 Bad file name or number
  7 Out of memory                    53 File not found
  8 Label not defined                54 Bad file mode
  9 Subscript out of range           55 File already open
 10 Duplicate definition             56 FIELD statement active
 11 Division by zero                 57 Device I/O error
 12 Illegal in direct mode           58 File already exists
 13 Type mismatch                    59 Bad record length
 14 Out of string space              61 Disk full
 16 String formula too complex       62 Input past end of file
 17 Cannot continue                  63 Bad record number
 18 Function not defined             64 Bad file name
 19 No RESUME                        67 Too many files
 20 RESUME without error             68 Device unavailable
 24 Device timeout                   69 Communication-buffer overflow
 25 Device fault                     70 Permission denied
 26 FOR without NEXT                 71 Disk not ready
 27 Out of paper                     72 Disk-media error
 28 Out of stack space               73 Advanced feature unavailable
 29 WHILE without WEND               74 Rename across disks
 30 WEND without WHILE               75 Path/File access error
 33 Duplicate label                  76 Path not found
 35 Subprogram not defined
```

ERR returns the code inside an ON ERROR handler; ERROR n raises one.

See also: [ON ERROR](#kw-on-error), [ERR](#kw-err)

<a id="topic-game-programming"></a>

### Game Programming Extensions

QBLike emulates the hardware tricks used by DOS games:

- Direct VGA memory in SCREEN 13: DEF SEG = &HA000: POKE y \* 320 + x, c
- Vertical sync: WHILE (INP(&H3DA) AND 8) = 0: WEND  (or WAIT &H3DA, 8)
- Palette ports: OUT &H3C8, index: OUT &H3C9, r: OUT &H3C9, g: OUT &H3C9, b
- Keyboard scan codes: INP(&H60)
- Several keys at once: [MULTIKEY](#kw-multikey)(scancode)
- Mouse: [MOUSE](#kw-mouse) x, y, buttons
- Transparent sprites: PUT (x, y), sprite(0), MASK   (see [PUT](#kw-put))
- Clipped sprites: PUT never fails off-screen; area set with [CLIP](#kw-clip)
- Page flipping: SCREEN 7, 0, 1, 0 ... [PCOPY](#kw-pcopy) 1, 0
- Loading images: [LOADBMP](#kw-loadbmp), [LOADGIF](#kw-loadgif) and [LOADPAL](#kw-loadpal)
- Flood fill by color, any border: [FILL](#kw-fill) (x, y), color
- Background music: [MUSIC](#kw-music) "SONG.MOD", LOOP   (MOD or MIDI)
- Sound effects: id = [SOUND](#kw-sound) "BOOM.WAV": SOUND PLAY id
- AdLib FM ports: OUT &H388, reg: OUT &H389, value   (see [OUT](#kw-out))

See also: [INP](#kw-inp), [OUT](#kw-out), [DEF SEG](#kw-def-seg), [GET](#kw-get)

## Keyword reference

<a id="kw-abs"></a>

### ABS

```
ABS(numeric-expression)
```

Returns the absolute value of a number.

```
PRINT ABS(-5.5)     ' 5.5
```

See also: [SGN](#kw-sgn)

<a id="kw-asc"></a>

### ASC

```
ASC(stringexpression$)
```

Returns the ASCII code (0-255) of the first character of a string. Illegal function call if the string is empty.

```
PRINT ASC("A")      ' 65
```

See also: [CHR\$](#kw-chr-s)

<a id="kw-atn"></a>

### ATN

```
ATN(numeric-expression)
```

Returns the arctangent of a number, in radians (-PI/2 to PI/2). PI can be computed as 4 \* ATN(1).

See also: [COS](#kw-cos), [SIN](#kw-sin), [TAN](#kw-tan)

<a id="kw-beep"></a>

### BEEP

```
BEEP
```

Sounds the speaker: an 800 Hz tone for a quarter of a second.

See also: [SOUND](#kw-sound), [PLAY](#kw-play)

<a id="kw-bload"></a>

### BLOAD

```
BLOAD filespec$[, offset%]
BLOAD filespec$, VARPTR(array(index))
```

Loads a file saved with BSAVE.

With VARPTR(array(index)) as offset the data is copied raw into the numeric array, byte by byte, from that element (DEF SEG and the segment in the file are ignored). Illegal function call if it does not fit:

```
BLOAD "SHIP.SPR", VARPTR(ship%(0))
```

Otherwise the data goes to emulated video memory: DEF SEG when it is &HA000 or &HB800 and an offset is given, else the segment in the file.

```
&HA000  SCREEN 13: linear (up to 64000 bytes)
        SCREEN 7, 8, 9, 12: one bit plane, written to the planes
        enabled with OUT &H3C4, 2: OUT &H3C5, mask
&HB800  SCREEN 1, 2: CGA memory; SCREEN 0: character/attribute pairs
```

Files for other segments are ignored.

See also: [BSAVE](#kw-bsave), [DEF SEG](#kw-def-seg), [VARPTR](#kw-varptr)

<a id="kw-bsave"></a>

### BSAVE

```
BSAVE filespec$, offset%, length%
BSAVE filespec$, VARPTR(array(index)), length%
```

Saves length% bytes to a file in BSAVE format (&HFD, segment, offset, length, data).

With VARPTR(array(index)) the bytes come from the numeric array, from that element (2 bytes per INTEGER, 4 per LONG or SINGLE, 8 per DOUBLE):

```
GET (0, 0)-(15, 15), ship%
BSAVE "SHIP.SPR", VARPTR(ship%(0)), 260
```

Otherwise they come from emulated video memory at DEF SEG (see BLOAD). In SCREEN 7, 8, 9 and 12 one bit plane is saved, the one selected with OUT &H3CE, 4: OUT &H3CF, plane.

See also: [BLOAD](#kw-bload), [DEF SEG](#kw-def-seg), [VARPTR](#kw-varptr)

<a id="kw-call"></a>

### CALL

```
[CALL] name [([argumentlist])]
```

Transfers control to a SUB procedure. Without CALL the parentheses around the argument list are omitted. Arguments are passed by reference unless enclosed in parentheses.

See also: [SUB](#kw-sub), [DECLARE](#kw-declare)

<a id="kw-case"></a>

### CASE

```
CASE expressionlist
```

See SELECT CASE.

See also: [SELECT](#kw-select)

<a id="kw-cdbl"></a>

### CDBL

```
CDBL(numeric-expression)
```

Converts a numeric expression to DOUBLE precision.

See also: [CINT](#kw-cint), [CLNG](#kw-clng), [CSNG](#kw-csng)

<a id="kw-chain"></a>

### CHAIN

```
CHAIN filespec$
```

Transfers control to another program. Not supported in QBLike: raises "Advanced feature unavailable". Combine the programs or use RUN.

See also: [RUN](#kw-run), [COMMON](#kw-common)

<a id="kw-chdir"></a>

### CHDIR

```
CHDIR pathspec$
```

Changes the current directory.

```
CHDIR "GAMES"
```

See also: [MKDIR](#kw-mkdir), [RMDIR](#kw-rmdir), [FILES](#kw-files)

<a id="kw-chr-s"></a>

### CHR$

```
CHR$(code%)
```

Returns the character with the given ASCII code (0-255).

```
PRINT CHR$(65); CHR$(3)     ' A and a heart
```

See also: [ASC](#kw-asc)

<a id="kw-cint"></a>

### CINT

```
CINT(numeric-expression)
```

Rounds to the nearest INTEGER; halves round to the even number (CINT(2.5) = 2, CINT(3.5) = 4). Range -32768 to 32767 (Overflow otherwise).

See also: [CLNG](#kw-clng), [FIX](#kw-fix), [INT](#kw-int)

<a id="kw-circle"></a>

### CIRCLE

```
CIRCLE [STEP] (x!,y!),radius![,[color%] [,[start!] [,[end!] [,aspect!]]]]
```

Draws a circle, ellipse or arc. Start and end angles are in radians; negative angles also draw a radius line.

See also: [LINE](#kw-line), [PSET](#kw-pset), [PAINT](#kw-paint)

<a id="kw-clear"></a>

### CLEAR

```
CLEAR [,,stack&]
```

Closes all files, erases all arrays and resets every module-level variable to zero or "". The stack size argument of QBasic is accepted and ignored (QBLike's stack is not limited this way).

See also: [ERASE](#kw-erase)

<a id="kw-clip"></a>

### CLIP *(QBLike extension)*

```
CLIP x1%, y1%, x2%, y2%
CLIP
```

Sets the clipping rectangle used by PUT (and by LOADBMP/LOADGIF ..., SCREEN), in absolute screen coordinates: the origin is not moved, unlike VIEW. CLIP alone, or SCREEN with a mode, resets it to the whole screen.

PUT never fails for being off-screen: only the part of the image inside the rectangle is drawn, so sprites can leave the screen on any side.

```
CLIP 0, 20, 319, 199     ' keep the top 20 lines (score bar) intact
PUT (x%, y%), sprite%(0), MASK
```

See also: [PUT](#kw-put), [VIEW](#kw-view)

<a id="kw-clng"></a>

### CLNG

```
CLNG(numeric-expression)
```

Rounds to the nearest LONG integer (halves to even). Range -2147483648 to 2147483647.

See also: [CINT](#kw-cint)

<a id="kw-close"></a>

### CLOSE

```
CLOSE [[#]filenumber%[,[#]filenumber%]...]
```

Closes one or more open files. CLOSE alone closes all files.

See also: [OPEN](#kw-open), [RESET](#kw-reset)

<a id="kw-cls"></a>

### CLS

```
CLS [{0 | 1 | 2}]
```

Clears the screen and moves the cursor to the top-left corner.

```
CLS      clears the active viewport (graphics VIEW or text VIEW PRINT)
CLS 0    clears the whole screen
CLS 1    clears the graphics viewport
CLS 2    clears the text viewport (VIEW PRINT area)
```

The screen is filled with the current background color.

See also: [VIEW](#kw-view), [VIEW PRINT](#kw-view-print)

<a id="kw-color"></a>

### COLOR

```
SCREEN 0:       COLOR [foreground%] [,[background%] [,border%]]
SCREEN 7-10:    COLOR [foreground%] [,background%]
SCREEN 12-13:   COLOR [foreground%]
```

Sets screen display colors. In text mode foreground 0-31 (16-31 blink) and background 0-7.

```
0 Black    4 Red       8 Gray         12 Light Red
1 Blue     5 Magenta   9 Light Blue   13 Light Magenta
2 Green    6 Brown    10 Light Green  14 Yellow
3 Cyan     7 White    11 Light Cyan   15 Bright White
```

See also: [PALETTE](#kw-palette), [SCREEN](#kw-screen)

<a id="kw-com"></a>

### COM

```
COM(n%) ON | OFF | STOP
ON COM(n%) GOSUB line
```

Serial port event trapping. Accepted so old programs run, but QBLike has no serial ports: the event never fires.

See also: [ON TIMER](#kw-on-timer)

<a id="kw-common"></a>

### COMMON

```
COMMON [SHARED] variablelist
```

Shares variables between chained programs. QBLike does not chain programs, so COMMON is treated like DIM: with SHARED the variables are visible in every SUB and FUNCTION.

Note: the types are decided where COMMON appears. A COMMON SHARED before DEFINT A-Z declares SINGLE variables; the INTEGER ones with the same names used later are different variables.

See also: [DIM](#kw-dim), [SHARED](#kw-shared)

<a id="kw-const"></a>

### CONST

```
CONST constantname = expression [,constantname = expression]...
```

Declares symbolic constants that can be used instead of numbers or strings. A constant's value cannot change. Constants declared in the module are visible in all procedures.

```
CONST PI = 3.141593, TITLE$ = "Space Game"
CONST TRUE = -1, FALSE = NOT TRUE
CONST MAXX% = 319
```

See also: [DIM](#kw-dim)

<a id="kw-cos"></a>

### COS

```
COS(angle)
```

Returns the cosine of an angle given in radians (degrees \* PI / 180).

See also: [SIN](#kw-sin), [TAN](#kw-tan), [ATN](#kw-atn)

<a id="kw-csng"></a>

### CSNG

```
CSNG(numeric-expression)
```

Converts a numeric expression to SINGLE precision.

See also: [CDBL](#kw-cdbl)

<a id="kw-csrlin"></a>

### CSRLIN

```
CSRLIN
```

Returns the current row of the text cursor (1-based). POS(0) returns the column.

See also: [POS](#kw-pos), [LOCATE](#kw-locate)

<a id="kw-cvd"></a>

### CVD

```
CVD(8-byte-string)
```

See CVI.

See also: [CVI](#kw-cvi), [MKD\$](#kw-mki-s)

<a id="kw-cvi"></a>

### CVI

```
CVI(2-byte-string)  CVL(4-byte-string)  CVS(4-byte-string)  CVD(8-byte-string)
```

Convert strings created by MKI\$, MKL\$, MKS\$ and MKD\$ (2, 4, 4 and 8 bytes) back to numbers. Used with FIELD buffers and binary files.

See also: [MKI\$](#kw-mki-s)

<a id="kw-cvl"></a>

### CVL

```
CVL(4-byte-string)
```

See CVI.

See also: [CVI](#kw-cvi), [MKL\$](#kw-mki-s)

<a id="kw-cvs"></a>

### CVS

```
CVS(4-byte-string)
```

See CVI.

See also: [CVI](#kw-cvi), [MKS\$](#kw-mki-s)

<a id="kw-cvsmbf"></a>

### CVSMBF

```
CVSMBF(4-byte-string$)
CVDMBF(8-byte-string$)
MKSMBF$(single-expression!)
MKDMBF$(double-expression#)
```

Convert between numbers and strings in Microsoft Binary Format, the floating point format of GW-BASIC and early QuickBASIC data files. CVSMBF/CVDMBF read such a string; MKSMBF\$/MKDMBF\$ write one.

See also: [CVI](#kw-cvi), [MKI\$](#kw-mki-s)

<a id="kw-data"></a>

### DATA

```
DATA constant[,constant]...
```

Stores numeric and string constants that READ assigns to variables. DATA statements can be anywhere in the module (not inside a SUB or FUNCTION); they are read in order. Strings that contain commas, colons or leading spaces must be in quotes. Use RESTORE to read them again.

```
FOR i% = 1 TO 3: READ n$, age%: PRINT n$; age%: NEXT
DATA Ann, 31, "Smith, John", 45, Bob, 27
```

See also: [READ](#kw-read), [RESTORE](#kw-restore)

<a id="kw-date-s"></a>

### DATE$

```
DATE$
```

Returns the current date as "mm-dd-yyyy". (Setting the date with DATE\$ = ... is accepted and ignored.)

See also: [TIME\$](#kw-time-s), [TIMER](#kw-timer)

<a id="kw-declare"></a>

### DECLARE

```
DECLARE {SUB | FUNCTION} name [([parameterlist])]
```

Declares a procedure. QBLike adds DECLARE lines automatically when saving; they are not required to run.

See also: [SUB](#kw-sub), [FUNCTION](#kw-function)

<a id="kw-def-fn"></a>

### DEF FN

```
DEF FNname[(parameterlist)] = expression

DEF FNname[(parameterlist)]
    [statementblock]
    FNname = expression
END DEF
```

Defines a function inside the module, in the older style. The name must begin with FN. Module variables are visible inside DEF FN (unlike SUB/FUNCTION, where they need SHARED). FUNCTION procedures are preferred.

```
DEF FNarea (r) = 3.14159 * r ^ 2
PRINT FNarea(10)
```

See also: [FUNCTION](#kw-function)

<a id="kw-def-seg"></a>

### DEF SEG

```
DEF SEG [=address]
```

Sets the segment used by PEEK, POKE, BLOAD and BSAVE. Emulated segments:

```
&HA000  SCREEN 13: 1 byte per pixel, offset = y*320+x
        SCREEN 7, 8, 9, 12: EGA/VGA bit planes, offset = y*(width\8)+x\8;
        OUT &H3C4, 2: OUT &H3C5, mask selects the planes written,
        OUT &H3CE, 4: OUT &H3CF, plane selects the plane read
&HB800  SCREEN 0: character/attribute pairs; SCREEN 1, 2: CGA memory
0       BIOS: PEEK(&H417) keyboard flags, PEEK(&H46C) timer ticks
```

See also: [PEEK](#kw-peek), [POKE](#kw-poke), [BLOAD](#kw-bload)

<a id="kw-defint"></a>

### DEFINT

```
DEFINT letterrange [,letterrange]...
DEFLNG / DEFSNG / DEFDBL / DEFSTR
```

Sets the default data type for variables whose names begin with the specified letters. Example: DEFINT A-Z

See also: [DIM](#kw-dim)

<a id="kw-dim"></a>

### DIM

```
DIM [SHARED] variable[(subscripts)] [AS type] [,variable[(subscripts)] [AS type]]...

subscripts: [lower% TO] upper% [,[lower% TO] upper%]...
type: INTEGER, LONG, SINGLE, DOUBLE, STRING, STRING * n, or a TYPE name
```

Declares a variable or array and its type.

- Arrays with constant bounds are static: they are created when the program starts. Arrays with variable bounds (or created with REDIM) are dynamic: ERASE frees them and REDIM resizes them.
- Without DIM, an array used with subscripts is created with bounds 0 TO 10 (or 1 TO 10 with OPTION BASE 1).
- SHARED makes the variable visible in every SUB and FUNCTION.
- As in QBasic, elements are stored with the FIRST subscript varying fastest: a(0,0), a(1,0), a(0,1)... This matters when GET, PUT, BLOAD or BSAVE use part of a multi-dimensional array, e.g. one sprite per column: GET (0, 0)-(15, 15), sprites%(0, i%)

QBLike limits: up to 64 million elements per array and subscripts in the whole LONG range (QBasic: 64 KB per array, -32768 to 32767).

```
DIM a%(100), grid(1 TO 20, 1 TO 20), title AS STRING * 20
DIM SHARED score&, player AS PlayerType
```

See also: [REDIM](#kw-redim), [ERASE](#kw-erase), [TYPE](#kw-type), [SHARED](#kw-shared), [OPTION BASE](#kw-option-base)

<a id="kw-do"></a>

### DO

```
DO [{WHILE | UNTIL} condition]
    [statementblock]
LOOP

DO
    [statementblock]
LOOP [{WHILE | UNTIL} condition]
```

Repeats a block of statements while a condition is true or until it becomes true. EXIT DO leaves the loop.

See also: [WHILE](#kw-while), [FOR](#kw-for), [EXIT](#kw-exit)

<a id="kw-draw"></a>

### DRAW

```
DRAW commandstring$
```

Draws an object.

```
U/D/L/R [n]  up/down/left/right    E/F/G/H [n]  diagonals
M [+|-]x,y   move to / by          B  prefix: move without drawing
N  prefix: return after drawing    A n  angle (0-3)   TA n  turn angle
C n  color   S n  scale (default 4) P paint,border
X + VARPTR$(a$)  runs a$   any n can be "=" + VARPTR$(var%)
```

See also: [LINE](#kw-line), [PSET](#kw-pset), [VARPTR\$](#kw-varptr-s)

<a id="kw-end"></a>

### END

```
END [{DEF | FUNCTION | IF | SELECT | SUB | TYPE}]
```

END alone ends the program: files are closed and, in the editor, the screen waits for a key ("Press any key to continue").

END IF, END SELECT, END SUB, END FUNCTION, END DEF and END TYPE close the corresponding block.

See also: [STOP](#kw-stop), [SYSTEM](#kw-system)

<a id="kw-environ"></a>

### ENVIRON

```
ENVIRON$(name$)
ENVIRON$(n%)
ENVIRON "name=value"
```

ENVIRON\$(name\$) returns the value of an environment variable ("" if it does not exist); ENVIRON\$(n%) returns the n-th entry as "NAME=value". The ENVIRON statement sets a variable for this program and the programs it starts with SHELL.

See also: [SHELL](#kw-shell)

<a id="kw-eof"></a>

### EOF

```
EOF(filenumber%)
```

Returns true (-1) when the end of a file has been reached, false (0) otherwise.

```
DO UNTIL EOF(1): LINE INPUT #1, l$: LOOP
```

See also: [LOF](#kw-lof), [LOC](#kw-loc), [OPEN](#kw-open)

<a id="kw-erase"></a>

### ERASE

```
ERASE arrayname [,arrayname]...
```

For static arrays, sets every element to zero or "". For dynamic arrays, frees the memory: the array must be dimensioned again (DIM or REDIM) before it is used.

```
ERASE a%, names$
```

See also: [DIM](#kw-dim), [REDIM](#kw-redim)

<a id="kw-erdev"></a>

### ERDEV

```
ERDEV
ERDEV$
```

Return the code and name of the device that caused the last critical error in DOS. In QBLike they always return 0 and "".

See also: [ERR](#kw-err)

<a id="kw-err"></a>

### ERR

```
ERR
ERL
```

ERR returns the code of the last error, ERL the nearest line number before the statement that caused it.

See also: [ON ERROR](#kw-on-error), [ERROR](#kw-error), [Errors](#topic-errors)

<a id="kw-error"></a>

### ERROR

```
ERROR expression%
```

Simulates the occurrence of the error with the given code (1-255), which can be trapped with ON ERROR. Codes not used by BASIC can be used for the program's own errors. See [Errors](#topic-errors) for the codes.

```
IF lives% < 0 THEN ERROR 200
```

See also: [ON ERROR](#kw-on-error), [ERR](#kw-err)

<a id="kw-exit"></a>

### EXIT

```
EXIT {DEF | DO | FOR | FUNCTION | SUB}
```

Leaves a block early:

```
EXIT DO        continues after LOOP
EXIT FOR       continues after NEXT
EXIT SUB       returns from the SUB
EXIT FUNCTION  returns from the FUNCTION (with the value assigned so far)
EXIT DEF       returns from the DEF FN
```

See also: [DO](#kw-do), [FOR](#kw-for), [SUB](#kw-sub), [FUNCTION](#kw-function)

<a id="kw-exp"></a>

### EXP

```
EXP(numeric-expression)
```

Returns e (2.718282) raised to the given power.

See also: [LOG](#kw-log)

<a id="kw-field"></a>

### FIELD

```
FIELD [#]filenumber%, width% AS stringvariable$ [, width% AS ...]
```

Divides the record buffer of a file opened FOR RANDOM into string variables. Assign them with LSET or RSET (never with =), then write the record with PUT; GET fills them again:

```
OPEN "DATA.DAT" FOR RANDOM AS #1 LEN = 30
FIELD #1, 20 AS name$, 10 AS phone$
LSET name$ = "Ann": LSET phone$ = "555-1234"
PUT #1, 1
```

See also: [LSET](#kw-lset), [OPEN](#kw-open), [GET](#kw-get)

<a id="kw-fileattr"></a>

### FILEATTR

```
FILEATTR(filenumber%, attribute%)
```

attribute% = 1 returns the mode the file was opened in: 1 INPUT, 2 OUTPUT, 4 RANDOM, 8 APPEND, 32 BINARY. attribute% = 2 (the DOS file handle) returns 0.

See also: [OPEN](#kw-open)

<a id="kw-files"></a>

### FILES

```
FILES [filespec$]
```

Lists the names of the files that match filespec\$ (wildcards \* and ? allowed; default all files) in the current or given directory, like the DIR command of DOS.

```
FILES "*.BAS"
FILES "DATA\*.*"
```

See also: [CHDIR](#kw-chdir), [KILL](#kw-kill)

<a id="kw-fill"></a>

### FILL *(QBLike extension)*

```
FILL [STEP] (x!, y!)[, color%]
```

Fills with color% the area of pixels that have the same color as (x, y) and touch it horizontally or vertically, whatever colors surround it (PAINT needs a single border color). Without color%, the foreground color is used. Coordinates follow WINDOW and VIEW, and the fill stays inside the VIEW viewport. If (x, y) already has color%, nothing happens.

```
LINE (10, 10)-(60, 60), 4, B
FILL (30, 30), 14            ' interior of the box
FILL (20, 10), 2             ' recolor the box itself
```

FILL is not a reserved word: a program with its own SUB, FUNCTION, variable or array called FILL keeps using it.

See also: [PAINT](#kw-paint), [POINT](#kw-point)

<a id="kw-fix"></a>

### FIX

```
FIX(numeric-expression)
```

Truncates toward zero: FIX(-3.7) = -3. INT rounds down instead: INT(-3.7) = -4.

See also: [INT](#kw-int), [CINT](#kw-cint)

<a id="kw-for"></a>

### FOR

```
FOR counter = start TO end [STEP increment]
    [statementblock]
NEXT [counter [,counter]...]
```

Repeats a block a specified number of times. The counter goes from start to end, adding increment (default 1) each time; with a negative STEP it counts down. The loop does not run at all if start is already past end. EXIT FOR leaves it early.

```
FOR i% = 1 TO 10: PRINT i%;: NEXT i%
FOR x! = 1 TO 0 STEP -.1: PRINT x!: NEXT
```

See also: [DO](#kw-do), [EXIT](#kw-exit)

<a id="kw-fre"></a>

### FRE

```
FRE(stringexpression$)
FRE(numeric-expression)
```

Returns free memory. QBasic reported a few tens of KB; QBLike has no such limits (strings up to 1 GB, arrays up to 64 million elements, about 100000 levels of recursion), so it returns 268435456 (256 MB), or 67108864 for FRE(-2) (stack).

See also: [DIM](#kw-dim)

<a id="kw-freefile"></a>

### FREEFILE

```
FREEFILE
```

Returns the next unused file number, to use with OPEN.

```
f% = FREEFILE: OPEN "LOG.TXT" FOR APPEND AS #f%
```

See also: [OPEN](#kw-open)

<a id="kw-function"></a>

### FUNCTION

```
FUNCTION name[(parameterlist)] [STATIC]
    [statementblock]
    name = expression
END FUNCTION
```

Defines a FUNCTION procedure. Type FUNCTION name in the editor (or use Edit \> New FUNCTION) and it gets its own window; press F2 to switch.

See also: [SUB](#kw-sub), [DECLARE](#kw-declare), [EXIT](#kw-exit)

<a id="kw-get"></a>

### GET

```
GET [#]filenumber%[,[record&][,variable]]        (files)
GET [STEP](x1!,y1!)-[STEP](x2!,y2!), arrayname[(index%)]   (graphics)
```

Files: reads a record from a file opened FOR RANDOM, or bytes from a file opened FOR BINARY, into variable (or the FIELD buffer). record& is the record number (RANDOM) or byte position (BINARY), counting from 1; omitted, the next one is read.

Graphics: stores the rectangle between the two points in a numeric array, to draw it later with PUT. The array needs 4 + INT((width \* bitsperpixel + 7) / 8) \* planes \* height bytes (SCREEN 13: 4 + width \* height).

```
DIM sprite%(130)
GET (0, 0)-(15, 15), sprite%
```

See also: [PUT](#kw-put), [OPEN](#kw-open)

<a id="kw-gosub"></a>

### GOSUB

```
GOSUB line
...
RETURN [line]
```

GOSUB jumps to a label or line number in the same module or procedure; RETURN goes back to the statement after the GOSUB. RETURN line returns to another label instead.

```
GOSUB DrawScore
...
END
DrawScore:
    LOCATE 1, 1: PRINT score%
RETURN
```

See also: [RETURN](#kw-return), [ON GOSUB](#kw-on-gosub)

<a id="kw-goto"></a>

### GOTO

```
GOTO line
```

Jumps to a line label or number in the same module or procedure.

```
start:
    INPUT "Again (Y/N)"; a$
    IF UCASE$(a$) = "Y" THEN GOTO start
```

See also: [GOSUB](#kw-gosub)

<a id="kw-hex-s"></a>

### HEX$

```
HEX$(expression&)
```

Returns the hexadecimal representation of a number (rounded to an integer) as a string.

```
PRINT HEX$(255)     ' FF
```

See also: [OCT\$](#kw-oct-s)

<a id="kw-if"></a>

### IF

```
IF condition THEN thenpart [ELSE elsepart]

IF condition1 THEN
    [statementblock-1]
[ELSEIF condition2 THEN
    [statementblock-2]]...
[ELSE
    [statementblock-n]]
END IF
```

Executes statements depending on conditions. A condition is true when it is not zero. The single-line form can contain several statements separated by colons, and THEN line / ELSE line jump to a label. The block form ends with END IF.

```
IF x% > 10 THEN PRINT "Big" ELSE PRINT "Small"
IF a$ = "" THEN
    PRINT "Empty"
ELSEIF LEN(a$) > 10 THEN
    PRINT "Long"
ELSE
    PRINT a$
END IF
```

See also: [SELECT](#kw-select)

<a id="kw-inkey-s"></a>

### INKEY$

```
INKEY$
```

Reads a character from the keyboard without waiting. Extended keys return CHR\$(0) + scan code (e.g. up arrow = CHR\$(0) + "H").

See also: [INPUT\$](#kw-input-s), [MULTIKEY](#kw-multikey), [INP](#kw-inp)

<a id="kw-inp"></a>

### INP

```
INP(port%)
```

Emulated ports:

```
&H60   last keyboard scan code (bit 7 set on release)
&H3DA  VGA status: bit 3 = vertical retrace (70 Hz)
&H3C9  palette data read (after OUT &H3C7, index)
&H388  AdLib FM status (timers work: AdLib detection succeeds)
&H330  MPU-401: not emulated, returns &HFF
```

VSYNC wait:

```
WHILE (INP(&H3DA) AND 8) <> 0: WEND
WHILE (INP(&H3DA) AND 8) = 0: WEND
```

See also: [OUT](#kw-out), [WAIT](#kw-wait), [MULTIKEY](#kw-multikey)

<a id="kw-input"></a>

### INPUT

```
INPUT [;] ["prompt"{; | ,}] variablelist
INPUT #filenumber%, variablelist
```

Reads values typed at the keyboard (separated by commas) into the variables, after showing the prompt. With ; after the prompt a question mark is shown; with , it is not. If the values don't match the variables, "Redo from start" is shown and the input is asked again. With ; right after INPUT the cursor stays on the line after Enter. INPUT \# reads values separated by commas or line ends from a file.

```
INPUT "Name, age"; n$, age%
```

See also: [LINE INPUT](#kw-line-input), [INPUT\$](#kw-input-s)

<a id="kw-input-s"></a>

### INPUT$

```
INPUT$(n[,[#]filenumber%])
```

Returns n characters read from the keyboard (without showing them and without waiting for Enter) or from a file. n can be 1 to 32767.

```
PRINT "Press a key": k$ = INPUT$(1)
header$ = INPUT$(4, #1)
```

See also: [INKEY\$](#kw-inkey-s)

<a id="kw-instr"></a>

### INSTR

```
INSTR([start%,]stringexpression1$,stringexpression2$)
```

Returns the position of the first occurrence of stringexpression2\$ in stringexpression1\$, searching from position start% (default 1), or 0 if it is not found. Returns a LONG (strings can be very long in QBLike).

```
PRINT INSTR("HELLO WORLD", "O")       ' 5
PRINT INSTR(6, "HELLO WORLD", "O")    ' 8
```

See also: [MID\$](#kw-mid-s)

<a id="kw-int"></a>

### INT

```
INT(numeric-expression)
```

Returns the largest integer less than or equal to a number: INT(3.7) = 3, INT(-3.2) = -4.

```
dice% = INT(RND * 6) + 1
```

See also: [FIX](#kw-fix), [CINT](#kw-cint)

<a id="kw-ioctl"></a>

### IOCTL

```
IOCTL [#]filenumber%, string$
IOCTL$([#]filenumber%)
```

Send and read control strings to a device driver. Accepted; IOCTL does nothing and IOCTL\$ returns "".

See also: [OPEN](#kw-open)

<a id="kw-key"></a>

### KEY

```
KEY n%, string$
KEY LIST | ON | OFF
KEY(n%) ON | OFF | STOP
ON KEY(n%) GOSUB line
```

Function key assignments and key event trapping. Accepted so old programs run, but ON KEY events never fire. To read keys use INKEY\$, or MULTIKEY for several keys at once.

See also: [INKEY\$](#kw-inkey-s), [MULTIKEY](#kw-multikey)

<a id="kw-kill"></a>

### KILL

```
KILL filespec$
```

Deletes the files that match filespec\$ (wildcards \* and ? allowed, as in DOS). File not found if none matches.

```
KILL "TEMP.DAT"
KILL "SAVES\*.TMP"
```

See also: [FILES](#kw-files), [NAME](#kw-name)

<a id="kw-lbound"></a>

### LBOUND

```
LBOUND(array[,dimension%])
UBOUND(array[,dimension%])
```

Return the lowest and highest subscript of an array dimension (dimension% 1 by default).

```
DIM a(5 TO 20)
FOR i = LBOUND(a) TO UBOUND(a): a(i) = i: NEXT
```

See also: [DIM](#kw-dim)

<a id="kw-lcase-s"></a>

### LCASE$

```
LCASE$(stringexpression$)
UCASE$(stringexpression$)
```

Convert a string to lowercase / uppercase (letters A-Z only).

```
IF UCASE$(answer$) = "Y" THEN ...
```

<a id="kw-left-s"></a>

### LEFT$

```
LEFT$(stringexpression$,n%)
RIGHT$(stringexpression$,n%)
```

Return the leftmost / rightmost n% characters of a string (the whole string if n% is larger than its length).

```
PRINT LEFT$("QBASIC", 2), RIGHT$("QBASIC", 3)     ' QB   SIC
```

See also: [MID\$](#kw-mid-s), [LEN](#kw-len)

<a id="kw-len"></a>

### LEN

```
LEN(stringexpression$)
LEN(variable)
```

Returns the number of characters in a string, or the number of bytes a variable takes (2 for INTEGER, 4 LONG/SINGLE, 8 DOUBLE, the record size for a TYPE variable). Returns a LONG (strings can be up to 1 GB in QBLike).

```
PRINT LEN("QBASIC")     ' 6
```

<a id="kw-let"></a>

### LET

```
[LET] variable=expression
```

Assigns the value of an expression to a variable. The word LET is optional. Numbers are converted between numeric types (rounding to integer types); assigning a string to a number or vice versa is a Type mismatch.

```
LET x = 5
name$ = "Ann": total& = total& + x
```

<a id="kw-line"></a>

### LINE

```
LINE [[STEP](x1!,y1!)]-[STEP](x2!,y2!) [,[color%] [,[B | BF] [,style%]]]
```

Draws a line between two points, or a rectangle:

```
B     draws a box with the two points as opposite corners
BF    draws a filled box
```

style% is a 16-bit pattern for dotted lines (&HFFFF solid, &HAAAA dotted, &HF0F0 dashed). Without the first point, the line starts at the last point drawn. STEP makes a point relative to the previous one.

```
LINE (0, 0)-(319, 199), 14
LINE (10, 10)-(100, 50), 4, BF
LINE -STEP(20, 0), , , &HAAAA
```

See also: [CIRCLE](#kw-circle), [PSET](#kw-pset), [LINE INPUT](#kw-line-input)

<a id="kw-line-input"></a>

### LINE INPUT

```
LINE INPUT [;] ["prompt";] variable$
LINE INPUT #filenumber%, variable$
```

Reads a whole line (up to Enter), including commas and quotes, into a string variable. From a file, reads up to the end of the line. With ; after LINE INPUT the cursor stays on the same line after Enter.

```
LINE INPUT "Your name: "; n$
DO UNTIL EOF(1): LINE INPUT #1, l$: PRINT l$: LOOP
```

See also: [INPUT](#kw-input)

<a id="kw-loadbmp"></a>

### LOADBMP *(QBLike extension)*

```
LOADBMP filespec$, arrayname[(index%)]
LOADBMP filespec$, SCREEN, x!, y! [, PALETTE]
```

Loads an indexed BMP (2, 16 or 256 colors; uncompressed, RLE4 or RLE8).

Array form: stores the image in GET/PUT format for the current screen mode, so PUT (x, y), arrayname(index) draws it. The array must be large enough (Illegal function call otherwise, like GET).

SCREEN form: draws the image at (x, y). The image must fit the current mode's colors (256 colors needs SCREEN 13, 16 colors a 16-color mode). PALETTE also sets the screen palette from the file.

```
SCREEN 13
LOADBMP "fondo.bmp", SCREEN, 0, 0, PALETTE
DIM nave%(130): LOADBMP "nave.bmp", nave%(0)
PUT (100, 100), nave%(0), MASK
```

See also: [LOADGIF](#kw-loadgif), [LOADPAL](#kw-loadpal), [PUT](#kw-put), [GET](#kw-get)

<a id="kw-loadgif"></a>

### LOADGIF *(QBLike extension)*

```
LOADGIF filespec$, arrayname[(index%)]
LOADGIF filespec$, SCREEN, x!, y! [, PALETTE]
```

Like LOADBMP but for GIF files.

Animated GIFs: every frame is composed on the GIF's logical screen and stored in the array one after another, each frame taking

```
(4 + bytes of pixels + elementsize - 1) \ elementsize  elements.
```

In SCREEN 13 with an INTEGER array: (4 + w \* h + 1) \\ 2 per frame. The SCREEN form draws the first frame.

See also: [LOADBMP](#kw-loadbmp), [LOADPAL](#kw-loadpal), [PUT](#kw-put)

<a id="kw-loadpal"></a>

### LOADPAL *(QBLike extension)*

```
LOADPAL filespec$, arrayname&[(index%)]
```

Loads only the palette of a BMP or GIF file into a LONG array, one element per color, in PALETTE USING format (65536 \* blue + 256 \* green + red, each 0-63).

```
DIM pal&(255)
LOADPAL "fondo.gif", pal&(0)
PALETTE USING pal&(0)
```

See also: [PALETTE](#kw-palette), [LOADBMP](#kw-loadbmp), [LOADGIF](#kw-loadgif)

<a id="kw-loc"></a>

### LOC

```
LOC(filenumber%)
```

Returns the current position within a file: for RANDOM files the number of the last record read or written, for BINARY files the last byte position, and for sequential files the position divided by 128.

See also: [SEEK](#kw-seek), [EOF](#kw-eof)

<a id="kw-locate"></a>

### LOCATE

```
LOCATE [row%] [,[column%] [,[cursor%] [,start% [,stop%]]]]
```

Moves the text cursor and sets its shape.

```
row%, column%   new position (1-based); omitted values stay the same
cursor%         1 shows the cursor, 0 hides it
start%, stop%   first and last scan line of the cursor shape

LOCATE 12, 30: PRINT "GAME OVER"
LOCATE , , 0                   ' hide the cursor
```

See also: [CSRLIN](#kw-csrlin), [POS](#kw-pos)

<a id="kw-lock"></a>

### LOCK

```
LOCK [#]filenumber% [, {record& | [start&] TO end&}]
UNLOCK [#]filenumber% [, {record& | [start&] TO end&}]
```

Lock part of a file against other programs on a network. Accepted; they have no effect.

See also: [OPEN](#kw-open)

<a id="kw-lof"></a>

### LOF

```
LOF(filenumber%)
```

Returns the length of an open file in bytes.

See also: [EOF](#kw-eof)

<a id="kw-log"></a>

### LOG

```
LOG(numeric-expression)
```

Returns the natural logarithm (base e) of a positive number. For base 10: LOG(x) / LOG(10).

See also: [EXP](#kw-exp)

<a id="kw-lprint"></a>

### LPRINT

```
LPRINT [expressionlist] [{; | ,}]
LPRINT USING formatstring$; expressionlist
```

Prints on the printer. QBLike has no printer: the output is discarded. LPOS(n%) returns 0.

See also: [PRINT](#kw-print)

<a id="kw-lset"></a>

### LSET

```
LSET stringvariable$ = stringexpression$
RSET stringvariable$ = stringexpression$
LSET record1 = record2
```

Copy a string into a variable without changing its length: LSET aligns it to the left, RSET to the right, padding with spaces or cutting the end. Used with FIELD buffers and fixed-length strings. LSET also copies one record variable into another of a different TYPE.

See also: [FIELD](#kw-field), [MID\$](#kw-mid-s)

<a id="kw-ltrim-s"></a>

### LTRIM$

```
LTRIM$(stringexpression$)
RTRIM$(stringexpression$)
```

LTRIM\$ removes the leading spaces of a string, RTRIM\$ the trailing ones.

```
PRINT "[" + LTRIM$(STR$(5)) + "]"     ' [5]
```

<a id="kw-mid-s"></a>

### MID$

```
MID$(stringexpression$,start%[,length%])        (function)
MID$(stringvariable$,start%[,length%])=expression$   (statement)
```

Function: returns length% characters of a string starting at position start% (all the rest if length% is omitted). Statement: replaces characters of a string variable, starting at start%, with those of expression\$; the length of the variable does not change.

```
PRINT MID$("QBASIC 1.1", 3, 4)        ' ASIC
a$ = "HELLO": MID$(a$, 2) = "AL": PRINT a$    ' HALLO
```

See also: [LEFT\$](#kw-left-s), [INSTR](#kw-instr)

<a id="kw-mkdir"></a>

### MKDIR

```
MKDIR pathspec$
```

Creates a directory.

See also: [CHDIR](#kw-chdir), [RMDIR](#kw-rmdir)

<a id="kw-mki-s"></a>

### MKI$

```
MKI$(integer%)  MKL$(long&)  MKS$(single!)  MKD$(double#)
```

Convert numbers to strings holding their binary representation: MKI\$ 2 bytes, MKL\$ 4, MKS\$ 4, MKD\$ 8. Used to store numbers in FIELD buffers; CVI, CVL, CVS and CVD convert them back.

See also: [CVI](#kw-cvi)

<a id="kw-mod"></a>

### MOD

```
numeric-expression1 MOD numeric-expression2
```

Remainder of an integer division. The operands are rounded to integers first; the result has the sign of the first one.

```
PRINT 17 MOD 5, -7 MOD 3     ' 2  -1
```

See also: [Operators](#topic-operators)

<a id="kw-mouse"></a>

### MOUSE *(QBLike extension)*

```
MOUSE x%, y%, buttons%   or   MOUSE ON | OFF
```

Reads the mouse into three numeric variables.

x% and y% get the pointer position on the visible screen in the current mode's resolution (0-319 by 0-199 in SCREEN 13 and SCREEN 1, 0-639 by 0-479 in SCREEN 12; text column and row, from 0, in SCREEN 0). They are -1, -1 when the pointer is outside the picture or the window. Coordinates are absolute: VIEW and WINDOW are not applied.

buttons%: bit 0 = left button, bit 1 = right button (1 left, 2 right, 3 both, 0 none).

MOUSE ON / MOUSE OFF show or hide the mouse cursor (an arrow in graphics modes, an inverted block in SCREEN 0). It is hidden when a program starts; MOUSE still reads the mouse while it is hidden. The cursor is overlaid on the displayed picture only, so POINT, GET and PEEK never see it.

```
DO
    MOUSE x%, y%, b%
    IF b% AND 1 THEN PSET (x%, y%), 15
LOOP UNTIL INKEY$ = CHR$(27)
```

See also: [MULTIKEY](#kw-multikey), [INKEY\$](#kw-inkey-s)

<a id="kw-multikey"></a>

### MULTIKEY *(QBLike extension)*

```
MULTIKEY(scancode%)
```

Returns -1 while the key with the given scan code is held down, 0 otherwise. Several keys can be checked at once, ideal for games.

Common scan codes: Esc 1, Enter 28, Space 57, Ctrl 29, Alt 56, Left Shift 42, Up 72, Down 80, Left 75, Right 77, A 30, D 32, S 31, W 17.

See also: [INKEY\$](#kw-inkey-s), [INP](#kw-inp)

<a id="kw-music"></a>

### MUSIC *(QBLike extension)*

```
MUSIC filespec$[, LOOP]
MUSIC STOP
MUSIC VOLUME volume%
MUSIC
```

Plays background music while the program goes on.

```
MUSIC file$        plays a MOD (ProTracker) or MID (MIDI, on the FM
                   synthesizer) file; with LOOP it starts again at the
                   end. A new MUSIC replaces the current music.
MUSIC STOP         stops it.
MUSIC VOLUME v%    0-255 (255 at start), also while playing.
MUSIC              as a function: -1 while music plays, 0 if not.

MUSIC "TITLE.MOD", LOOP
...
MUSIC "GAMEOVER.MID"
DO: LOOP WHILE MUSIC
```

MUSIC is not a reserved word: programs with their own variable or SUB called MUSIC keep working.

See also: [SOUND](#kw-sound), [PLAY](#kw-play)

<a id="kw-name"></a>

### NAME

```
NAME oldspec$ AS newspec$
```

Renames a file or moves it to another directory on the same drive.

```
NAME "OLD.TXT" AS "NEW.TXT"
```

See also: [KILL](#kw-kill)

<a id="kw-oct-s"></a>

### OCT$

```
OCT$(numeric-expression&)
```

Returns the octal representation of a number (rounded to an integer) as a string.

```
PRINT OCT$(8)       ' 10
```

See also: [HEX\$](#kw-hex-s)

<a id="kw-on-error"></a>

### ON ERROR

```
ON ERROR GOTO line
ON ERROR GOTO 0
...
RESUME [NEXT | line]
```

ON ERROR GOTO line turns on error trapping: when an error happens, the program jumps to the handler at line. ERR gives the error code and ERL the line number. The handler ends with RESUME. ON ERROR GOTO 0 turns trapping off (an error inside the handler then stops the program).

```
ON ERROR GOTO Handler
OPEN "DATA.TXT" FOR INPUT AS #1
...
Handler:
    IF ERR = 53 THEN PRINT "File not found": RESUME NEXT
    ON ERROR GOTO 0
```

See also: [RESUME](#kw-resume), [ERR](#kw-err), [ERROR](#kw-error)

<a id="kw-on-gosub"></a>

### ON GOSUB

```
ON expression% GOSUB line-list
ON expression% GOTO line-list
```

Jumps to the n-th label of the list, where n is the value of the expression. If it is 0 or greater than the number of labels, the program continues with the next statement.

```
ON choice% GOSUB NewGame, LoadGame, Quit
```

See also: [GOSUB](#kw-gosub), [GOTO](#kw-goto)

<a id="kw-on-play"></a>

### ON PLAY

```
ON PLAY(n%) GOSUB line
PLAY ON | OFF | STOP
```

Calls a subroutine when the background music queue drops below n% notes (1-32), so it can be refilled:

```
ON PLAY(2) GOSUB More: PLAY ON
PLAY "MB L8 CDEFG"
...
More: PLAY "MB EDC": RETURN
```

See also: [PLAY](#kw-play)

<a id="kw-on-timer"></a>

### ON TIMER

```
ON TIMER(n%) GOSUB line
TIMER ON | OFF | STOP
```

Calls a subroutine every n% seconds (1 to 86400) once TIMER ON has been executed. TIMER OFF disables the event; TIMER STOP delays it until TIMER ON. The event is checked between statements.

```
ON TIMER(1) GOSUB Clock: TIMER ON
DO: LOOP UNTIL INKEY$ <> ""
END
Clock: LOCATE 1, 70: PRINT TIME$: RETURN
```

See also: [TIMER](#kw-timer)

<a id="kw-open"></a>

### OPEN

```
OPEN file$ [FOR mode] [ACCESS access] [lock] AS [#]filenumber% [LEN=reclen%]

mode: APPEND, BINARY, INPUT, OUTPUT, RANDOM

OPEN mode$, [#]filenumber%, file$[, reclen%]
```

Opens a file. DOS-style names (C:\\DIR\\FILE.TXT) are accepted; on Linux the name is matched case-insensitively.

See also: [CLOSE](#kw-close), [GET](#kw-get), [PUT](#kw-put), [INPUT](#kw-input), [PRINT](#kw-print)

<a id="kw-option-base"></a>

### OPTION BASE

```
OPTION BASE {0 | 1}
```

Sets the default lower bound of arrays (0 or 1) for DIM statements that give only the upper bound. Must appear before any array is dimensioned.

See also: [DIM](#kw-dim)

<a id="kw-out"></a>

### OUT

```
OUT port%, data%
```

Emulated ports:

```
&H3C8  select palette index to write
&H3C9  write R, G, B (0-63) successively
&H3C7  select palette index to read
&H3C4  sequencer index; &H3C5 data (index 2 = planes written)
&H3CE  graphics index; &H3CF data (index 4 = plane read)
&H3D8  CGA mode control: in SCREEN 2, bit 2 = 0 turns the color
       burst on (composite artifact colors): OUT &H3D8, 26
&H3D9  CGA color select: in SCREEN 2, foreground color (0-15)
&H388  AdLib FM (OPL) register index; &H389 register data
       (also &H220/&H221; &H38A/&H38B = OPL3 second bank)

OUT &H3C8, 0: OUT &H3C9, 10: OUT &H3C9, 20: OUT &H3C9, 50
```

See also: [INP](#kw-inp), [PALETTE](#kw-palette), [SCREEN](#kw-screen), [DEF SEG](#kw-def-seg)

<a id="kw-paint"></a>

### PAINT

```
PAINT [STEP](x!,y!)[,[{color% | tile$}] [,[bordercolor%] [,background$]]]
```

Fills an area, starting at (x, y), with color% or with the tile pattern tile\$, until it reaches pixels of bordercolor% (by default the fill color). The area must be completely enclosed by that color. To fill regardless of the border colors, see [FILL](#kw-fill).

```
CIRCLE (160, 100), 50, 15
PAINT (160, 100), 4, 15
```

See also: [CIRCLE](#kw-circle), [LINE](#kw-line), [DRAW](#kw-draw)

<a id="kw-palette"></a>

### PALETTE

```
PALETTE [attribute%,color&]
PALETTE USING arrayname#[(index%)]
```

Changes colors in the palette. In SCREEN 12 and 13 color& is 65536 \* blue + 256 \* green + red (each 0-63).

See also: [COLOR](#kw-color), [OUT](#kw-out), [SCREEN](#kw-screen)

<a id="kw-pcopy"></a>

### PCOPY

```
PCOPY sourcepage%, destinationpage%
```

Copies one video page to another (SCREEN 0, 7, 8 and 9 have several pages). Used for flicker-free animation: draw on a hidden page and copy it to the visible one.

```
SCREEN 7, 0, 1, 0        ' draw on page 1, show page 0
... draw ...
PCOPY 1, 0
```

See also: [SCREEN](#kw-screen)

<a id="kw-peek"></a>

### PEEK

```
PEEK(address)
```

Returns the byte at the given offset of the segment set with DEF SEG. In QBLike it works on emulated video memory and some BIOS areas: see [DEF SEG](#kw-def-seg).

See also: [POKE](#kw-poke), [DEF SEG](#kw-def-seg)

<a id="kw-pen"></a>

### PEN

```
PEN(n%)
PEN ON | OFF | STOP
ON PEN GOSUB line
```

Light pen. PEN(n%) always returns 0 and the event never fires.

See also: [MOUSE](#kw-mouse)

<a id="kw-play"></a>

### PLAY

```
PLAY commandstring$
PLAY(n)
PLAY ON | OFF | STOP
```

Plays music described by a string of commands (upper or lower case, spaces ignored). Octave, length, tempo and mode stay in effect for the next PLAY.

```
A-G [#|+|-] [n]  a note; # or + sharp, - flat; n length (1-64)
N n              note number 0-84 (0 = rest); octave and length as set
O n              octave 0-6 (default 4); < and > go down / up one
L n              default length: 1 whole note, 4 quarter (default),
                 8 eighth... up to 64
P n              pause (rest) of length n
.                after a note or pause: 1.5 times its length (.. 1.75)
T n              tempo: quarter notes per minute, 32-255 (default 120)
MN ML MS         normal (7/8 of the length), legato, staccato (3/4)
MF MB            foreground (PLAY waits) or background (the program
                 goes on while up to 32 notes play)
X + VARPTR$(a$)  plays the commands in a$
```

Any number can be written as "=" + VARPTR\$(variable). PLAY(n) returns the number of notes still in the background queue. PLAY ON / OFF / STOP enable the ON PLAY event.

```
PLAY "T160 O3 L8 CDEFGAB > C4"
PLAY "MB L16 EGEG >C<G"          ' the program continues meanwhile
PLAY "T=" + VARPTR$(tempo%)
```

See also: [ON PLAY](#kw-on-play), [SOUND](#kw-sound), [BEEP](#kw-beep)

<a id="kw-pmap"></a>

### PMAP

```
PMAP(startcoordinate#, n%)
```

Maps between logical (WINDOW) and physical coordinates. n: 0 = x to physical, 1 = y to physical, 2 = x to logical, 3 = y to logical.

See also: [WINDOW](#kw-window), [VIEW](#kw-view)

<a id="kw-point"></a>

### POINT

```
POINT(x%,y%)
POINT(number%)
```

Returns a pixel color, or the graphics cursor position (0 = physical x, 1 = physical y, 2 = logical x, 3 = logical y).

See also: [PSET](#kw-pset)

<a id="kw-poke"></a>

### POKE

```
POKE address, byte%
```

Writes a byte to memory at the DEF SEG segment. Example (SCREEN 13): DEF SEG = &HA000: POKE y \* 320 + x, color

See also: [PEEK](#kw-peek), [DEF SEG](#kw-def-seg)

<a id="kw-pos"></a>

### POS

```
POS(0)
```

Returns the current column of the text cursor (1-based). The argument is ignored. CSRLIN returns the row.

See also: [CSRLIN](#kw-csrlin), [LOCATE](#kw-locate)

<a id="kw-print"></a>

### PRINT

```
PRINT [#filenumber%,] [expressionlist] [{; | ,}]
```

Writes data to the screen or a file. ; keeps the cursor after the last item, , moves to the next 14-column zone. Positive numbers are printed with a leading space and all numbers with a trailing space. ? is an abbreviation for PRINT.

See also: [PRINT USING](#kw-print-using), [WRITE](#kw-write), [LPRINT](#kw-lprint)

<a id="kw-print-using"></a>

### PRINT USING

```
PRINT [#filenumber%,] USING formatstring$; expressionlist [{; | ,}]
```

Prints formatted output.

```
#  digit      .  decimal point    ,  thousands      +  sign
-  trailing minus   $$  floating $    **  * fill     ^^^^  exponent
!  first char  \  \  n chars       &  whole string   _  literal
```

See also: [PRINT](#kw-print)

<a id="kw-pset"></a>

### PSET

```
PSET [STEP](x!,y!) [,color%]
PRESET [STEP](x!,y!) [,color%]
```

Draws a point at (x, y) in color% (default: the foreground color for PSET, the background color for PRESET). STEP makes the coordinates relative to the last point drawn.

```
FOR x% = 0 TO 319: PSET (x%, 100 + SIN(x% / 20) * 50), 10: NEXT
```

See also: [POINT](#kw-point), [LINE](#kw-line)

<a id="kw-put"></a>

### PUT

```
PUT [#]filenumber%[,[record&][,variable]]        (files)
PUT [STEP](x!,y!), arrayname[(index%)] [,actionverb]   (graphics)

actionverb: PSET, PRESET, AND, OR, XOR (default) or MASK
```

Writes to a file, or draws an image captured with GET. MASK (QBLike extension) skips pixels of color 0, so sprites can have transparent backgrounds: PUT (x%, y%), sprite%(0), MASK In QBLike PUT is clipped to the CLIP rectangle instead of failing when the image is partly or totally off-screen.

See also: [GET](#kw-get), [CLIP](#kw-clip)

<a id="kw-randomize"></a>

### RANDOMIZE

```
RANDOMIZE [seed%]
RANDOMIZE TIMER
```

Initializes the random-number generator so RND gives a different sequence. Without a seed it asks for one; RANDOMIZE TIMER uses the clock, which gives a different sequence each run.

See also: [RND](#kw-rnd)

<a id="kw-read"></a>

### READ

```
READ variablelist
```

Reads values from DATA statements into variables, in order. Reading past the last DATA gives the error Out of DATA. See [DATA](#kw-data).

See also: [DATA](#kw-data), [RESTORE](#kw-restore)

<a id="kw-redim"></a>

### REDIM

```
REDIM [PRESERVE] [SHARED] variable(subscripts) [AS type] [,...]
```

Changes the size of a dynamic array (and creates it if needed). The contents are erased, unless PRESERVE is used, which keeps the values (in memory order, so they stay in place when only the upper bound of the last dimension changes). The type and number of dimensions cannot change.

```
REDIM enemies(count%) AS EnemyType
REDIM PRESERVE scores%(1 TO n% + 10)
```

See also: [DIM](#kw-dim), [ERASE](#kw-erase)

<a id="kw-rem"></a>

### REM

```
REM remark
' remark
```

Adds a comment: the rest of the line is ignored. An apostrophe (') does the same and can follow a statement on the same line.

```
REM Main loop
x% = 0   ' reset the position
```

<a id="kw-reset"></a>

### RESET

```
RESET
```

Closes all open files.

See also: [CLOSE](#kw-close)

<a id="kw-restore"></a>

### RESTORE

```
RESTORE [line]
```

Makes the next READ start again from the first DATA statement, or from the first DATA after the given label.

```
RESTORE Level2: READ w%, h%
```

See also: [DATA](#kw-data), [READ](#kw-read)

<a id="kw-resume"></a>

### RESUME

```
RESUME [0]
RESUME NEXT
RESUME line
```

Ends an error handler (see ON ERROR):

```
RESUME or RESUME 0   runs the statement that caused the error again
RESUME NEXT          continues with the statement after it
RESUME line          continues at a label
```

See also: [ON ERROR](#kw-on-error)

<a id="kw-return"></a>

### RETURN

```
RETURN [line]
```

Returns from a GOSUB subroutine (or event handler such as ON TIMER) to the statement after the GOSUB, or to another label with RETURN line.

See also: [GOSUB](#kw-gosub)

<a id="kw-right-s"></a>

### RIGHT$

```
RIGHT$(stringexpression$,n%)
```

See LEFT\$.

See also: [LEFT\$](#kw-left-s)

<a id="kw-rmdir"></a>

### RMDIR

```
RMDIR pathspec$
```

Removes an empty directory.

See also: [MKDIR](#kw-mkdir)

<a id="kw-rnd"></a>

### RND

```
RND[(n#)]
```

Returns a single-precision random number between 0 and 1. The sequence is the same as in QBasic for the same seed.

See also: [RANDOMIZE](#kw-randomize)

<a id="kw-run"></a>

### RUN

```
RUN [line]
```

Restarts the current program from the beginning: variables are cleared and files closed. RUN with a line number or file name is not supported (Advanced feature unavailable).

See also: [END](#kw-end)

<a id="kw-screen"></a>

### SCREEN

```
SCREEN mode% [,[colorswitch%] [,[activepage%] [,visualpage%]]]
SCREEN FULL | WINDOW

SCREEN(row%, column% [,colorflag%])   (function)
```

Sets the screen mode.

```
0  text 80x25 (40x25, 80x43, 80x50 with WIDTH), 16 colors, 8 pages
1  320x200, 4 colors          2  640x200, 2 colors
3  160x200, 16 colors, 2 pages (PCjr/Tandy; QBLike extension)
7  320x200, 16 colors, 8 pages
8  640x200, 16 colors, 4 pages
9  640x350, 16 colors, 2 pages
11  640x480, 2 colors          12  640x480, 16 colors
13  320x200, 256 colors
```

SCREEN 2 with OUT &H3D8, 26 turns the CGA color burst on: composite artifact colors, each group of 4 pixels shown as one of 16 colors. OUT &H3D8, 30 turns it off again.

SCREEN 3 (PCjr/Tandy 1000): pixels are twice as wide as tall, 20x25 text. Memory at &HB800: 2 pixels per byte (high nibble = left), even lines at offset 0 and odd lines at &H2000, 80 bytes per line.

SCREEN FULL and SCREEN WINDOW (QBLike extension) switch QBLike to full screen or to a window, like Alt+Enter or F11. The screen mode and its contents don't change. FULL is not a reserved word: if the program has a variable called FULL, SCREEN full sets the mode in that variable.

The function SCREEN(row, col) returns the character code at a position.

See also: [COLOR](#kw-color), [PALETTE](#kw-palette), [PCOPY](#kw-pcopy), [WIDTH](#kw-width), [OUT](#kw-out)

<a id="kw-seek"></a>

### SEEK

```
SEEK [#]filenumber%, position&
SEEK(filenumber%)
```

SEEK statement: sets the position for the next read or write: the record number for RANDOM files, the byte position (from 1) for other files. SEEK function: returns the current position the same way.

See also: [LOC](#kw-loc)

<a id="kw-select"></a>

### SELECT

```
SELECT CASE testexpression
CASE expressionlist1
    [statementblock-1]
[CASE expressionlist2
    [statementblock-2]]...
[CASE ELSE
    [statementblock-n]]
END SELECT

expressionlist: expression, expression TO expression, IS relop expression
```

Executes the block of the first CASE that matches the value of the test expression; CASE ELSE runs if none matches.

```
SELECT CASE score%
    CASE 100: PRINT "Perfect"
    CASE 90 TO 99: PRINT "Great"
    CASE IS < 50, 77: PRINT "Hmm"
    CASE ELSE: PRINT "OK"
END SELECT
```

See also: [IF](#kw-if)

<a id="kw-sgn"></a>

### SGN

```
SGN(numeric-expression)
```

Returns -1 if the number is negative, 0 if it is zero and 1 if it is positive.

See also: [ABS](#kw-abs)

<a id="kw-shared"></a>

### SHARED

```
SHARED variable[()] [AS type] [,variable[()] [AS type]]...
```

Inside a SUB or FUNCTION, gives access to module-level variables without declaring them DIM SHARED in the module.

```
SUB AddPoints (n%)
    SHARED score&
    score& = score& + n%
END SUB
```

See also: [DIM](#kw-dim), [STATIC](#kw-static)

<a id="kw-shell"></a>

### SHELL

```
SHELL [commandstring$]
```

Runs an operating system command (cmd.exe on Windows, the shell on Linux) and waits until it finishes. SHELL alone does nothing in QBLike.

<a id="kw-sin"></a>

### SIN

```
SIN(angle)
```

Returns the sine of an angle given in radians (degrees \* PI / 180).

```
x = 160 + COS(a) * r: y = 100 + SIN(a) * r
```

See also: [COS](#kw-cos), [TAN](#kw-tan), [ATN](#kw-atn)

<a id="kw-sleep"></a>

### SLEEP

```
SLEEP [seconds&]
```

Suspends execution until a key is pressed or the given number of seconds has passed. SLEEP or SLEEP 0 waits only for a key. The key stays in the keyboard buffer (read it with INKEY\$).

See also: [INKEY\$](#kw-inkey-s)

<a id="kw-sound"></a>

### SOUND

```
SOUND frequency, duration
id% = SOUND filespec$
SOUND PLAY id%[, channel%][, volume%][, pan%][, LOOP]
SOUND STOP [channel%]
```

SOUND frequency, duration generates a tone. Duration is in clock ticks (18.2 per second).

QBLike extension, sound effects from WAV files (mono or stereo, 4000-48000 Hz):

```
id% = SOUND file$   loads the file and returns its number (loading
                    the same file again returns the same number)
SOUND PLAY          plays it on a channel (1-12; omitted = a free one).
                    volume% 0-255 (255), pan% -100 left to 100 right
                    (0), LOOP repeats it until SOUND STOP. Playing on
                    a busy channel replaces what was playing there.
SOUND STOP          stops a channel, or all of them without channel%.

boom% = SOUND "BOOM.WAV"
SOUND PLAY boom%, , 200, -60
```

See also: [PLAY](#kw-play), [BEEP](#kw-beep), [MUSIC](#kw-music)

<a id="kw-space-s"></a>

### SPACE$

```
SPACE$(n%)
```

Returns a string of n% spaces.

```
PRINT "A"; SPACE$(5); "B"
```

See also: [STRING\$](#kw-string-s), [SPC](#kw-spc)

<a id="kw-spc"></a>

### SPC

```
SPC(n%)
```

Used in PRINT: skips n% spaces.

```
PRINT "A"; SPC(10); "B"
```

See also: [TAB](#kw-tab), [PRINT](#kw-print)

<a id="kw-sqr"></a>

### SQR

```
SQR(numeric-expression)
```

Returns the square root of a number (Illegal function call if it is negative).

```
d = SQR((x2 - x1) ^ 2 + (y2 - y1) ^ 2)
```

<a id="kw-static"></a>

### STATIC

```
STATIC variablelist
```

Inside a SUB or FUNCTION, makes the listed variables keep their values between calls (they are not reset on each call). SUB name STATIC does this for all local variables.

```
SUB Counter
    STATIC calls%
    calls% = calls% + 1: PRINT calls%
END SUB
```

See also: [SHARED](#kw-shared), [SUB](#kw-sub)

<a id="kw-stick"></a>

### STICK

```
STICK(n%)
STRIG(n%)
STRIG(n%) ON | OFF | STOP
ON STRIG(n%) GOSUB line
```

Joystick position and buttons. There is no joystick support: STICK and STRIG return 0 and the events never fire.

See also: [MOUSE](#kw-mouse), [MULTIKEY](#kw-multikey)

<a id="kw-stop"></a>

### STOP

```
STOP
```

Halts the program and returns to the editor (like a breakpoint).

See also: [END](#kw-end)

<a id="kw-str-s"></a>

### STR$

```
STR$(numeric-expression)
```

Returns a number as a string, as PRINT would show it: with a leading space for positive numbers (no trailing space).

```
PRINT "[" + STR$(42) + "]"     ' [ 42]
```

See also: [VAL](#kw-val)

<a id="kw-string-s"></a>

### STRING$

```
STRING$(length%,{ascii-code% | stringexpression$})
```

Returns a string of length% copies of a character, given by its ASCII code or as the first character of a string.

```
PRINT STRING$(40, "-"); STRING$(3, 3)
```

See also: [SPACE\$](#kw-space-s)

<a id="kw-sub"></a>

### SUB

```
SUB name[(parameterlist)] [STATIC]
    [statementblock]
END SUB
```

Defines a SUB procedure. Type SUB name in the editor (or use Edit \> New SUB) and it gets its own window; press F2 to switch.

See also: [CALL](#kw-call), [FUNCTION](#kw-function), [SHARED](#kw-shared), [STATIC](#kw-static)

<a id="kw-swap"></a>

### SWAP

```
SWAP variable1, variable2
```

Exchanges the values of two variables or array elements of the same type.

```
IF a% > b% THEN SWAP a%, b%
```

<a id="kw-system"></a>

### SYSTEM

```
SYSTEM
```

Ends the program (and closes QBLike when run with /X).

See also: [END](#kw-end)

<a id="kw-tab"></a>

### TAB

```
TAB(column%)
```

Used in PRINT: moves to column column% (to the next line if the cursor is already past it).

```
PRINT "Name"; TAB(20); "Score"
```

See also: [SPC](#kw-spc), [PRINT](#kw-print)

<a id="kw-tan"></a>

### TAN

```
TAN(angle)
```

Returns the tangent of an angle given in radians.

See also: [SIN](#kw-sin), [COS](#kw-cos), [ATN](#kw-atn)

<a id="kw-time-s"></a>

### TIME$

```
TIME$
```

Returns the current time as "hh:mm:ss" (24-hour clock). (Setting the time with TIME\$ = ... is accepted and ignored.)

See also: [DATE\$](#kw-date-s), [TIMER](#kw-timer)

<a id="kw-timer"></a>

### TIMER

```
TIMER
TIMER ON | OFF | STOP
```

TIMER returns the number of seconds since midnight, with fractions (accurate to milliseconds in QBLike). TIMER ON / OFF / STOP control the ON TIMER event.

```
t! = TIMER: DO: LOOP WHILE TIMER < t! + .5    ' wait half a second
```

See also: [ON TIMER](#kw-on-timer), [TIME\$](#kw-time-s)

<a id="kw-tron"></a>

### TRON

```
TRON
TROFF
```

TRON / TROFF turned the line-number trace on and off. Accepted for compatibility; use F8 (step) or Debug \> Trace On in the editor.

<a id="kw-type"></a>

### TYPE

```
TYPE usertype
    elementname AS typename
    ...
END TYPE
```

Defines a record data type. Elements can be INTEGER, LONG, SINGLE, DOUBLE, STRING \* n or another TYPE.

See also: [DIM](#kw-dim)

<a id="kw-ubound"></a>

### UBOUND

```
UBOUND(array[,dimension%])
```

See LBOUND.

See also: [LBOUND](#kw-lbound)

<a id="kw-ucase-s"></a>

### UCASE$

```
UCASE$(stringexpression$)
```

See LCASE\$.

See also: [LCASE\$](#kw-lcase-s)

<a id="kw-val"></a>

### VAL

```
VAL(stringexpression$)
```

Converts a string to a number, reading up to the first character that is not part of a number. Blanks are ignored; &H (hexadecimal) and &O (octal) prefixes are accepted. Returns 0 if there is no number.

```
PRINT VAL("12abc"), VAL("&HFF")     ' 12   255
```

See also: [STR\$](#kw-str-s)

<a id="kw-varptr"></a>

### VARPTR

```
VARPTR(variable)
VARSEG(variable)
```

In QBasic, return the offset and the segment of a variable in memory. QBLike has no real-mode memory, so both return 0, with one exception: VARPTR(array(index)) written directly as the offset of BLOAD or BSAVE loads or saves the array's raw bytes from that element:

```
DEF SEG = VARSEG(ship%(0))       ' classic form; optional in QBLike
BLOAD "SHIP.SPR", VARPTR(ship%(0))
DEF SEG
```

See also: [BLOAD](#kw-bload), [BSAVE](#kw-bsave), [DEF SEG](#kw-def-seg)

<a id="kw-varptr-s"></a>

### VARPTR$

```
VARPTR$(variable)
```

Returns a 3-character reference to a variable, used inside the PLAY and DRAW command strings:

```
DRAW "R=" + VARPTR$(n%) + "X" + VARPTR$(shape$)
PLAY "P=" + VARPTR$(p%)
```

See also: [PLAY](#kw-play), [DRAW](#kw-draw), [VARPTR](#kw-varptr)

<a id="kw-varseg"></a>

### VARSEG

```
VARSEG(variable)
```

See VARPTR.

See also: [VARPTR](#kw-varptr)

<a id="kw-view"></a>

### VIEW

```
VIEW [[SCREEN] (x1!,y1!)-(x2!,y2!) [,[color%] [,border%]]]
```

Defines a rectangle of the screen as the graphics viewport: drawing is clipped to it. Without SCREEN, coordinates are relative to the viewport's corner; with SCREEN, they stay absolute. color% fills it, border% draws a frame. VIEW alone restores the whole screen.

See also: [WINDOW](#kw-window), [VIEW PRINT](#kw-view-print), [CLS](#kw-cls)

<a id="kw-view-print"></a>

### VIEW PRINT

```
VIEW PRINT [toprow% TO bottomrow%]
```

Limits text output and scrolling to the rows toprow% to bottomrow%. VIEW PRINT alone restores the whole screen. CLS 2 clears the area.

```
VIEW PRINT 3 TO 22
```

See also: [VIEW](#kw-view), [CLS](#kw-cls)

<a id="kw-wait"></a>

### WAIT

```
WAIT &H3DA, 8
WAIT VSYNC
```

Waits for the start of the next vertical retrace (emulated at 70 Hz, like a VGA card) and shows what has been drawn, so animation doesn't flicker. Other ports are ignored.

WAIT VSYNC is a QBLike alias of WAIT &H3DA, 8. VSYNC is not a reserved word: programs with their own SUB VSync keep working.

```
DO
    WAIT VSYNC
    PUT (x, y), sprite, PSET
LOOP
```

See also: [INP](#kw-inp)

<a id="kw-while"></a>

### WHILE

```
WHILE condition
    [statementblock]
WEND
```

Repeats the block while the condition is true (checked at the start). DO...LOOP is more flexible (EXIT DO, condition at the end).

```
WHILE INKEY$ = "": WEND
```

See also: [DO](#kw-do)

<a id="kw-width"></a>

### WIDTH

```
WIDTH [columns%] [,rows%]
```

Changes the text screen size (40/80 columns; 25, 43 or 50 rows in SCREEN 0; 30 or 60 rows in SCREEN 12; 20 columns in SCREEN 3).

See also: [SCREEN](#kw-screen)

<a id="kw-window"></a>

### WINDOW

```
WINDOW [[SCREEN] (x1!,y1!)-(x2!,y2!)]
```

Defines logical coordinates for graphics: (x1, y1) and (x2, y2) are the corners of the viewport. Without SCREEN, y grows upwards (like in mathematics); WINDOW SCREEN keeps y growing downwards. WINDOW alone returns to pixel coordinates. PMAP converts between both.

```
WINDOW (-10, -10)-(10, 10)
LINE (-10, 0)-(10, 0): LINE (0, -10)-(0, 10)
```

See also: [VIEW](#kw-view), [PMAP](#kw-pmap)

<a id="kw-write"></a>

### WRITE

```
WRITE [#filenumber%,] expressionlist
```

Writes values separated by commas, with strings in quotes, so INPUT can read them back exactly.

```
WRITE #1, name$, age%, score!      ' "Ann",31,1500
```

See also: [PRINT](#kw-print), [INPUT](#kw-input)

## The editor

<a id="topic-editing-keys"></a>

### Editing Keys

The editor follows modern conventions:

```
Arrows, Home, End, PgUp, PgDn    move           Ctrl+Left/Right   word by word
Ctrl+Home / Ctrl+End             start / end    Ctrl+Up/Down      scroll
Shift + any movement key         select text    Ctrl+A            select all
Mouse drag, double click         select         Shift+click       extend selection
Ctrl+C  Ctrl+X  Ctrl+V           copy, cut, paste (system clipboard;
                                 without a selection: the whole line)
Ctrl+Ins  Shift+Del  Shift+Ins   copy, cut, paste (classic keys)
Ctrl+Z  Ctrl+Y                   undo, redo
Ctrl+Backspace  Ctrl+Del         delete word
Tab / Shift+Tab                  indent / unindent (also selected lines)
Left at a tab stop in spaces     moves back a whole tab (Options > Display)
Ctrl+Shift+F                     Edit > Format Code (see below)
Insert                           toggle overwrite mode
Ctrl+S  Ctrl+O  Ctrl+N           save, open, new
Ctrl+F  Ctrl+H  F3               find, change, find next
Ctrl+G                           go to line
F2                               list of SUBs / FUNCTIONs
Shift+F2 / Ctrl+F2               next / previous procedure
F6                               switch to/from the Immediate window
Alt (tap) or Alt+letter          menus (holding Alt shows the shortcuts)
```

When you leave a line, keywords are capitalised, spacing is tidied and the line is checked for syntax errors, like in QBasic.

Edit \> Format Code reformats the selected lines, or the whole view (the module or the SUB/FUNCTION shown) if nothing is selected, after asking: statements joined with ':' go on separate lines, single-line IFs with several statements become block IFs, and blocks are re-indented by the tab size. Strings, comments, DATA, labels and line numbers are kept. Ctrl+Z undoes it.

See also: [Debugging](#topic-debugging), [Contents](#contents)

<a id="topic-debugging"></a>

### Running and Debugging

```
Shift+F5   Start the program from the beginning
F5         Continue (or start)
F8         Step: execute the next statement (enters procedures)
F10        Procedure step: step over SUB/FUNCTION calls
F7         Run to the cursor line
F9         Toggle breakpoint on the current line
F4         Show the output screen
Ctrl+Break  stop a running program (also Ctrl+Alt+Esc, for keyboards
           without Break, and Ctrl+Pause / Ctrl+Scroll Lock)
```

When a program is stopped, the editor shows the statement where it stopped. F5 continues from there, F8/F10 step. During INPUT it stops at the INPUT, which asks again when you continue. If you edit the program, it has to be restarted.

While stopped you can inspect variables in the Immediate window (F6), e.g. PRINT x, and use Debug \> Set Next Statement.

See also: [Editing Keys](#topic-editing-keys), [STOP](#kw-stop)

<a id="topic-menus"></a>

### Menus

Press Alt to activate the menu bar, then use the arrows and Enter, or press Alt plus the highlighted letter. You can also click with the mouse.

See also: [Editing Keys](#topic-editing-keys), [Debugging](#topic-debugging)

<a id="topic-using-help"></a>

### Using Help

```
F1          Help on the keyword under the cursor
Shift+F1    This screen
Tab         Move to the next link (shown as Index)
Enter       Follow the selected link (or click it with the mouse)
Alt+F1      Go back to the previous topic
PgUp/PgDn   Scroll
Esc         Close Help
```

See also: [Contents](#contents), [Index](#index)
