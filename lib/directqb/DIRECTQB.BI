'-----------------------------------------------------------------------------
' DIRECTQB.BI
'   DirectQB 1.61 compatibility layer for QBLike
'
' A replacement for the DirectQB library by Angelo Mottola (Enhanced Creations
' 1998-99), written in plain QBLike BASIC. It implements every SUB and FUNCTION
' of DirectQB 1.61 with the same names, parameters and results, so a game
' written for DirectQB runs in QBLike by keeping its
'
'     '$INCLUDE: 'DIRECTQB.BI'
'
' line at the top of the program (no .LIB / .QLB needed). Include it before
' any other code, as the original manual says.
'
' How the hardware is emulated:
'   * Layers are SCREEN 13 pages: VIDEO is page 0, layers 1..n follow, and the
'     base layers B0-B9 come after them. Up to 53 layers are allowed.
'   * Sprites keep the GET/PUT format, in normal QB arrays passed with
'     VARSEG/VARPTR, exactly as with the original library.
'   * DQBmapLayer returns &HA000 and makes that layer the active page, so
'     DEF SEG = DQBmapLayer(n): POKE ... writes on layer n.
'   * EMS data memory (DQBpeek/DQBpoke) lives in an array.
'   * Sound uses QBLike's SOUND engine (12 channels, any sample rate).
'   * There is no joystick: DQBjoyDetected always returns FALSE.
' Read lib/directqb/README.md for the full list of differences.
'
' Notes for programmers:
'   * DirectQB calls that read sprite data leave DEF SEG at its default.
'   * Every name used internally starts with "dqb" (lower case) to avoid
'     clashes with your program.
'-----------------------------------------------------------------------------

'$DYNAMIC
DEFINT A-Z

' Library constants (the same as in the original DIRECTQB.BI)
CONST FALSE = 0, TRUE = NOT FALSE, VIDEO = 0
CONST B0 = &H8000, B1 = &H8001, B2 = &H8002, B3 = &H8003, B4 = &H8004
CONST B5 = &H8005, B6 = &H8006, B7 = &H8007, B8 = &H8008, B9 = &H8009
CONST BSV = 0, BMP = 1, PCX = 2
CONST HOR = 1, VER = 2, BOX = 0, PIXEL = 1, BIT.AND = 1, BIT.OR = 2, BIT.XOR = 3
CONST FORWARD = 0, BACKWARD = 1
CONST CENTERED = &H8000, NONE = 0, SOLID = &H1, BOLD = &H2, ITALIC = &H4
CONST UNDERLINED = &H8, BLENDED = &H10, TEXTURED = &H20
CONST ATTRIB.R = &H1, ATTRIB.H = &H2, ATTRIB.S = &H4
CONST ATTRIB.L = &H8, ATTRIB.D = &H10, ATTRIB.A = &H20
CONST KEYANY = -1, KEYESC = 1, KEYENTER = 28, KEYSPACE = 57
CONST KEYUP = 72, KEYDOWN = 80, KEYLEFT = 75, KEYRIGHT = 77
CONST UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3
CONST JOY1 = 0, JOY2 = 1, GAMEPAD = 2
CONST BUTA = 0, BUTB = 1, BUTC = 2, BUTD = 3
CONST AUTO = -1, ONCE = 0, LOOPED = 1

' ---- internal state ----
' library / layers
DIM SHARED dqbOn, dqbErr, dqbLayers, dqbVGA, dqbVGAon, dqbML, dqbMapPg
DIM SHARED dqbBase(9)
DIM SHARED dqbData(0) AS INTEGER, dqbDataLen AS LONG
' clipping box (dqbCF: the box is the whole screen) and put mode
DIM SHARED dqbCX1, dqbCY1, dqbCX2, dqbCY2, dqbCF, dqbSolid, dqbColM
' scratch image buffers: full screen, work surface, one column, one row
DIM SHARED dqbBuf(32002), dqbTmp(32002), dqbCol(1030), dqbRow(1100)
' timer
DIM SHARED dqbFPer AS DOUBLE, dqbFNext AS DOUBLE, dqbFOn
' fonts
DIM SHARED dqbFontOK, dqbFont(2047), dqbCLen(256), dqbFS(2047) AS LONG
DIM SHARED dqbTMode, dqbTBack, dqbTSpace, dqbTBMap, dqbTex(63)
' blender maps
DIM SHARED dqbBMOn(10), dqbBMF(10), dqbBML(10), dqbBM(0) AS INTEGER
' keyboard
DIM SHARED dqbKeyOn, dqbKM AS STRING
' mouse
DIM SHARED dqbMX, dqbMY, dqbMB, dqbMOX, dqbMOY, dqbMX1, dqbMY1, dqbMX2, dqbMY2, dqbMOn
' sound
DIM SHARED dqbNSnd, dqbSB, dqbVoices, dqbMVol, dqbSndId(0), dqbSndLen(0) AS LONG
DIM SHARED dqbVVol(32), dqbTmpN
' datafile
DIM SHARED dqbPkOn, dqbPkF, dqbPkN, dqbPkPw AS STRING, dqbPkPP, dqbPkRep, dqbPkLast
DIM SHARED dqbPkBuf AS STRING, dqbPkPtr, dqbPkOff(255) AS LONG
' FLI player
DIM SHARED dqbFliOn, dqbFliF, dqbFliFrames, dqbFliSpeed, dqbFliCur, dqbFliW, dqbFliH
DIM SHARED dqbFliPos1 AS LONG, dqbFliPos AS LONG
' 3D
DIM SHARED dqbTexW

dqbCX2 = 319: dqbCY2 = 199: dqbCF = -1: dqbTBMap = 1: dqbTexW = 64
dqbMX2 = 319: dqbMY2 = 199: dqbMVol = 15
dqbErr = 29

'=============================================================================
' Internal helpers
'=============================================================================

' signed 16-bit value of a LONG (0..65535 -> -32768..32767)
FUNCTION dqbS16% (v&)
  v2& = v& AND &HFFFF&
  IF v2& > 32767 THEN dqbS16% = v2& - 65536 ELSE dqbS16% = v2&
END FUNCTION

' unsigned value of an INTEGER (VARPTR offsets, lengths passed BYVAL)
FUNCTION dqbU16& (v)
  dqbU16& = v AND &HFFFF&
END FUNCTION

' page of a layer: VIDEO 0, layers 1..n, base layers after them
FUNCTION dqbPage (Layer)
  IF Layer > 0 THEN
    IF Layer <= dqbLayers THEN dqbPage = Layer
  ELSEIF Layer < 0 THEN
    b = Layer AND &H7FFF
    IF b < 10 THEN
      IF dqbBase(b) THEN dqbPage = dqbLayers + 1 + b
    END IF
  END IF
END FUNCTION

' makes sure the screen is in SCREEN 13 (layers are its pages)
SUB dqbVGAchk
  DEF SEG = 0: m = PEEK(&H449): DEF SEG
  IF m <> &H13 THEN SCREEN 13: dqbVGAon = -1
  dqbVGA = -1
  dqbClipBack
END SUB

' selects the page of a layer for drawing / goes back to the mapped one
SUB dqbUse (Layer)
  IF dqbVGA = 0 THEN dqbVGAchk
  SCREEN , , dqbPage(Layer)
END SUB

SUB dqbDone
  SCREEN , , dqbMapPg
END SUB

' QBLike CLIP rectangle = DirectQB clipping box (used by PUT)
SUB dqbClipBack
  IF dqbVGA = 0 THEN EXIT SUB
  IF dqbCF THEN CLIP ELSE CLIP dqbCX1, dqbCY1, dqbCX2, dqbCY2
END SUB

SUB dqbClipAll
  IF dqbCF = 0 THEN CLIP
END SUB

' VIEW SCREEN = clipping box, for PSET/LINE/CIRCLE/FILL
SUB dqbView
  IF dqbCF THEN EXIT SUB
  a = dqbCX1: b = dqbCY1: c = dqbCX2: d = dqbCY2
  IF a < 0 THEN a = 0
  IF b < 0 THEN b = 0
  IF c > 319 THEN c = 319
  IF d > 199 THEN d = 199
  IF c < a THEN c = a
  IF d < b THEN d = b
  VIEW SCREEN (a, b)-(c, d)
END SUB

SUB dqbNoView
  IF dqbCF = 0 THEN VIEW
END SUB

' width and height of a sprite at seg:off; leaves DEF SEG = seg
SUB dqbHdr (s, o, w, h)
  DEF SEG = s
  oo& = o AND &HFFFF&
  w = (PEEK(oo&) + PEEK(oo& + 1) * 256&) \ 8
  h = PEEK(oo& + 2) + PEEK(oo& + 3) * 256&
END SUB

' a blender map applied to a pair of colors
FUNCTION dqbBlend (BMap, fore, back)
  dqbBlend = fore
  IF BMap < 1 OR BMap > 10 THEN EXIT FUNCTION
  IF dqbBMOn(BMap) = 0 THEN EXIT FUNCTION
  IF fore < dqbBMF(BMap) OR fore > dqbBML(BMap) THEN EXIT FUNCTION
  dqbBlend = dqbBM((BMap - 1) * 65536 + (fore - dqbBMF(BMap)) * 256& + back)
END FUNCTION

' ok = TRUE if the file exists
SUB dqbFileExists (f$, ok)
  ok = 0
  IF LEN(f$) = 0 THEN EXIT SUB
  IF INSTR(f$, "*") OR INSTR(f$, "?") THEN EXIT SUB
  IF DIR$(f$) <> "" THEN ok = -1
END SUB

' a name for a temporary file
FUNCTION dqbTempName$ (ext$)
  d$ = ENVIRON$("TEMP")
  IF d$ = "" THEN d$ = ENVIRON$("TMP")
  IF d$ = "" THEN d$ = ENVIRON$("TMPDIR")
  IF d$ = "" THEN
    IF LEFT$(CURDIR$, 1) = "/" THEN d$ = "/tmp" ELSE d$ = "."
  END IF
  sep$ = "/"
  IF INSTR(d$, "\") THEN sep$ = "\"
  IF RIGHT$(d$, 1) = sep$ THEN d$ = LEFT$(d$, LEN(d$) - 1)
  dqbTmpN = dqbTmpN + 1
  t& = TIMER * 100
  dqbTempName$ = d$ + sep$ + "DQB" + LTRIM$(STR$(t& MOD 100000)) + "_" + LTRIM$(STR$(dqbTmpN)) + ext$
END FUNCTION

'=============================================================================
' MAIN module
'=============================================================================

FUNCTION DQBinit (NumLayers, NumSounds, MemSize)
  IF dqbOn THEN dqbErr = 0: DQBinit = 4: EXIT FUNCTION
  IF NumLayers < 0 OR NumLayers > 53 OR NumSounds < 0 OR NumSounds > 255 THEN
    dqbErr = 3: DQBinit = 3: EXIT FUNCTION
  END IF
  dqbLayers = NumLayers
  FOR i = 0 TO 9: dqbBase(i) = 0: NEXT
  dqbML = 0: dqbMapPg = 0
  IF MemSize <> 0 THEN
    dqbDataLen = dqbU16&(MemSize) * 1024&
    REDIM dqbData(0 TO dqbDataLen - 1) AS INTEGER
  ELSE
    dqbDataLen = 0
  END IF
  dqbNSnd = NumSounds
  IF NumSounds > 0 THEN
    REDIM dqbSndId(1 TO NumSounds), dqbSndLen(1 TO NumSounds) AS LONG
  END IF
  FOR i = 0 TO 32: dqbVVol(i) = 63: NEXT
  dqbFontOK = 0
  ' layers start cleared (pages that already exist are cleared now)
  DEF SEG = 0: m = PEEK(&H449): DEF SEG
  IF m = &H13 THEN
    FOR i = 1 TO NumLayers
      SCREEN , , i: LINE (0, 0)-(319, 199), 0, BF
    NEXT
    SCREEN , , 0
  END IF
  dqbMX1 = 0: dqbMY1 = 0: dqbMX2 = 319: dqbMY2 = 199: dqbMOX = 0: dqbMOY = 0
  dqbOn = -1
  dqbErr = 29
  DQBinit = 0
END FUNCTION

FUNCTION DQBver
  DQBver = &H13D
END FUNCTION

FUNCTION DQBid$
  DQBid$ = "DirectQB v1.61 by Angelo Mottola - Enhanced Creations 1998-99"
END FUNCTION

FUNCTION DQBmapLayer (Layer)
  dqbUse Layer
  dqbML = Layer
  dqbMapPg = dqbPage(Layer)
  DQBmapLayer = &HA000
END FUNCTION

FUNCTION DQBerror$
  SELECT CASE dqbErr
  CASE 0: e$ = "Library already initialized"
  CASE 1: e$ = "386 or better CPU not found"
  CASE 2: e$ = "No EMM driver detected"
  CASE 3: e$ = "Not enough free EMS memory"
  CASE 4: e$ = "Unable to open file"
  CASE 5: e$ = "General file reading error"
  CASE 6: e$ = "Blender map already allocated"
  CASE 7: e$ = "Not enough free base memory"
  CASE 8: e$ = "Blender map is not active"
  CASE 9: e$ = "Cannot create file"
  CASE 10: e$ = "General file writing error"
  CASE 11: e$ = "Unknown or bad file format"
  CASE 12: e$ = "No sounds initialized"
  CASE 13: e$ = "DMA channel not supported"
  CASE 14: e$ = "Failed to reset sound card DSP"
  CASE 15: e$ = "High mixing speed not supported"
  CASE 16: e$ = "Old sound card not supported"
  CASE 17: e$ = "File format not supported"
  CASE 18: e$ = "Sound exceed 64k size limit"
  CASE 19: e$ = "Unknown sound slot"
  CASE 20: e$ = "Cannot read past end of file"
  CASE 21: e$ = "Can't open two FLI files at once"
  CASE 22: e$ = "Incompatible blender map"
  CASE 23: e$ = "BLASTER variable not set"
  CASE 24: e$ = "Sound engine not yet initialized"
  CASE 25: e$ = "Datafile not yet opened"
  CASE 26: e$ = "Unknown packet ID"
  CASE 27: e$ = "Bad password or corrupted data"
  CASE 28: e$ = "Can't open two datafiles at once"
  CASE ELSE: e$ = "Operation successful"
  END SELECT
  DQBerror$ = e$
END FUNCTION

SUB DQBclose
  IF dqbOn = 0 THEN EXIT SUB
  IF dqbFOn THEN dqbFOn = 0
  IF dqbKeyOn THEN DQBremoveKeyboard
  IF dqbSB THEN DQBremoveSB
  FOR i = 1 TO 10: dqbBMOn(i) = 0: NEXT
  IF dqbFliOn THEN DQBcloseFLI
  IF dqbPkOn THEN DQBcloseDataFile
  FOR i = 0 TO 9: dqbBase(i) = 0: NEXT
  dqbML = 0: dqbMapPg = 0
  IF dqbVGAon THEN
    SCREEN 0: WIDTH 80, 25
    dqbVGAon = 0
  END IF
  dqbVGA = 0
  dqbOn = 0
END SUB

' copies Length bytes from the EMS data area to memory
SUB DQBpeek (DataSeg, DataOff, Offset AS LONG, Length)
  n& = dqbU16&(Length): o& = dqbU16&(DataOff)
  DEF SEG = DataSeg
  FOR i& = 0 TO n& - 1
    p& = Offset + i&
    IF p& >= 0 AND p& < dqbDataLen THEN POKE o& + i&, dqbData(p&)
  NEXT
  DEF SEG
END SUB

' copies Length bytes from memory to the EMS data area
SUB DQBpoke (DataSeg, DataOff, Offset AS LONG, Length)
  n& = dqbU16&(Length): o& = dqbU16&(DataOff)
  DEF SEG = DataSeg
  FOR i& = 0 TO n& - 1
    p& = Offset + i&
    IF p& >= 0 AND p& < dqbDataLen THEN dqbData(p&) = PEEK(o& + i&)
  NEXT
  DEF SEG
END SUB

' sorts NumRecords records of RecordLen bytes by the INTEGER at IndexOff
SUB DQBsort (ArraySeg, ArrayOff, NumRecords, RecordLen, IndexOff)
  IF NumRecords < 2 OR RecordLen < 1 THEN EXIT SUB
  DEF SEG = ArraySeg
  b& = dqbU16&(ArrayOff)
  rl& = RecordLen
  FOR i = 0 TO NumRecords - 2
    best = i
    pa& = b& + i * rl& + IndexOff
    kb& = PEEK(pa&) + PEEK(pa& + 1) * 256&: IF kb& > 32767 THEN kb& = kb& - 65536
    FOR j = i + 1 TO NumRecords - 1
      pa& = b& + j * rl& + IndexOff
      k& = PEEK(pa&) + PEEK(pa& + 1) * 256&: IF k& > 32767 THEN k& = k& - 65536
      IF k& < kb& THEN kb& = k&: best = j
    NEXT
    IF best <> i THEN
      p1& = b& + i * rl&: p2& = b& + best * rl&
      FOR k = 0 TO RecordLen - 1
        t = PEEK(p1& + k): POKE p1& + k, PEEK(p2& + k): POKE p2& + k, t
      NEXT
    END IF
  NEXT
  DEF SEG
END SUB

' angle 0-255 of the line from (x1,y1) to (x2,y2)
FUNCTION DQBangle (x1, y1, x2, y2)
  IF x1 = x2 THEN
    IF y1 < y2 THEN DQBangle = 127 ELSE DQBangle = 0
    EXIT FUNCTION
  END IF
  r& = CLNG(ATN((y2 - y1) / (x2 - x1)) / 3.141592653589793# * 128) + 64
  IF x2 < x1 THEN r& = r& + 128
  DQBangle = r& AND 255
END FUNCTION

SUB DQBinitVGA
  DEF SEG = 0: m = PEEK(&H449): DEF SEG
  IF m = &H13 THEN
    ' already in SCREEN 13 (layers may be in use): clear the screen only
    SCREEN , , 0, 0
    VIEW: CLS
    PALETTE
  ELSE
    SCREEN 13
  END IF
  dqbML = 0: dqbMapPg = 0
  dqbVGA = -1: dqbVGAon = -1
  dqbClipBack
  DQBresetMouse
END SUB

SUB DQBinitText
  SCREEN 0: WIDTH 80, 25
  dqbVGA = 0: dqbVGAon = 0
  dqbML = 0: dqbMapPg = 0
END SUB

FUNCTION DQBsetBaseLayer (Layer)
  b = Layer AND &H7FFF
  IF b >= 10 THEN dqbErr = 7: DQBsetBaseLayer = 0: EXIT FUNCTION
  IF dqbBase(b) OR dqbLayers + 1 + b > 63 THEN dqbErr = 7: DQBsetBaseLayer = 0: EXIT FUNCTION
  dqbBase(b) = -1
  IF dqbVGA = 0 THEN dqbVGAchk
  SCREEN , , dqbLayers + 1 + b
  LINE (0, 0)-(319, 199), 0, BF
  dqbDone
  DQBsetBaseLayer = &HA000
END FUNCTION

SUB DQBcopyLayer (SourceLayer, DestLayer)
  IF SourceLayer = DestLayer THEN EXIT SUB
  IF dqbVGA = 0 THEN dqbVGAchk
  PCOPY dqbPage(SourceLayer), dqbPage(DestLayer)
END SUB

SUB DQBclearLayer (Layer)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  LINE (0, 0)-(319, 199), 0, BF
  IF Layer <> dqbML THEN dqbDone
END SUB

' waits for the vertical retrace Times times
SUB DQBwait (Times)
  FOR i = 1 TO Times
    WAIT VSYNC
  NEXT
END SUB

SUB DQBsetFrameRate (FPS)
  IF FPS < 19 THEN dqbFPer = 65536# / 1193181# ELSE dqbFPer = 1# / FPS
  dqbFNext = TIMER + dqbFPer
  dqbFOn = -1
END SUB

FUNCTION DQBframeReady
  DQBframeReady = 0
  IF dqbFOn = 0 THEN EXIT FUNCTION
  t# = TIMER
  IF t# < dqbFNext - 43200 THEN t# = t# + 86400   ' midnight
  IF t# >= dqbFNext THEN
    dqbFNext = dqbFNext + dqbFPer
    IF dqbFNext <= t# THEN dqbFNext = t# + dqbFPer
    IF dqbFNext >= 86400 THEN dqbFNext = dqbFNext - 86400
    DQBframeReady = -1
  END IF
END FUNCTION

'=============================================================================
' DRAW module
'=============================================================================

SUB DQBcopyTransLayer (SourceLayer, DestLayer)
  IF SourceLayer = DestLayer THEN EXIT SUB
  dqbUse SourceLayer
  GET (0, 0)-(319, 199), dqbBuf
  dqbUse DestLayer
  dqbClipAll
  PUT (0, 0), dqbBuf(0), MASK
  dqbClipBack
  dqbDone
END SUB

' non-zero pixels of the source become Col on the destination, 0 stays 0
SUB DQBcopyHitLayer (SourceLayer, DestLayer, Col)
  IF SourceLayer = DestLayer THEN EXIT SUB
  dqbUse SourceLayer
  GET (0, 0)-(319, 199), dqbBuf
  dqbUse DestLayer
  c = Col AND 255: z = (Col AND &HFF00) \ 256 AND 255
  DEF SEG = &HA000
  FOR k& = 0 TO 31999
    v = dqbBuf(k& + 2)
    IF v AND 255 THEN POKE k& * 2, c ELSE POKE k& * 2, z
    IF v AND &HFF00 THEN POKE k& * 2 + 1, c ELSE POKE k& * 2 + 1, z
  NEXT
  DEF SEG
  dqbDone
END SUB

SUB DQBpset (Layer, x, y, Col)
  IF x < dqbCX1 OR x > dqbCX2 OR y < dqbCY1 OR y > dqbCY2 THEN EXIT SUB
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  PSET (x, y), Col AND 255
  IF Layer <> dqbML THEN dqbDone
END SUB

FUNCTION DQBpoint (Layer, x, y)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  c = POINT(x, y)
  IF c < 0 THEN c = 0
  DQBpoint = c
  IF Layer <> dqbML THEN dqbDone
END FUNCTION

SUB DQBline (Layer, x1, y1, x2, y2, Col)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  LINE (x1, y1)-(x2, y2), Col AND 255
  dqbNoView
  IF Layer <> dqbML THEN dqbDone
END SUB

' line with a gradient from Col1 to Col2
SUB DQBgline (Layer, x1, y1, x2, y2, Col1, Col2)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dx = ABS(x2 - x1): dy = ABS(y2 - y1)
  IF dx >= dy THEN
    n = dx + 1: dinc1 = dy * 2: d = dinc1 - dx: dinc2 = (dy - dx) * 2
    xi1 = 1: xi2 = 1: yi1 = 0: yi2 = 1
  ELSE
    n = dy + 1: dinc1 = dx * 2: d = dinc1 - n: dinc2 = (dx - n + 1) * 2
    xi1 = 0: xi2 = 1: yi1 = 1: yi2 = 1
  END IF
  IF x1 > x2 THEN xi1 = -xi1: xi2 = -xi2
  IF y1 > y2 THEN yi1 = -yi1: yi2 = -yi2
  stp& = ((Col2 - Col1 + 1) * 65536) \ n
  acc& = 0: x = x1: y = y1
  FOR i = 1 TO n
    IF x >= dqbCX1 AND x <= dqbCX2 AND y >= dqbCY1 AND y <= dqbCY2 THEN
      PSET (x, y), (Col1 + INT(acc& / 65536)) AND 255
    END IF
    acc& = acc& + stp&
    IF d >= 0 THEN
      d = d + dinc2: x = x + xi2: y = y + yi2
    ELSE
      d = d + dinc1: x = x + xi1: y = y + yi1
    END IF
  NEXT
  IF Layer <> dqbML THEN dqbDone
END SUB

SUB DQBellipse (Layer, x, y, rx, ry, Col)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  c = Col AND 255
  IF rx = 0 THEN
    LINE (x, y - ry)-(x, y + ry), c
  ELSEIF ry = 0 THEN
    LINE (x - rx, y)-(x + rx, y), c
  ELSEIF rx >= ry THEN
    CIRCLE (x, y), ABS(rx), c, , , ABS(ry) / ABS(rx)
  ELSE
    CIRCLE (x, y), ABS(ry), c, , , ABS(ry) / ABS(rx)
  END IF
  dqbNoView
  IF Layer <> dqbML THEN dqbDone
END SUB

SUB DQBbox (Layer, x1, y1, x2, y2, Col)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  LINE (x1, y1)-(x2, y2), Col AND 255, B
  IF Layer <> dqbML THEN dqbDone
END SUB

SUB DQBboxf (Layer, x1, y1, x2, y2, Col)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  LINE (x1, y1)-(x2, y2), Col AND 255, BF
  IF Layer <> dqbML THEN dqbDone
END SUB

' fills the area of pixels of the same color as (x, y)
SUB DQBpaint (Layer, x, y, Col)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  FILL (x, y), Col AND 255
  dqbNoView
  IF Layer <> dqbML THEN dqbDone
END SUB

' moves the layer contents by (dx, dy) as a linear memory block, like DirectQB
SUB DQBscroll (Layer, dx, dy)
  sh& = dy * 320& + dx
  IF sh& = 0 THEN EXIT SUB
  dqbUse Layer
  GET (0, 0)-(319, 199), dqbBuf
  q& = sh& \ 320
  IF sh& < 0 AND (sh& MOD 320) <> 0 THEN q& = q& - 1
  r& = sh& - q& * 320
  IF q& > -200 AND q& < 200 THEN
    dqbClipAll
    PUT (r&, q&), dqbBuf(0), PSET
    IF r& > 0 THEN PUT (r& - 320, q& + 1), dqbBuf(0), PSET
    dqbClipBack
  END IF
  dqbDone
END SUB

' scrolls a box one pixel in a direction (UP, DOWN, LEFT, RIGHT)
SUB DQBscrollArea (Layer, x1, y1, x2, y2, Direction)
  dqbUse Layer
  dqbClipAll
  SELECT CASE Direction
  CASE 0
    IF y2 > y1 THEN GET (x1, y1 + 1)-(x2, y2), dqbBuf: PUT (x1, y1), dqbBuf(0), PSET
  CASE 1
    IF y2 > y1 THEN GET (x1, y1)-(x2, y2 - 1), dqbBuf: PUT (x1, y1 + 1), dqbBuf(0), PSET
  CASE 2
    IF x2 > x1 THEN GET (x1 + 1, y1)-(x2, y2), dqbBuf: PUT (x1, y1), dqbBuf(0), PSET
  CASE 3
    IF x2 > x1 THEN GET (x1, y1)-(x2 - 1, y2), dqbBuf: PUT (x1 + 1, y1), dqbBuf(0), PSET
  END SELECT
  dqbClipBack
  dqbDone
END SUB

SUB DQBsetTransPut
  dqbSolid = 0
END SUB

SUB DQBsetSolidPut
  dqbSolid = -1
END SUB

SUB DQBget (Layer, x1, y1, x2, y2, BufSeg, BufOff)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  GET (x1, y1)-(x2, y2), SEG BufSeg, BufOff
  IF Layer <> dqbML THEN dqbDone
END SUB

SUB DQBput (Layer, x, y, BufSeg, BufOff)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  IF dqbSolid THEN PUT (x, y), SEG BufSeg, BufOff, PSET ELSE PUT (x, y), SEG BufSeg, BufOff, MASK
  IF Layer <> dqbML THEN dqbDone
END SUB

'=============================================================================
' SPRITE module
'=============================================================================

' bytes needed by DQBget for the box (x1,y1)-(x2,y2)
FUNCTION DQBsize (x1, y1, x2, y2)
  DQBsize = dqbS16%((x2 - x1 + 1&) * (y2 - y1 + 1&) + 4)
END FUNCTION

SUB DQBsetClipBox (x1, y1, x2, y2)
  a = x1: b = x2: IF b <= a THEN SWAP a, b
  dqbCX1 = a: dqbCX2 = b
  a = y1: b = y2: IF b <= a THEN SWAP a, b
  dqbCY1 = a: dqbCY2 = b
  dqbCF = (dqbCX1 <= 0 AND dqbCY1 <= 0 AND dqbCX2 >= 319 AND dqbCY2 >= 199)
  dqbClipBack
END SUB

' fast put: solid, not clipped to the clipping box
SUB DQBfPut (Layer, x, y, BufSeg, BufOff)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbClipAll
  PUT (x, y), SEG BufSeg, BufOff, PSET
  dqbClipBack
  IF Layer <> dqbML THEN dqbDone
END SUB

' scaled put
SUB DQBsPut (Layer, x, y, BufSeg, BufOff, NewWidth, NewHeight)
  IF NewWidth <= 0 OR NewHeight <= 0 THEN EXIT SUB
  dqbHdr BufSeg, BufOff, w, h
  DEF SEG
  IF w <= 0 OR h <= 0 THEN EXIT SUB
  xs& = (w * 128&) \ NewWidth: ys& = (h * 128&) \ NewHeight
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  IF NewWidth * CLNG(h) <= 64000 AND h <= 2048 AND NewWidth <= 2190 THEN
    ' scale the columns into the work surface, then draw its rows
    dqbTmp(0) = dqbS16%(NewWidth * 8&): dqbTmp(1) = h
    acc& = 0: last = -1
    FOR dc = 0 TO NewWidth - 1
      sc = (acc& AND &HFFFF&) \ 128
      IF sc <> last THEN GET SEG BufSeg, BufOff, (sc, 0)-(sc, h - 1), dqbCol: last = sc
      PUT dqbTmp(0), (dc, 0), dqbCol(0), PSET
      acc& = acc& + xs&
    NEXT
    acc& = 0
    FOR dr = 0 TO NewHeight - 1
      yy& = y + CLNG(dr)
      IF yy& >= dqbCY1 AND yy& <= dqbCY2 THEN
        sr = (acc& AND &HFFFF&) \ 128
        GET dqbTmp(0), (0, sr)-(NewWidth - 1, sr), dqbRow
        IF dqbSolid THEN PUT (x, yy&), dqbRow(0), PSET ELSE PUT (x, yy&), dqbRow(0), MASK
      END IF
      acc& = acc& + ys&
    NEXT
  ELSE
    DEF SEG = BufSeg
    b& = dqbU16&(BufOff) + 4
    dqbView
    ya& = 0
    FOR dr = 0 TO NewHeight - 1
      rb& = b& + ((ya& AND &HFFFF&) \ 128) * CLNG(w)
      xa& = 0
      FOR dc = 0 TO NewWidth - 1
        c = PEEK(rb& + (xa& AND &HFFFF&) \ 128)
        IF c OR dqbSolid THEN PSET (x + CLNG(dc), y + CLNG(dr)), c
        xa& = xa& + xs&
      NEXT
      ya& = ya& + ys&
    NEXT
    dqbNoView
    DEF SEG
  END IF
  IF Layer <> dqbML THEN dqbDone
END SUB

' rotated (Angle 0-255) and zoomed (Zoom: percent) put, inside the sprite area
SUB DQBrPut (Layer, x, y, BufSeg, BufOff, Angle, Zoom)
  STATIC ct() AS INTEGER, ready
  IF ready = 0 THEN
    REDIM ct(255) AS INTEGER
    FOR i = 0 TO 255: ct(i) = CINT(256 * COS(i * 3.141592653589793# / 128)): NEXT
    ready = -1
  END IF
  IF Zoom = 0 THEN EXIT SUB
  dqbHdr BufSeg, BufOff, w, h
  IF w <= 0 OR h <= 0 THEN DEF SEG: EXIT SUB
  b& = dqbU16&(BufOff) + 4
  a = Angle AND 255
  ca& = (ct(a) * 100&) \ Zoom
  sa& = (ct((a + 64) AND 255) * 100&) \ Zoom
  hw = w \ 2: hh = h \ 2
  v0& = -(hh * ca&) + 256& * hh - hw * sa&
  u0& = -(hw * ca&) + 256& * hw + hh * sa&
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  FOR r = 0 TO h - 1
    py = y + r
    IF py >= dqbCY1 AND py <= dqbCY2 THEN
      u& = u0&: v& = v0&
      FOR c = 0 TO w - 1
        IF u& >= 0 AND v& >= 0 THEN
          su& = u& \ 256: sv& = v& \ 256
          IF su& < w AND sv& < h THEN
            p = PEEK(b& + sv& * w + su&)
            IF p OR dqbSolid THEN PSET (x + c, py), p
          END IF
        END IF
        u& = u& + ca&: v& = v& + sa&
      NEXT
    END IF
    u0& = u0& - sa&: v0& = v0& + ca&
  NEXT
  dqbNoView
  DEF SEG
  IF Layer <> dqbML THEN dqbDone
END SUB

' copies a box of a layer onto another layer, like a sprite
SUB DQBxPut (SourceLayer, x1, y1, x2, y2, DestLayer, x, y)
  dqbUse SourceLayer
  GET (x1, y1)-(x2, y2), dqbBuf
  dqbUse DestLayer
  IF dqbSolid THEN PUT (x, y), dqbBuf(0), PSET ELSE PUT (x, y), dqbBuf(0), MASK
  dqbDone
END SUB

' flipped put: Flip = HOR, VER or both
SUB DQBmPut (Layer, x, y, BufSeg, BufOff, Flip)
  f = Flip AND 3
  IF f = 0 THEN DQBput Layer, x, y, BufSeg, BufOff: EXIT SUB
  dqbHdr BufSeg, BufOff, w, h
  DEF SEG
  IF w <= 0 OR h <= 0 THEN EXIT SUB
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  SELECT CASE f
  CASE 1
    FOR c = 0 TO w - 1
      dx& = x + CLNG(w) - 1 - c
      IF dx& >= dqbCX1 AND dx& <= dqbCX2 THEN
        GET SEG BufSeg, BufOff, (c, 0)-(c, h - 1), dqbCol
        IF dqbSolid THEN PUT (dx&, y), dqbCol(0), PSET ELSE PUT (dx&, y), dqbCol(0), MASK
      END IF
    NEXT
  CASE 2
    FOR r = 0 TO h - 1
      dy& = y + CLNG(h) - 1 - r
      IF dy& >= dqbCY1 AND dy& <= dqbCY2 THEN
        GET SEG BufSeg, BufOff, (0, r)-(w - 1, r), dqbRow
        IF dqbSolid THEN PUT (x, dy&), dqbRow(0), PSET ELSE PUT (x, dy&), dqbRow(0), MASK
      END IF
    NEXT
  CASE 3
    dqbTmp(0) = dqbS16%(w * 8&): dqbTmp(1) = h
    FOR c = 0 TO w - 1
      GET SEG BufSeg, BufOff, (c, 0)-(c, h - 1), dqbCol
      PUT dqbTmp(0), (w - 1 - c, 0), dqbCol(0), PSET
    NEXT
    FOR r = 0 TO h - 1
      dy& = y + CLNG(h) - 1 - r
      IF dy& >= dqbCY1 AND dy& <= dqbCY2 THEN
        GET dqbTmp(0), (0, r)-(w - 1, r), dqbRow
        IF dqbSolid THEN PUT (x, dy&), dqbRow(0), PSET ELSE PUT (x, dy&), dqbRow(0), MASK
      END IF
    NEXT
  END SELECT
  IF Layer <> dqbML THEN dqbDone
END SUB

' every non-transparent pixel of the sprite drawn with color Col
SUB DQBhPut (Layer, x, y, BufSeg, BufOff, Col)
  dqbHdr BufSeg, BufOff, w, h
  b& = dqbU16&(BufOff) + 4
  c = Col AND 255
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  FOR r = 0 TO h - 1
    py = y + r
    IF py >= dqbCY1 AND py <= dqbCY2 THEN
      rb& = b& + r * CLNG(w)
      FOR i = 0 TO w - 1
        IF PEEK(rb& + i) THEN PSET (x + i, py), c
      NEXT
    END IF
  NEXT
  dqbNoView
  DEF SEG
  IF Layer <> dqbML THEN dqbDone
END SUB

' put with a bitwise operation: BIT.AND, BIT.OR or BIT.XOR
SUB DQBtPut (Layer, x, y, BufSeg, BufOff, BitMode)
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  SELECT CASE BitMode
  CASE 1
    IF dqbSolid THEN
      PUT (x, y), SEG BufSeg, BufOff, AND
    ELSE
      ' transparent: pixels of color 0 leave the background alone
      dqbHdr BufSeg, BufOff, w, h
      b& = dqbU16&(BufOff) + 4
      dqbView
      FOR r = 0 TO h - 1
        rb& = b& + r * CLNG(w)
        py = y + r
        FOR i = 0 TO w - 1
          p = PEEK(rb& + i)
          IF p THEN
            d = POINT(x + i, py)
            IF d >= 0 THEN PSET (x + i, py), d AND p
          END IF
        NEXT
      NEXT
      dqbNoView
      DEF SEG
    END IF
  CASE 2
    PUT (x, y), SEG BufSeg, BufOff, OR
  CASE ELSE
    PUT (x, y), SEG BufSeg, BufOff, XOR
  END SELECT
  IF Layer <> dqbML THEN dqbDone
END SUB

' put through an 8-bit pattern, inverted on every line
SUB DQBpPut (Layer, x, y, BufSeg, BufOff, Pattern)
  dqbHdr BufSeg, BufOff, w, h
  b& = dqbU16&(BufOff) + 4
  pat = Pattern AND 255
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  FOR r = 0 TO h - 1
    py = y + r
    IF r AND 1 THEN pr = pat XOR 255 ELSE pr = pat
    IF pr <> 0 AND py >= dqbCY1 AND py <= dqbCY2 THEN
      rb& = b& + r * CLNG(w)
      m = 1
      FOR i = 0 TO w - 1
        IF pr AND m THEN
          p = PEEK(rb& + i)
          IF p OR dqbSolid THEN PSET (x + i, py), p
        END IF
        m = m * 2: IF m = 256 THEN m = 1
      NEXT
    END IF
  NEXT
  dqbNoView
  DEF SEG
  IF Layer <> dqbML THEN dqbDone
END SUB

' draws a sprite over another sprite (clipped to it, color 0 transparent)
SUB DQBputOver (BackSeg, BackOff, x, y, BufSeg, BufOff)
  PUT SEG BackSeg, BackOff, (x, y), SEG BufSeg, BufOff, MASK
END SUB

SUB DQBsetCollideMethod (Method)
  dqbColM = Method
END SUB

FUNCTION DQBcollide (x1, y1, Seg1, Off1, x2, y2, Seg2, Off2)
  DQBcollide = 0
  dqbHdr Seg1, Off1, w1, h1
  dqbHdr Seg2, Off2, w2, h2
  DEF SEG
  IF x1 + w1 <= x2 OR x2 + w2 <= x1 OR y1 + h1 <= y2 OR y2 + h2 <= y1 THEN EXIT FUNCTION
  IF dqbColM = 0 THEN DQBcollide = -1: EXIT FUNCTION
  ix1 = x1: IF x2 > ix1 THEN ix1 = x2
  iy1 = y1: IF y2 > iy1 THEN iy1 = y2
  ix2 = x1 + w1 - 1: IF x2 + w2 - 1 < ix2 THEN ix2 = x2 + w2 - 1
  iy2 = y1 + h1 - 1: IF y2 + h2 - 1 < iy2 THEN iy2 = y2 + h2 - 1
  sb1& = dqbU16&(Off1) + 4: sb2& = dqbU16&(Off2) + 4
  FOR py = iy1 TO iy2
    r1& = sb1& + (py - y1) * CLNG(w1) - x1
    r2& = sb2& + (py - y2) * CLNG(w2) - x2
    FOR px = ix1 TO ix2
      DEF SEG = Seg1
      IF PEEK(r1& + px) THEN
        DEF SEG = Seg2
        IF PEEK(r2& + px) THEN DQBcollide = -1: DEF SEG: EXIT FUNCTION
      END IF
    NEXT
  NEXT
  DEF SEG
END FUNCTION

' color of the first layer pixel under a non-transparent sprite pixel, or 0
FUNCTION DQBcollideOnLayer (Layer, x, y, SpriteSeg, SpriteOff)
  DQBcollideOnLayer = 0
  dqbHdr SpriteSeg, SpriteOff, w, h
  b& = dqbU16&(SpriteOff) + 4
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  res = 0
  FOR r = 0 TO h - 1
    py = y + r
    IF py >= dqbCY1 AND py <= dqbCY2 THEN
      rb& = b& + r * CLNG(w)
      FOR i = 0 TO w - 1
        px = x + i
        IF px >= dqbCX1 AND px <= dqbCX2 THEN
          IF PEEK(rb& + i) THEN
            c = POINT(px, py)
            IF c > 0 THEN res = c: EXIT FOR
          END IF
        END IF
      NEXT
      IF res THEN EXIT FOR
    END IF
  NEXT
  DEF SEG
  DQBcollideOnLayer = res
  IF Layer <> dqbML THEN dqbDone
END FUNCTION

'=============================================================================
' BIT module (16-bit values, as in assembly)
'=============================================================================

FUNCTION dqbBitM& (b)
  IF b < 0 OR b > 15 THEN dqbBitM& = 0 ELSE dqbBitM& = 2& ^ b
END FUNCTION

FUNCTION DQBsetBit (Value, Bit)
  DQBsetBit = dqbS16%((Value AND &HFFFF&) OR dqbBitM&(Bit))
END FUNCTION

FUNCTION DQBresetBit (Value, Bit)
  DQBresetBit = dqbS16%((Value AND &HFFFF&) AND NOT dqbBitM&(Bit))
END FUNCTION

FUNCTION DQBreadBit (Value, Bit)
  IF (Value AND &HFFFF&) AND dqbBitM&(Bit) THEN DQBreadBit = -1 ELSE DQBreadBit = 0
END FUNCTION

FUNCTION DQBtoggleBit (Value, Bit)
  DQBtoggleBit = dqbS16%((Value AND &HFFFF&) XOR dqbBitM&(Bit))
END FUNCTION

FUNCTION DQBshiftLeft (Value, nBits)
  IF nBits <= 0 THEN DQBshiftLeft = Value: EXIT FUNCTION
  IF nBits > 15 THEN DQBshiftLeft = 0: EXIT FUNCTION
  DQBshiftLeft = dqbS16%((Value AND &HFFFF&) * 2& ^ nBits)
END FUNCTION

FUNCTION DQBshiftRight (Value, nBits)
  IF nBits <= 0 THEN DQBshiftRight = Value: EXIT FUNCTION
  IF nBits > 15 THEN DQBshiftRight = 0: EXIT FUNCTION
  DQBshiftRight = dqbS16%((Value AND &HFFFF&) \ 2& ^ nBits)
END FUNCTION

'=============================================================================
' PALETTE module (VGA DAC, values 0-63)
'=============================================================================

SUB DQBsetCol (ColorIndex, Red, Green, Blue)
  OUT &H3C8, ColorIndex AND 255
  OUT &H3C9, Red AND 63: OUT &H3C9, Green AND 63: OUT &H3C9, Blue AND 63
END SUB

SUB DQBgetCol (ColorIndex, Red, Green, Blue)
  OUT &H3C7, ColorIndex AND 255
  Red = INP(&H3C9): Green = INP(&H3C9): Blue = INP(&H3C9)
END SUB

FUNCTION DQBfindCol (Red, Green, Blue)
  br = 64: bg = 64: bb = 64: best = 0
  OUT &H3C7, 0
  FOR c = 0 TO 255
    dr = ABS(INP(&H3C9) - Red): dg = ABS(INP(&H3C9) - Green): db = ABS(INP(&H3C9) - Blue)
    IF dr <= br AND dg <= bg AND db <= bb THEN br = dr: bg = dg: bb = db: best = c
  NEXT
  DQBfindCol = best
END FUNCTION

FUNCTION DQBfindPalCol (Pal$, Red, Green, Blue)
  br = 64: bg = 64: bb = 64: best = 0
  p$ = LEFT$(Pal$ + STRING$(768, 0), 768)
  FOR c = 0 TO 255
    dr = ABS(ASC(MID$(p$, c * 3 + 1, 1)) - Red)
    dg = ABS(ASC(MID$(p$, c * 3 + 2, 1)) - Green)
    db = ABS(ASC(MID$(p$, c * 3 + 3, 1)) - Blue)
    IF dr <= br AND dg <= bg AND db <= bb THEN br = dr: bg = dg: bb = db: best = c
  NEXT
  DQBfindPalCol = best
END FUNCTION

SUB DQBsetPal (Pal$)
  p$ = LEFT$(Pal$ + STRING$(768, 0), 768)
  OUT &H3C8, 0
  FOR i = 1 TO 768: OUT &H3C9, ASC(MID$(p$, i, 1)) AND 63: NEXT
END SUB

SUB DQBgetPal (Pal$)
  p$ = SPACE$(768)
  OUT &H3C7, 0
  FOR i = 1 TO 768: MID$(p$, i, 1) = CHR$(INP(&H3C9)): NEXT
  Pal$ = p$
END SUB

' one fade step of every color towards a 768-byte palette
SUB dqbFadeStep (t$, changed)
  DIM cur(767) AS INTEGER
  OUT &H3C7, 0
  FOR i = 0 TO 767: cur(i) = INP(&H3C9): NEXT
  changed = 0
  FOR i = 0 TO 767
    tv = ASC(MID$(t$, i + 1, 1)) AND 63
    IF cur(i) < tv THEN
      cur(i) = cur(i) + 1: changed = -1
    ELSEIF cur(i) > tv THEN
      cur(i) = cur(i) - 1: changed = -1
    END IF
  NEXT
  OUT &H3C8, 0
  FOR i = 0 TO 767: OUT &H3C9, cur(i): NEXT
END SUB

FUNCTION dqbSolidPal$ (Red, Green, Blue)
  c$ = CHR$(Red AND 63) + CHR$(Green AND 63) + CHR$(Blue AND 63)
  p$ = ""
  FOR i = 1 TO 256: p$ = p$ + c$: NEXT
  dqbSolidPal$ = p$
END FUNCTION

SUB DQBfadeIn (Pal$)
  p$ = LEFT$(Pal$ + STRING$(768, 0), 768)
  DO
    WAIT VSYNC
    dqbFadeStep p$, ch
  LOOP WHILE ch
END SUB

SUB DQBfadeStepIn (Pal$)
  dqbFadeStep LEFT$(Pal$ + STRING$(768, 0), 768), ch
END SUB

SUB DQBfadeTo (Red, Green, Blue)
  p$ = dqbSolidPal$(Red, Green, Blue)
  DO
    WAIT VSYNC
    dqbFadeStep p$, ch
  LOOP WHILE ch
END SUB

SUB DQBfadeStepTo (Red, Green, Blue)
  dqbFadeStep dqbSolidPal$(Red, Green, Blue), ch
END SUB

SUB DQBpalOff
  OUT &H3C8, 0
  FOR i = 1 TO 768: OUT &H3C9, 0: NEXT
END SUB

SUB DQBpalRotate (FirstCol, LastCol, RotDir)
  f = FirstCol AND 255: l = LastCol AND 255
  IF l <= f THEN EXIT SUB
  DIM r(255), g(255), b(255)
  OUT &H3C7, f
  FOR i = f TO l: r(i) = INP(&H3C9): g(i) = INP(&H3C9): b(i) = INP(&H3C9): NEXT
  OUT &H3C8, f
  IF RotDir = 0 THEN
    OUT &H3C9, r(l): OUT &H3C9, g(l): OUT &H3C9, b(l)
    FOR i = f + 1 TO l: OUT &H3C9, r(i - 1): OUT &H3C9, g(i - 1): OUT &H3C9, b(i - 1): NEXT
  ELSE
    FOR i = f TO l - 1: OUT &H3C9, r(i + 1): OUT &H3C9, g(i + 1): OUT &H3C9, b(i + 1): NEXT
    OUT &H3C9, r(f): OUT &H3C9, g(f): OUT &H3C9, b(f)
  END IF
END SUB

'=============================================================================
' FONT module
'=============================================================================

SUB dqbFontStyles
  ' each font row as a LINE style pattern: a whole row is drawn with one LINE
  FOR i = 0 TO 2047: dqbFS(i) = dqbFont(i) * 256&: NEXT
  dqbFontOK = -1
END SUB

SUB dqbApplyFont (f$)
  s$ = LEFT$(f$ + STRING$(2305, 8), 2305)
  FOR i = 0 TO 2047: dqbFont(i) = ASC(MID$(s$, i + 1, 1)): NEXT
  FOR i = 0 TO 256: dqbCLen(i) = ASC(MID$(s$, 2049 + i, 1)): NEXT
  dqbFontStyles
END SUB

SUB dqbPrintX (Layer, t$, x, y, Col, Style)
  IF dqbFontOK = 0 THEN dqbLoadBIOS
  n = LEN(t$)
  IF n = 0 THEN EXIT SUB
  md = dqbTMode
  IF (Style AND 255) <> 255 THEN md = Style AND 255
  h = dqbCLen(256)
  wt = 0
  FOR i = 1 TO n
    wt = wt + dqbCLen(ASC(MID$(t$, i, 1)))
    IF md AND 2 THEN wt = wt + 1
    IF i < n THEN wt = wt + dqbTSpace
  NEXT
  bx = x
  IF x = -32768 THEN bx = (dqbCX2 - dqbCX1 + 1 - wt) \ 2 + dqbCX1
  IF md AND 4 THEN wt = wt + h \ 4
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbView
  fc = Col AND 255
  IF (md AND 1) <> 0 AND wt > 0 AND h > 0 THEN
    IF md AND 16 THEN
      FOR py = y TO y + h - 1
        FOR px = bx TO bx + wt - 1
          d = POINT(px, py)
          IF d >= 0 THEN PSET (px, py), dqbBlend(dqbTBMap, dqbTBack, d)
        NEXT
      NEXT
    ELSE
      LINE (bx, y)-(bx + wt - 1, y + h - 1), dqbTBack AND 255, BF
    END IF
  END IF
  IF (md AND 8) <> 0 AND wt > 0 THEN
    IF md AND 16 THEN
      FOR px = bx TO bx + wt - 1
        d = POINT(px, y + h)
        IF d >= 0 THEN PSET (px, y + h), dqbBlend(dqbTBMap, fc, d)
      NEXT
    ELSE
      LINE (bx, y + h)-(bx + wt - 1, y + h), fc
    END IF
  END IF
  cx = bx
  IF md AND 4 THEN cx = cx + h \ 4
  FOR i = 1 TO n
    c = ASC(MID$(t$, i, 1))
    rx = cx
    FOR r = 0 TO h - 1
      fi = c * 8 + r
      IF fi <= 2047 THEN
        bits = dqbFont(fi)
        IF bits THEN
          py = y + r
          IF (md AND 48) = 0 THEN
            LINE (rx, py)-(rx + 7, py), fc, , dqbFS(fi)
            IF md AND 2 THEN LINE (rx + 1, py)-(rx + 8, py), fc, , dqbFS(fi)
          ELSE
            m = 128
            FOR k = 0 TO 7
              IF bits AND m THEN
                IF md AND 32 THEN pc = dqbTex((r * 8 + k) AND 63) ELSE pc = fc
                FOR bb = 0 TO -((md AND 2) <> 0)
                  px = rx + k + bb
                  IF md AND 16 THEN
                    d = POINT(px, py)
                    IF d >= 0 THEN PSET (px, py), dqbBlend(dqbTBMap, pc, d)
                  ELSE
                    PSET (px, py), pc
                  END IF
                NEXT
              END IF
              m = m \ 2
            NEXT
          END IF
        END IF
      END IF
      IF md AND 4 THEN
        IF ((h - r - 1) AND 3) = 3 THEN rx = rx - 1
      END IF
    NEXT
    cx = cx + dqbCLen(c) + dqbTSpace
    IF md AND 2 THEN cx = cx + 1
    IF md AND 4 THEN cx = cx + 2
  NEXT
  dqbNoView
  IF Layer <> dqbML THEN dqbDone
END SUB

SUB DQBprint (Layer, Text$, x, y, Col)
  dqbPrintX Layer, Text$, x, y, Col, 255
END SUB

SUB DQBprints (Layer, Text$, x, y, Col, Style)
  dqbPrintX Layer, Text$, x, y, Col, Style
END SUB

FUNCTION DQBlen (Text$)
  IF dqbFontOK = 0 THEN dqbLoadBIOS
  n = LEN(Text$)
  IF n = 0 THEN DQBlen = 0: EXIT FUNCTION
  w = 0
  IF dqbTMode AND 4 THEN w = dqbCLen(256) \ 4
  FOR i = 1 TO n
    w = w + dqbCLen(ASC(MID$(Text$, i, 1))) + dqbTSpace
    IF dqbTMode AND 2 THEN w = w + 1
  NEXT
  DQBlen = w
END FUNCTION

SUB DQBsetBIOSfont
  dqbLoadBIOS
  dqbTMode = 0
END SUB

FUNCTION DQBloadFont (FileName$)
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBloadFont = 1: EXIT FUNCTION
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  IF LOF(f) < 2305 THEN CLOSE #f: dqbErr = 5: DQBloadFont = 2: EXIT FUNCTION
  s$ = SPACE$(2305)
  GET #f, 1, s$
  CLOSE #f
  dqbApplyFont s$
  DQBloadFont = 0
END FUNCTION

SUB DQBsetFont (Font$)
  dqbApplyFont Font$
END SUB

SUB DQBsetTextStyle (Style)
  dqbTMode = Style AND 255
END SUB

SUB DQBsetTextBackCol (Col)
  dqbTBack = Col AND 255
END SUB

SUB DQBsetTextSpacing (Spacing)
  dqbTSpace = Spacing
END SUB

SUB DQBsetTextBMap (BMap)
  dqbTBMap = BMap
END SUB

' the font texture: an 8x8 sprite
SUB DQBsetFontTexture (TextSeg, TextOff)
  DEF SEG = TextSeg
  o& = dqbU16&(TextOff) + 4
  FOR i = 0 TO 63: dqbTex(i) = PEEK(o& + i): NEXT
  DEF SEG
END SUB

'=============================================================================
' DISK module
'=============================================================================

' first file matching Mask (or the next one, with Mask = "")
FUNCTION DQBdir$ (Mask$, Attributes)
  IF Mask$ = "" THEN DQBdir$ = DIR$: EXIT FUNCTION
  IF Attributes = &H8 THEN DQBdir$ = "": EXIT FUNCTION   ' no volume labels
  DQBdir$ = DIR$(Mask$, Attributes)
END FUNCTION

FUNCTION DQBdrive$
  c$ = CURDIR$
  IF MID$(c$, 2, 1) = ":" THEN DQBdrive$ = UCASE$(LEFT$(c$, 1)) ELSE DQBdrive$ = "C"
END FUNCTION

' current path, without the drive and the first backslash
FUNCTION DQBpath$
  c$ = CURDIR$
  IF MID$(c$, 2, 1) = ":" THEN c$ = MID$(c$, 3)
  FOR i = 1 TO LEN(c$)
    IF MID$(c$, i, 1) = "/" THEN MID$(c$, i, 1) = "\"
  NEXT
  IF LEFT$(c$, 1) = "\" THEN c$ = MID$(c$, 2)
  DQBpath$ = c$
END FUNCTION

FUNCTION DQBnumDrives
  DQBnumDrives = 3
END FUNCTION

SUB DQBsetDrive (NewDrive$)
  ' drives do not exist on every system: nothing to do
END SUB

FUNCTION DQBchDir (NewDir$)
  d$ = NewDir$
  ok = 0
  IF d$ = "." OR d$ = ".." OR d$ = "\" OR d$ = "/" OR RIGHT$(d$, 2) = "\." OR RIGHT$(d$, 3) = "\.." THEN
    ok = -1
  ELSEIF LEN(d$) > 0 AND INSTR(d$, "*") = 0 AND INSTR(d$, "?") = 0 THEN
    e$ = d$
    IF RIGHT$(e$, 1) = "\" OR RIGHT$(e$, 1) = "/" THEN e$ = LEFT$(e$, LEN(e$) - 1)
    IF RIGHT$(e$, 1) = ":" OR e$ = "" THEN ok = -1 ELSE IF DIR$(e$, &H10) <> "" THEN ok = -1
  END IF
  IF ok THEN CHDIR d$: DQBchDir = 0 ELSE DQBchDir = -1
END FUNCTION

'=============================================================================
' BLENDING module
'=============================================================================

FUNCTION DQBcreateBMap (BMap, FirstCol, LastCol)
  IF BMap < 1 OR BMap > 10 THEN dqbErr = 7: DQBcreateBMap = 1: EXIT FUNCTION
  IF dqbBMOn(BMap) THEN dqbErr = 6: DQBcreateBMap = 2: EXIT FUNCTION
  IF UBOUND(dqbBM) < 655359 THEN REDIM dqbBM(0 TO 655359) AS INTEGER
  f = FirstCol AND 255: l = LastCol AND 255
  IF l < f THEN dqbErr = 7: DQBcreateBMap = 1: EXIT FUNCTION
  dqbBMF(BMap) = f: dqbBML(BMap) = l: dqbBMOn(BMap) = -1
  b& = (BMap - 1) * 65536
  FOR c = f TO l
    o& = b& + (c - f) * 256&
    FOR k = 0 TO 255: dqbBM(o& + k) = c: NEXT
  NEXT
  DQBcreateBMap = 0
END FUNCTION

SUB DQBsetBMap (BMap, ForeCol, BackCol, NewCol)
  IF BMap < 1 OR BMap > 10 THEN EXIT SUB
  IF dqbBMOn(BMap) = 0 THEN EXIT SUB
  f = ForeCol AND 255
  IF f < dqbBMF(BMap) OR f > dqbBML(BMap) THEN EXIT SUB
  dqbBM((BMap - 1) * 65536 + (f - dqbBMF(BMap)) * 256& + (BackCol AND 255)) = NewCol AND 255
END SUB

FUNCTION DQBgetBMap (BMap, ForeCol, BackCol)
  DQBgetBMap = -1
  IF BMap < 1 OR BMap > 10 THEN EXIT FUNCTION
  IF dqbBMOn(BMap) = 0 THEN EXIT FUNCTION
  f = ForeCol AND 255
  IF f < dqbBMF(BMap) OR f > dqbBML(BMap) THEN EXIT FUNCTION
  DQBgetBMap = dqbBM((BMap - 1) * 65536 + (f - dqbBMF(BMap)) * 256& + (BackCol AND 255))
END FUNCTION

SUB DQBremoveBMap (BMap)
  IF BMap >= 1 AND BMap <= 10 THEN dqbBMOn(BMap) = 0
END SUB

FUNCTION DQBloadBMap (BMap, FileName$)
  IF BMap < 1 OR BMap > 10 THEN dqbErr = 8: DQBloadBMap = 1: EXIT FUNCTION
  IF dqbBMOn(BMap) = 0 THEN dqbErr = 8: DQBloadBMap = 1: EXIT FUNCTION
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBloadBMap = 2: EXIT FUNCTION
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  IF LOF(f) < 6 THEN CLOSE #f: dqbErr = 5: DQBloadBMap = 5: EXIT FUNCTION
  h$ = SPACE$(6): GET #f, 1, h$
  IF LEFT$(h$, 4) <> "BMap" THEN CLOSE #f: dqbErr = 11: DQBloadBMap = 3: EXIT FUNCTION
  cnt = ASC(MID$(h$, 6, 1)) - ASC(MID$(h$, 5, 1))
  IF cnt <> dqbBML(BMap) - dqbBMF(BMap) THEN CLOSE #f: dqbErr = 22: DQBloadBMap = 4: EXIT FUNCTION
  n& = (cnt + 1) * 256&
  IF LOF(f) < 6 + n& THEN CLOSE #f: dqbErr = 5: DQBloadBMap = 5: EXIT FUNCTION
  d$ = SPACE$(n&): GET #f, , d$
  CLOSE #f
  b& = (BMap - 1) * 65536
  FOR i& = 0 TO n& - 1: dqbBM(b& + i&) = ASC(MID$(d$, i& + 1, 1)): NEXT
  DQBloadBMap = 0
END FUNCTION

FUNCTION DQBsaveBMap (BMap, FileName$)
  IF BMap < 1 OR BMap > 10 THEN dqbErr = 8: DQBsaveBMap = 1: EXIT FUNCTION
  IF dqbBMOn(BMap) = 0 THEN dqbErr = 8: DQBsaveBMap = 1: EXIT FUNCTION
  n& = (dqbBML(BMap) - dqbBMF(BMap) + 1) * 256&
  d$ = "BMap" + CHR$(dqbBMF(BMap)) + CHR$(dqbBML(BMap)) + SPACE$(n&)
  b& = (BMap - 1) * 65536
  FOR i& = 0 TO n& - 1: MID$(d$, 7 + i&, 1) = CHR$(dqbBM(b& + i&)): NEXT
  f = FREEFILE
  OPEN FileName$ FOR OUTPUT AS #f: CLOSE #f
  OPEN FileName$ FOR BINARY AS #f
  PUT #f, 1, d$
  CLOSE #f
  DQBsaveBMap = 0
END FUNCTION

' box blending a color with the background
SUB DQBfilterBox (Layer, x1, y1, x2, y2, Col, BMap)
  IF BMap < 1 OR BMap > 10 THEN EXIT SUB
  IF dqbBMOn(BMap) = 0 THEN EXIT SUB
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  c = Col AND 255
  IF c < dqbBMF(BMap) OR c > dqbBML(BMap) THEN
    LINE (x1, y1)-(x2, y2), c, BF
  ELSE
    b& = (BMap - 1) * 65536 + (c - dqbBMF(BMap)) * 256&
    ax = x1: bx = x2: IF ax < 0 THEN ax = 0
    IF bx > 319 THEN bx = 319
    ay = y1: by = y2: IF ay < 0 THEN ay = 0
    IF by > 199 THEN by = 199
    DEF SEG = &HA000
    FOR y = ay TO by
      o& = y * 320&
      FOR x = ax TO bx
        POKE o& + x, dqbBM(b& + PEEK(o& + x))
      NEXT
    NEXT
    DEF SEG
  END IF
  IF Layer <> dqbML THEN dqbDone
END SUB

' sprite blended with the background
SUB DQBbPut (Layer, x, y, BufSeg, BufOff, BMap)
  IF BMap < 1 OR BMap > 10 THEN EXIT SUB
  IF dqbBMOn(BMap) = 0 THEN EXIT SUB
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  dqbHdr BufSeg, BufOff, w, h
  b& = dqbU16&(BufOff) + 4
  f = dqbBMF(BMap): l = dqbBML(BMap): m& = (BMap - 1) * 65536
  FOR r = 0 TO h - 1
    py = y + r
    IF py >= dqbCY1 AND py <= dqbCY2 AND py >= 0 AND py <= 199 THEN
      rb& = b& + r * CLNG(w)
      o& = py * 320&
      FOR i = 0 TO w - 1
        px = x + i
        IF px >= dqbCX1 AND px <= dqbCX2 AND px >= 0 AND px <= 319 THEN
          DEF SEG = BufSeg
          p = PEEK(rb& + i)
          IF p OR dqbSolid THEN
            DEF SEG = &HA000
            IF p >= f AND p <= l THEN p = dqbBM(m& + (p - f) * 256& + PEEK(o& + px))
            POKE o& + px, p
          END IF
        END IF
      NEXT
    END IF
  NEXT
  DEF SEG
  IF Layer <> dqbML THEN dqbDone
END SUB

' layer blended into another layer (color 0 is transparent)
SUB DQBcopyBlendLayer (SourceLayer, DestLayer, BMap)
  IF BMap < 1 OR BMap > 10 THEN EXIT SUB
  IF dqbBMOn(BMap) = 0 OR SourceLayer = DestLayer THEN EXIT SUB
  dqbUse SourceLayer
  GET (0, 0)-(319, 199), dqbBuf
  dqbUse DestLayer
  f = dqbBMF(BMap): l = dqbBML(BMap): m& = (BMap - 1) * 65536
  DEF SEG = &HA000
  FOR k& = 0 TO 31999
    v = dqbBuf(k& + 2)
    IF v THEN
      p = v AND 255
      IF p THEN
        IF p >= f AND p <= l THEN p = dqbBM(m& + (p - f) * 256& + PEEK(k& * 2))
        POKE k& * 2, p
      END IF
      p = ((v AND &HFF00) \ 256) AND 255
      IF p THEN
        IF p >= f AND p <= l THEN p = dqbBM(m& + (p - f) * 256& + PEEK(k& * 2 + 1))
        POKE k& * 2 + 1, p
      END IF
    END IF
  NEXT
  DEF SEG
  dqbDone
END SUB

'=============================================================================
' KEYBOARD module (QBLike MULTIKEY)
'=============================================================================

SUB dqbFlushKeys
  DO WHILE LEN(INKEY$): LOOP
END SUB

SUB DQBinstallKeyboard
  dqbKeyOn = -1
  dqbFlushKeys
END SUB

SUB DQBremoveKeyboard
  dqbKeyOn = 0
  dqbFlushKeys
END SUB

FUNCTION DQBkey (ScanCode)
  IF ScanCode = -1 THEN
    FOR k = 1 TO 127
      IF MULTIKEY(k) THEN DQBkey = -1: EXIT FUNCTION
    NEXT
    DQBkey = 0
  ELSEIF ScanCode > 0 AND ScanCode < 128 THEN
    DQBkey = MULTIKEY(ScanCode)
  ELSE
    DQBkey = 0
  END IF
END FUNCTION

' waits for a key to be pressed and released; returns its scancode
FUNCTION DQBreadKey
  IF dqbKeyOn = 0 THEN DQBreadKey = -1: EXIT FUNCTION
  k = 0
  DO
    FOR i = 1 TO 127
      IF MULTIKEY(i) THEN k = i: EXIT FOR
    NEXT
  LOOP UNTIL k
  DO: LOOP WHILE MULTIKEY(k)
  dqbFlushKeys
  DQBreadKey = k
END FUNCTION

SUB DQBwaitKey (ScanCode)
  IF dqbKeyOn = 0 THEN EXIT SUB
  IF ScanCode = -1 THEN
    k = 0
    DO
      FOR i = 1 TO 127
        IF MULTIKEY(i) THEN k = i: EXIT FOR
      NEXT
    LOOP UNTIL k
  ELSE
    IF ScanCode < 1 OR ScanCode > 127 THEN EXIT SUB
    k = ScanCode
    DO: LOOP UNTIL MULTIKEY(k)
  END IF
  DO: LOOP WHILE MULTIKEY(k)
  dqbFlushKeys
END SUB

FUNCTION DQBasc (ScanCode, ShiftFlag)
  IF LEN(dqbKM) = 0 THEN dqbLoadKeyMap
  i = ScanCode - 1
  IF ShiftFlag THEN i = i + 128
  IF i < 0 OR i > 255 THEN DQBasc = 0 ELSE DQBasc = ASC(MID$(dqbKM, i + 1, 1))
END FUNCTION

FUNCTION DQBinkey$
  DQBinkey$ = ""
  IF LEN(dqbKM) = 0 THEN dqbLoadKeyMap
  FOR k = 1 TO 127
    IF k <> 42 AND k <> 54 THEN
      IF MULTIKEY(k) THEN
        s = 0
        IF MULTIKEY(42) OR MULTIKEY(54) THEN s = 128
        DO: LOOP WHILE MULTIKEY(k)
        dqbFlushKeys
        c = ASC(MID$(dqbKM, k + s, 1))
        IF c THEN DQBinkey$ = CHR$(c)
        EXIT FUNCTION
      END IF
    END IF
  NEXT
END FUNCTION

'=============================================================================
' JOYSTICK module: no joystick is ever detected
'=============================================================================

FUNCTION DQBjoyDetected (JoyNum)
  DQBjoyDetected = 0
END FUNCTION

FUNCTION DQBjoyX (JoyNum)
  DQBjoyX = 0
END FUNCTION

FUNCTION DQBjoyY (JoyNum)
  DQBjoyY = 0
END FUNCTION

FUNCTION DQBjoyMove (JoyNum, Direction)
  DQBjoyMove = 0
END FUNCTION

FUNCTION DQBjoyFire (JoyNum, Button)
  DQBjoyFire = 0
END FUNCTION

SUB DQBpollJoy (JoyNum)
END SUB

SUB DQBresetJoy
END SUB

SUB DQBsetJoySens (Sens)
END SUB

'=============================================================================
' MOUSE module (QBLike MOUSE)
'=============================================================================

SUB dqbReadMouse
  MOUSE mx, my, mb
  IF mx >= 0 THEN dqbMX = mx: dqbMY = my
  dqbMB = mb
END SUB

FUNCTION DQBmouseDetected
  DQBmouseDetected = -1
END FUNCTION

FUNCTION DQBmouseX
  dqbReadMouse
  v = dqbMX + dqbMOX
  IF v < dqbMX1 THEN v = dqbMX1
  IF v > dqbMX2 THEN v = dqbMX2
  DQBmouseX = v
END FUNCTION

FUNCTION DQBmouseY
  dqbReadMouse
  v = dqbMY + dqbMOY
  IF v < dqbMY1 THEN v = dqbMY1
  IF v > dqbMY2 THEN v = dqbMY2
  DQBmouseY = v
END FUNCTION

FUNCTION DQBmouseLB
  dqbReadMouse
  IF dqbMB AND 1 THEN DQBmouseLB = -1 ELSE DQBmouseLB = 0
END FUNCTION

FUNCTION DQBmouseRB
  dqbReadMouse
  IF dqbMB AND 2 THEN DQBmouseRB = -1 ELSE DQBmouseRB = 0
END FUNCTION

' the system pointer cannot be moved: the position read is shifted instead
SUB DQBsetMousePos (x, y)
  dqbReadMouse
  dqbMOX = x - dqbMX: dqbMOY = y - dqbMY
END SUB

SUB DQBmouseShow
  MOUSE ON
  dqbMOn = -1
END SUB

SUB DQBmouseHide
  MOUSE OFF
  dqbMOn = 0
END SUB

SUB DQBsetMouseRange (x1, y1, x2, y2)
  dqbMX1 = x1: dqbMY1 = y1: dqbMX2 = x2: dqbMY2 = y2
END SUB

SUB DQBsetMouseShape (hotX, hotY, Shape$)
  ' QBLike draws its own arrow: custom shapes are not supported
END SUB

SUB DQBsetMouseSpeed (HorSpeed, VerSpeed)
END SUB

SUB DQBresetMouse
  dqbMX1 = 0: dqbMY1 = 0: dqbMX2 = 319: dqbMY2 = 199
  dqbMOX = 0: dqbMOY = 0
END SUB

'=============================================================================
' SOUND module (QBLike SOUND: 12 channels)
'=============================================================================

FUNCTION DQBinstallSB (VolActive, Channels, Freq, BaseAddr, IRQ, DMA)
  IF dqbNSnd = 0 THEN dqbErr = 12: DQBinstallSB = 1: EXIT FUNCTION
  dqbVoices = Channels
  IF dqbVoices < 1 THEN dqbVoices = 1
  IF dqbVoices > 32 THEN dqbVoices = 32
  dqbSB = -1
  DQBinstallSB = 0
END FUNCTION

' QBLike channel of a DirectQB voice
FUNCTION dqbChan (Voice)
  IF Voice < 1 THEN dqbChan = 0 ELSE dqbChan = ((Voice - 1) MOD 12) + 1
END FUNCTION

' loads raw 8-bit unsigned mono data into a slot (through a temporary WAV)
SUB dqbRawToSlot (Slot, d$, rate&)
  t$ = dqbTempName$(".WAV")
  n& = LEN(d$)
  h$ = "RIFF" + MKL$(36 + n&) + "WAVEfmt " + MKL$(16) + MKI$(1) + MKI$(1) + MKL$(rate&) + MKL$(rate&) + MKI$(1) + MKI$(8) + "data" + MKL$(n&)
  f = FREEFILE
  OPEN t$ FOR OUTPUT AS #f: CLOSE #f
  OPEN t$ FOR BINARY AS #f
  PUT #f, 1, h$
  PUT #f, , d$
  CLOSE #f
  dqbSndId(Slot) = SOUND(t$)
  dqbSndLen(Slot) = n&
  KILL t$
END SUB

FUNCTION DQBloadSound (Slot, FileName$)
  IF Slot < 1 OR Slot > dqbNSnd THEN dqbErr = 19: DQBloadSound = 6: EXIT FUNCTION
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBloadSound = 1: EXIT FUNCTION
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  IF LOF(f) < 44 THEN CLOSE #f: dqbErr = 11: DQBloadSound = 3: EXIT FUNCTION
  h$ = SPACE$(12): GET #f, 1, h$
  IF LEFT$(h$, 4) <> "RIFF" OR MID$(h$, 9, 4) <> "WAVE" THEN CLOSE #f: dqbErr = 11: DQBloadSound = 3: EXIT FUNCTION
  ' look for the "fmt " and "data" chunks
  p& = 13: fmtok = 0: dlen& = -1
  DO WHILE p& + 8 <= LOF(f) + 1
    c$ = SPACE$(8): GET #f, p&, c$
    cl& = CVL(MID$(c$, 5, 4))
    IF LEFT$(c$, 4) = "fmt " THEN
      fm$ = SPACE$(16): GET #f, p& + 8, fm$
      IF CVI(LEFT$(fm$, 2)) = 1 AND CVI(MID$(fm$, 3, 2)) = 1 AND CVI(MID$(fm$, 15, 2)) = 8 THEN fmtok = -1
    ELSEIF LEFT$(c$, 4) = "data" THEN
      dlen& = cl&
      EXIT DO
    END IF
    p& = p& + 8 + cl& + (cl& AND 1)
  LOOP
  CLOSE #f
  IF fmtok = 0 THEN dqbErr = 17: DQBloadSound = 4: EXIT FUNCTION
  IF dlen& < 0 THEN dqbErr = 11: DQBloadSound = 3: EXIT FUNCTION
  IF dlen& > 65535 THEN dqbErr = 18: DQBloadSound = 5: EXIT FUNCTION
  dqbSndId(Slot) = SOUND(FileName$)
  dqbSndLen(Slot) = dlen&
  DQBloadSound = 0
END FUNCTION

FUNCTION DQBloadRawSound (Slot, FileName$, Offset&, Length)
  IF Slot < 1 OR Slot > dqbNSnd THEN dqbErr = 19: DQBloadRawSound = 4: EXIT FUNCTION
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBloadRawSound = 1: EXIT FUNCTION
  n& = dqbU16&(Length)
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  IF Offset& < 0 OR Offset& + n& > LOF(f) THEN CLOSE #f: dqbErr = 20: DQBloadRawSound = 3: EXIT FUNCTION
  d$ = SPACE$(n&)
  IF n& > 0 THEN GET #f, Offset& + 1, d$
  CLOSE #f
  dqbRawToSlot Slot, d$, 11025
  DQBloadRawSound = 0
END FUNCTION

FUNCTION DQBinUse (Voice)
  c = dqbChan(Voice)
  IF c = 0 OR dqbSB = 0 THEN DQBinUse = 0 ELSE DQBinUse = SOUND(c)
END FUNCTION

SUB DQBplaySound (SoundNum, Voice, Freq, LoopFlag)
  IF dqbSB = 0 OR SoundNum < 1 OR SoundNum > dqbNSnd THEN EXIT SUB
  IF dqbSndId(SoundNum) = 0 THEN EXIT SUB
  c = dqbChan(Voice)
  IF c = 0 THEN EXIT SUB
  v = 63: IF Voice <= 32 THEN v = dqbVVol(Voice)
  vol = (v * 255& * dqbMVol) \ (63 * 15)
  f& = dqbU16&(Freq)
  IF f& < 1000 THEN f& = 1000
  IF LoopFlag THEN
    SOUND PLAY dqbSndId(SoundNum), c, vol, 0, f&, LOOP
  ELSE
    SOUND PLAY dqbSndId(SoundNum), c, vol, 0, f&
  END IF
END SUB

' voice volume 0-63 (used from the next DQBplaySound on that voice)
SUB DQBsetVoiceVol (Voice, NewVol)
  IF Voice >= 1 AND Voice <= 32 THEN
    v = NewVol: IF v < 0 THEN v = 0
    IF v > 63 THEN v = 63
    dqbVVol(Voice) = v
  END IF
END SUB

' master volume 0-15
SUB DQBsetVolume (Volume)
  v = Volume: IF v < 0 THEN v = 0
  IF v > 15 THEN v = 15
  dqbMVol = v
END SUB

' QBLike cannot pause a sound: DQBpauseSound stops them
SUB DQBpauseSound
  IF dqbSB THEN SOUND STOP
END SUB

SUB DQBresumeSound
END SUB

SUB DQBstopVoice (Voice)
  c = dqbChan(Voice)
  IF dqbSB AND c > 0 THEN SOUND STOP c
END SUB

SUB DQBremoveSB
  IF dqbSB THEN SOUND STOP
  dqbSB = 0
END SUB

'=============================================================================
' 3D module (scanline triangles)
'=============================================================================

' kind: 0 flat, 1 flat blended, 2 gouraud, 3 gouraud blended,
'       4 textured, 5 textured blended, 6 textured bilinear
' p, q: color (gouraud) or u, v (textured) of each vertex
SUB dqbTriX (Layer, ax, ay, bx, by, cx, cy, ap!, aq!, bp!, bq!, cp!, cq!, kind, Col, ts, toff, BMap)
  x1 = ax: y1 = ay: x2 = bx: y2 = by: x3 = cx: y3 = cy
  p1! = ap!: q1! = aq!: p2! = bp!: q2! = bq!: p3! = cp!: q3! = cq!
  IF y2 < y1 THEN SWAP x1, x2: SWAP y1, y2: SWAP p1!, p2!: SWAP q1!, q2!
  IF y3 < y1 THEN SWAP x1, x3: SWAP y1, y3: SWAP p1!, p3!: SWAP q1!, q3!
  IF y3 < y2 THEN SWAP x2, x3: SWAP y2, y3: SWAP p2!, p3!: SWAP q2!, q3!
  IF y3 = y1 THEN EXIT SUB
  ys = y1: IF ys < dqbCY1 THEN ys = dqbCY1
  IF ys < 0 THEN ys = 0
  ye = y3: IF ye > dqbCY2 THEN ye = dqbCY2
  IF ye > 199 THEN ye = 199
  IF ys > ye THEN EXIT SUB
  lx = dqbCX1: IF lx < 0 THEN lx = 0
  rx = dqbCX2: IF rx > 319 THEN rx = 319
  c = Col AND 255
  IF kind >= 4 THEN
    dqbHdr ts, toff, tw0, th
    IF th <= 0 THEN DEF SEG: EXIT SUB
    tb& = dqbU16&(toff) + 4: tw = dqbTexW: tm = tw - 1
  END IF
  IF dqbVGA = 0 THEN dqbVGAchk
  IF Layer <> dqbML THEN dqbUse Layer
  FOR y = ys TO ye
    t! = (y - y1) / (y3 - y1)
    xa! = x1 + (x3 - x1) * t!: pa! = p1! + (p3! - p1!) * t!: qa! = q1! + (q3! - q1!) * t!
    IF y < y2 THEN
      t! = (y - y1) / (y2 - y1)
      xb! = x1 + (x2 - x1) * t!: pb! = p1! + (p2! - p1!) * t!: qb! = q1! + (q2! - q1!) * t!
    ELSEIF y3 = y2 THEN
      xb! = x2: pb! = p2!: qb! = q2!
    ELSE
      t! = (y - y2) / (y3 - y2)
      xb! = x2 + (x3 - x2) * t!: pb! = p2! + (p3! - p2!) * t!: qb! = q2! + (q3! - q2!) * t!
    END IF
    IF xa! > xb! THEN SWAP xa!, xb!: SWAP pa!, pb!: SWAP qa!, qb!
    xl = CINT(xa!): xr = CINT(xb!)
    cl = xl: IF cl < lx THEN cl = lx
    cr = xr: IF cr > rx THEN cr = rx
    IF cl <= cr THEN
      IF kind = 0 THEN
        LINE (cl, y)-(cr, y), c
      ELSE
        IF xr > xl THEN dp! = (pb! - pa!) / (xr - xl): dq! = (qb! - qa!) / (xr - xl) ELSE dp! = 0: dq! = 0
        pp! = pa! + dp! * (cl - xl): qq! = qa! + dq! * (cl - xl)
        FOR x = cl TO cr
          SELECT CASE kind
          CASE 1
            PSET (x, y), dqbBlend(BMap, c, POINT(x, y))
          CASE 2
            PSET (x, y), CINT(pp!) AND 255
          CASE 3
            PSET (x, y), dqbBlend(BMap, CINT(pp!) AND 255, POINT(x, y))
          CASE 4, 5
            tv& = INT(qq!) MOD th: IF tv& < 0 THEN tv& = tv& + th
            p = PEEK(tb& + tv& * tw + (INT(pp!) AND tm))
            IF p THEN
              IF kind = 5 THEN p = dqbBlend(BMap, p, POINT(x, y))
              PSET (x, y), p
            END IF
          CASE 6
            u0& = INT(pp!): v0& = INT(qq!): fu! = pp! - u0&: fv! = qq! - v0&
            ta& = v0& MOD th: IF ta& < 0 THEN ta& = ta& + th
            tb2& = (ta& + 1) MOD th
            t00 = PEEK(tb& + ta& * tw + (u0& AND tm))
            IF t00 THEN
              t10 = PEEK(tb& + ta& * tw + ((u0& + 1) AND tm))
              t01 = PEEK(tb& + tb2& * tw + (u0& AND tm))
              t11 = PEEK(tb& + tb2& * tw + ((u0& + 1) AND tm))
              v! = (t00 * (1 - fu!) + t10 * fu!) * (1 - fv!) + (t01 * (1 - fu!) + t11 * fu!) * fv!
              PSET (x, y), CINT(v!) AND 255
            END IF
          END SELECT
          pp! = pp! + dp!: qq! = qq! + dq!
        NEXT
      END IF
    END IF
  NEXT
  IF kind >= 4 THEN DEF SEG
  IF Layer <> dqbML THEN dqbDone
END SUB

SUB DQBtri (Layer, x1, y1, x2, y2, x3, y3, Col)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, 0, 0, 0, 0, 0, 0, 0, Col, 0, 0, 0
END SUB

SUB DQBbtri (Layer, x1, y1, x2, y2, x3, y3, Col, BMap)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, 0, 0, 0, 0, 0, 0, 1, Col, 0, 0, BMap
END SUB

SUB DQBgtri (Layer, x1, y1, c1, x2, y2, c2, x3, y3, c3)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, CSNG(c1), 0, CSNG(c2), 0, CSNG(c3), 0, 2, 0, 0, 0, 0
END SUB

SUB DQBbgtri (Layer, x1, y1, c1, x2, y2, c2, x3, y3, c3, BMap)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, CSNG(c1), 0, CSNG(c2), 0, CSNG(c3), 0, 3, 0, 0, 0, BMap
END SUB

SUB DQBttri (Layer, x1, y1, x2, y2, x3, y3, u1, v1, u2, v2, u3, v3, TextureSeg, TextureOff)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, CSNG(u1), CSNG(v1), CSNG(u2), CSNG(v2), CSNG(u3), CSNG(v3), 4, 0, TextureSeg, TextureOff, 0
END SUB

SUB DQBbttri (Layer, x1, y1, x2, y2, x3, y3, u1, v1, u2, v2, u3, v3, TextureSeg, TextureOff, BMap)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, CSNG(u1), CSNG(v1), CSNG(u2), CSNG(v2), CSNG(u3), CSNG(v3), 5, 0, TextureSeg, TextureOff, BMap
END SUB

SUB DQBfttri (Layer, x1, y1, x2, y2, x3, y3, u1, v1, u2, v2, u3, v3, TextureSeg, TextureOff)
  dqbTriX Layer, x1, y1, x2, y2, x3, y3, CSNG(u1), CSNG(v1), CSNG(u2), CSNG(v2), CSNG(u3), CSNG(v3), 6, 0, TextureSeg, TextureOff, 0
END SUB

' texture width: rounded down to a power of 2
SUB DQBsetTextureSize (Size)
  s = 1
  DO WHILE s * 2 <= Size AND s < 16384: s = s * 2: LOOP
  dqbTexW = s
END SUB

'=============================================================================
' DATAFILE module
'=============================================================================

FUNCTION DQBopenDataFile (FileName$, Password$)
  IF dqbPkOn THEN dqbErr = 28: DQBopenDataFile = 4: EXIT FUNCTION
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBopenDataFile = 1: EXIT FUNCTION
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  IF LOF(f) < 1032 THEN CLOSE #f: dqbErr = 5: DQBopenDataFile = 2: EXIT FUNCTION
  h$ = SPACE$(8): GET #f, 1, h$
  IF LEFT$(h$, 7) <> "DQBPaCk" THEN CLOSE #f: dqbErr = 11: DQBopenDataFile = 3: EXIT FUNCTION
  dqbPkN = ASC(MID$(h$, 8, 1)) + 1
  FOR i = 0 TO 255: GET #f, , dqbPkOff(i): NEXT
  dqbPkPw = LEFT$(UCASE$(Password$), 8)
  dqbPkF = f: dqbPkOn = -1
  DQBopenDataFile = 0
END FUNCTION

SUB DQBcloseDataFile
  IF dqbPkOn THEN CLOSE #dqbPkF
  dqbPkOn = 0
END SUB

' next byte of the packet file, with the password removed
FUNCTION dqbPkRaw
  IF dqbPkPtr > LEN(dqbPkBuf) THEN
    n& = LOF(dqbPkF) - SEEK(dqbPkF) + 1
    IF n& > 4096 THEN n& = 4096
    IF n& <= 0 THEN dqbPkRaw = 0: EXIT FUNCTION
    dqbPkBuf = SPACE$(n&): GET #dqbPkF, , dqbPkBuf: dqbPkPtr = 1
  END IF
  b = ASC(MID$(dqbPkBuf, dqbPkPtr, 1)): dqbPkPtr = dqbPkPtr + 1
  IF LEN(dqbPkPw) THEN
    dqbPkPP = dqbPkPP + 1: IF dqbPkPP > LEN(dqbPkPw) THEN dqbPkPP = 1
    b = (b - ASC(MID$(dqbPkPw, dqbPkPP, 1))) AND 255
  END IF
  dqbPkRaw = b
END FUNCTION

' next decoded (RLE) byte of the packet
FUNCTION dqbPkByte
  IF dqbPkRep > 0 THEN dqbPkRep = dqbPkRep - 1: dqbPkByte = dqbPkLast: EXIT FUNCTION
  b = dqbPkRaw
  IF b = 255 THEN
    n = dqbPkRaw
    b = dqbPkRaw
    dqbPkRep = (n - 1) AND 255: dqbPkLast = b
  END IF
  dqbPkByte = b
END FUNCTION

' moves to a packet and checks its type: 0 ok, 1 no datafile, 2 bad id, 3 bad data
FUNCTION dqbPkStart (id, tag$)
  IF dqbPkOn = 0 THEN dqbErr = 25: dqbPkStart = 1: EXIT FUNCTION
  IF id < 0 OR id >= dqbPkN THEN dqbErr = 26: dqbPkStart = 2: EXIT FUNCTION
  IF dqbPkOff(id) < 1032 OR dqbPkOff(id) >= LOF(dqbPkF) THEN dqbErr = 26: dqbPkStart = 2: EXIT FUNCTION
  SEEK #dqbPkF, dqbPkOff(id) + 1
  dqbPkPP = 0: dqbPkRep = 0: dqbPkBuf = "": dqbPkPtr = 1
  h$ = ""
  FOR i = 1 TO 4: h$ = h$ + CHR$(dqbPkByte): NEXT
  IF h$ <> tag$ + CHR$(254) THEN dqbErr = 27: dqbPkStart = 3: EXIT FUNCTION
  dqbPkStart = 0
END FUNCTION

FUNCTION dqbPkWord&
  a = dqbPkByte: b = dqbPkByte
  dqbPkWord& = a + b * 256&
END FUNCTION

FUNCTION dqbPkLong&
  a& = dqbPkWord&: b& = dqbPkWord&
  dqbPkLong& = a& + b& * 65536
END FUNCTION

FUNCTION DQBunpackFont (PacketID, Font$)
  r = dqbPkStart(PacketID, "FNT")
  IF r THEN DQBunpackFont = r: EXIT FUNCTION
  s$ = SPACE$(2305)
  FOR i = 1 TO 2305: MID$(s$, i, 1) = CHR$(dqbPkByte): NEXT
  Font$ = s$
  DQBunpackFont = 0
END FUNCTION

FUNCTION DQBunpackImage (PacketID, Layer, x, y)
  r = dqbPkStart(PacketID, "IMG")
  IF r THEN DQBunpackImage = r: EXIT FUNCTION
  w& = dqbPkWord&: h& = dqbPkWord&
  IF w& * h& <= 64000 THEN
    dqbBuf(0) = dqbS16%(w& * 8): dqbBuf(1) = dqbS16%(h&)
    DEF SEG = VARSEG(dqbBuf(0))
    o& = dqbU16&(VARPTR(dqbBuf(0))) + 4
    FOR i& = 0 TO w& * h& - 1: POKE o& + i&, dqbPkByte: NEXT
    DEF SEG
    dqbUse Layer
    dqbClipAll
    PUT (x, y), dqbBuf(0), PSET
    dqbClipBack
    dqbDone
  ELSE
    dqbUse Layer
    FOR j& = 0 TO h& - 1
      FOR i& = 0 TO w& - 1: PSET (x + i&, y + j&), dqbPkByte: NEXT
    NEXT
    dqbDone
  END IF
  DQBunpackImage = 0
END FUNCTION

FUNCTION DQBunpackSprite (PacketID, SpriteSeg, SpriteOff)
  r = dqbPkStart(PacketID, "IMG")
  IF r THEN DQBunpackSprite = r: EXIT FUNCTION
  w& = dqbPkWord&: h& = dqbPkWord&
  DEF SEG = SpriteSeg
  o& = dqbU16&(SpriteOff)
  v& = (w& * 8) AND &HFFFF&
  POKE o&, v& AND 255: POKE o& + 1, v& \ 256
  POKE o& + 2, h& AND 255: POKE o& + 3, (h& \ 256) AND 255
  FOR i& = 0 TO w& * h& - 1: POKE o& + 4 + i&, dqbPkByte: NEXT
  DEF SEG
  DQBunpackSprite = 0
END FUNCTION

FUNCTION DQBunpackSound (PacketID, Slot)
  r = dqbPkStart(PacketID, "SND")
  IF r THEN DQBunpackSound = r: EXIT FUNCTION
  IF Slot < 1 OR Slot > dqbNSnd THEN dqbErr = 19: DQBunpackSound = 4: EXIT FUNCTION
  n& = dqbPkLong&
  IF n& < 0 OR n& > 1048576 THEN dqbErr = 27: DQBunpackSound = 3: EXIT FUNCTION
  d$ = SPACE$(n&)
  FOR i& = 1 TO n&: MID$(d$, i&, 1) = CHR$(dqbPkByte): NEXT
  dqbRawToSlot Slot, d$, 11025
  DQBunpackSound = 0
END FUNCTION

FUNCTION DQBunpackPal (PacketID, Pal$)
  r = dqbPkStart(PacketID, "PAL")
  IF r THEN DQBunpackPal = r: EXIT FUNCTION
  s$ = SPACE$(768)
  FOR i = 1 TO 768: MID$(s$, i, 1) = CHR$(dqbPkByte): NEXT
  Pal$ = s$
  DQBunpackPal = 0
END FUNCTION

FUNCTION DQBunpackBMap (PacketID, BMap)
  r = dqbPkStart(PacketID, "BMA")
  IF r THEN DQBunpackBMap = r: EXIT FUNCTION
  IF BMap < 1 OR BMap > 10 THEN dqbErr = 8: DQBunpackBMap = 4: EXIT FUNCTION
  IF dqbBMOn(BMap) = 0 THEN dqbErr = 8: DQBunpackBMap = 4: EXIT FUNCTION
  n = dqbPkByte
  IF n <> dqbBML(BMap) - dqbBMF(BMap) + 1 THEN dqbErr = 22: DQBunpackBMap = 5: EXIT FUNCTION
  b& = (BMap - 1) * 65536
  FOR i& = 0 TO n * 256& - 1: dqbBM(b& + i&) = dqbPkByte: NEXT
  DQBunpackBMap = 0
END FUNCTION

FUNCTION DQBunpackCursor (PacketID, Cursor$)
  r = dqbPkStart(PacketID, "CUR")
  IF r THEN DQBunpackCursor = r: EXIT FUNCTION
  s$ = SPACE$(64)
  FOR i = 1 TO 64: MID$(s$, i, 1) = CHR$(dqbPkByte): NEXT
  Cursor$ = s$
  DQBunpackCursor = 0
END FUNCTION

FUNCTION DQBunpackUser (PacketID, DataSeg, DataOff)
  r = dqbPkStart(PacketID, "USR")
  IF r THEN DQBunpackUser = r: EXIT FUNCTION
  n& = dqbPkLong&
  DEF SEG = DataSeg
  o& = dqbU16&(DataOff)
  FOR i& = 0 TO n& - 1: POKE o& + i&, dqbPkByte: NEXT
  DEF SEG
  DQBunpackUser = 0
END FUNCTION

'=============================================================================
' IMAGE module: BMP, PCX and BSAVE images, FLI animations
'=============================================================================

FUNCTION DQBloadImage (Layer, x, y, FileName$, Pal$, imgWidth, imgHeight)
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBloadImage = 1: EXIT FUNCTION
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  l& = LOF(f)
  IF l& < 8 THEN CLOSE #f: dqbErr = 11: DQBloadImage = 3: EXIT FUNCTION
  n = 128: IF l& < 128 THEN n = l&
  h$ = SPACE$(n): GET #f, 1, h$
  h$ = h$ + STRING$(128 - n, 0)
  p$ = STRING$(768, 0)
  IF LEFT$(h$, 2) = "BM" THEN
    ' ---- BMP (256 colors): pixels by LOADBMP, palette read here
    hs& = CVL(MID$(h$, 15, 4))
    IF hs& < 40 THEN CLOSE #f: dqbErr = 11: DQBloadImage = 3: EXIT FUNCTION
    w& = CVL(MID$(h$, 19, 4)): hh& = ABS(CVL(MID$(h$, 23, 4)))
    IF CVI(MID$(h$, 29, 2)) <> 8 THEN CLOSE #f: dqbErr = 11: DQBloadImage = 3: EXIT FUNCTION
    nc& = CVL(MID$(h$, 47, 4)): IF nc& <= 0 OR nc& > 256 THEN nc& = 256
    b$ = SPACE$(nc& * 4): GET #f, 15 + hs&, b$
    CLOSE #f
    FOR i = 0 TO nc& - 1
      MID$(p$, i * 3 + 1, 1) = CHR$(ASC(MID$(b$, i * 4 + 3, 1)) \ 4)
      MID$(p$, i * 3 + 2, 1) = CHR$(ASC(MID$(b$, i * 4 + 2, 1)) \ 4)
      MID$(p$, i * 3 + 3, 1) = CHR$(ASC(MID$(b$, i * 4 + 1, 1)) \ 4)
    NEXT
    dqbUse Layer
    dqbClipAll
    LOADBMP FileName$, SCREEN, x, y
    dqbClipBack
    dqbDone
  ELSEIF ASC(h$) = 10 THEN
    ' ---- PCX, 8 bits, 1 plane
    IF ASC(MID$(h$, 4, 1)) <> 8 OR ASC(MID$(h$, 66, 1)) <> 1 OR l& < 130 THEN CLOSE #f: dqbErr = 11: DQBloadImage = 3: EXIT FUNCTION
    w& = CVI(MID$(h$, 9, 2)) - CVI(MID$(h$, 5, 2)) + 1
    hh& = CVI(MID$(h$, 11, 2)) - CVI(MID$(h$, 7, 2)) + 1
    bpl = CVI(MID$(h$, 67, 2))
    d$ = SPACE$(l& - 128): GET #f, 129, d$
    CLOSE #f
    dl& = LEN(d$)
    IF dl& > 769 THEN
      IF ASC(MID$(d$, dl& - 768, 1)) = 12 THEN
        FOR i = 1 TO 768: MID$(p$, i, 1) = CHR$(ASC(MID$(d$, dl& - 768 + i, 1)) \ 4): NEXT
      END IF
    END IF
    dqbUse Layer
    DEF SEG = &HA000
    q& = 1
    FOR r = 0 TO hh& - 1
      py = y + r
      cx = 0
      DO WHILE cx < bpl AND q& <= dl&
        b = ASC(MID$(d$, q&, 1)): q& = q& + 1
        IF b >= 192 THEN
          cnt = b - 192: v = 0
          IF q& <= dl& THEN v = ASC(MID$(d$, q&, 1)): q& = q& + 1
        ELSE
          cnt = 1: v = b
        END IF
        IF py >= 0 AND py <= 199 THEN
          o& = py * 320&
          FOR k = 0 TO cnt - 1
            px = x + cx + k
            IF cx + k < w& AND px >= 0 AND px <= 319 THEN POKE o& + px, v
          NEXT
        END IF
        cx = cx + cnt
      LOOP
    NEXT
    DEF SEG
    dqbDone
  ELSEIF ASC(h$) = &HFD THEN
    ' ---- BSAVE file: a whole screen, or a GET image; the palette may follow
    ln& = dqbU16&(CVI(MID$(h$, 6, 2)))
    IF ln& + 7 > l& OR ln& > 64004 THEN CLOSE #f: dqbErr = 5: DQBloadImage = 2: EXIT FUNCTION
    IF l& >= ln& + 7 + 768 THEN GET #f, ln& + 8, p$
    CLOSE #f
    dqbUse Layer
    IF ln& = 64000 THEN
      DEF SEG = &HA000: BLOAD FileName$, 0: DEF SEG
      w& = 320: hh& = 200
    ELSE
      DEF SEG = VARSEG(dqbBuf(0)): BLOAD FileName$, VARPTR(dqbBuf(0)): DEF SEG
      w& = dqbU16&(dqbBuf(0)) \ 8: hh& = dqbBuf(1)
      dqbClipAll
      PUT (x, y), dqbBuf(0), PSET
      dqbClipBack
    END IF
    dqbDone
  ELSE
    CLOSE #f: dqbErr = 11: DQBloadImage = 3: EXIT FUNCTION
  END IF
  Pal$ = p$
  imgWidth = w&: imgHeight = hh&
  DQBloadImage = 0
END FUNCTION

FUNCTION DQBsaveImage (Layer, x1, y1, x2, y2, FileName$, Pal$, Format)
  ax = x1: bx = x2: IF bx < ax THEN SWAP ax, bx
  ay = y1: by = y2: IF by < ay THEN SWAP ay, by
  IF ax < 0 THEN ax = 0
  IF ay < 0 THEN ay = 0
  IF bx > 319 THEN bx = 319
  IF by > 199 THEN by = 199
  w = bx - ax + 1: h = by - ay + 1
  IF w <= 0 OR h <= 0 THEN dqbErr = 10: DQBsaveImage = 2: EXIT FUNCTION
  p$ = LEFT$(Pal$ + STRING$(768, 0), 768)
  dqbUse Layer
  f = FREEFILE
  SELECT CASE Format
  CASE 0
    IF w = 320 AND h = 200 THEN
      DEF SEG = &HA000: BSAVE FileName$, 0, 64000: DEF SEG
    ELSE
      GET (ax, ay)-(bx, by), dqbBuf
      DEF SEG = VARSEG(dqbBuf(0)): BSAVE FileName$, VARPTR(dqbBuf(0)), 4 + CLNG(w) * h: DEF SEG
    END IF
    OPEN FileName$ FOR BINARY AS #f
    PUT #f, LOF(f) + 1, p$
    CLOSE #f
  CASE 1
    rl = (w + 3) AND NOT 3
    sz& = CLNG(rl) * h
    hd$ = "BM" + MKL$(1078 + sz&) + MKL$(0) + MKL$(1078) + MKL$(40) + MKL$(w) + MKL$(h)
    hd$ = hd$ + MKI$(1) + MKI$(8) + MKL$(0) + MKL$(sz&) + MKL$(2835) + MKL$(2835) + MKL$(256) + MKL$(256)
    pl$ = SPACE$(1024)
    FOR i = 0 TO 255
      MID$(pl$, i * 4 + 1, 4) = CHR$(dqbP8(p$, i * 3 + 3)) + CHR$(dqbP8(p$, i * 3 + 2)) + CHR$(dqbP8(p$, i * 3 + 1)) + CHR$(0)
    NEXT
    OPEN FileName$ FOR OUTPUT AS #f: CLOSE #f
    OPEN FileName$ FOR BINARY AS #f
    PUT #f, 1, hd$
    PUT #f, , pl$
    DEF SEG = &HA000
    FOR r = by TO ay STEP -1
      row$ = STRING$(rl, 0)
      o& = r * 320& + ax
      FOR i = 0 TO w - 1: MID$(row$, i + 1, 1) = CHR$(PEEK(o& + i)): NEXT
      PUT #f, , row$
    NEXT
    DEF SEG
    CLOSE #f
  CASE ELSE
    bpl = (w + 1) AND NOT 1
    hd$ = CHR$(10) + CHR$(5) + CHR$(1) + CHR$(8) + MKI$(0) + MKI$(0) + MKI$(w - 1) + MKI$(h - 1)
    hd$ = hd$ + MKI$(320) + MKI$(200) + STRING$(48, 0) + CHR$(0) + CHR$(1) + MKI$(bpl) + MKI$(1) + STRING$(58, 0)
    OPEN FileName$ FOR OUTPUT AS #f: CLOSE #f
    OPEN FileName$ FOR BINARY AS #f
    PUT #f, 1, hd$
    DEF SEG = &HA000
    FOR r = ay TO by
      o& = r * 320& + ax
      enc$ = ""
      i = 0
      DO WHILE i < bpl
        IF i < w THEN v = PEEK(o& + i) ELSE v = 0
        n = 1
        DO WHILE i + n < bpl AND n < 63
          IF i + n < w THEN v2 = PEEK(o& + i + n) ELSE v2 = 0
          IF v2 <> v THEN EXIT DO
          n = n + 1
        LOOP
        IF n > 1 OR v >= 192 THEN enc$ = enc$ + CHR$(192 + n) + CHR$(v) ELSE enc$ = enc$ + CHR$(v)
        i = i + n
      LOOP
      PUT #f, , enc$
    NEXT
    DEF SEG
    pl$ = CHR$(12) + SPACE$(768)
    FOR i = 1 TO 768: MID$(pl$, i + 1, 1) = CHR$(dqbP8(p$, i)): NEXT
    PUT #f, , pl$
    CLOSE #f
  END SELECT
  dqbDone
  DQBsaveImage = 0
END FUNCTION

' 8-bit value of the i-th byte of a 0-63 palette string
FUNCTION dqbP8 (p$, i)
  v = ASC(MID$(p$, i, 1)) AND 63
  dqbP8 = v * 4 + v \ 16
END FUNCTION

FUNCTION DQBopenFLI (FileName$, Frames, Speed)
  IF dqbFliOn THEN dqbErr = 21: DQBopenFLI = 5: EXIT FUNCTION
  dqbFileExists FileName$, ok
  IF ok = 0 THEN dqbErr = 4: DQBopenFLI = 2: EXIT FUNCTION
  f = FREEFILE
  OPEN FileName$ FOR BINARY AS #f
  IF LOF(f) < 128 THEN CLOSE #f: dqbErr = 5: DQBopenFLI = 3: EXIT FUNCTION
  h$ = SPACE$(128): GET #f, 1, h$
  IF CVI(MID$(h$, 5, 2)) <> &HAF11 THEN CLOSE #f: dqbErr = 11: DQBopenFLI = 4: EXIT FUNCTION
  dqbFliFrames = CVI(MID$(h$, 7, 2))
  dqbFliW = CVI(MID$(h$, 9, 2)): dqbFliH = CVI(MID$(h$, 11, 2))
  dqbFliSpeed = CVI(MID$(h$, 17, 2))
  Frames = dqbFliFrames: Speed = dqbFliSpeed
  dqbFliF = f: dqbFliOn = -1
  dqbFliCur = 0: dqbFliPos = 129: dqbFliPos1 = 0
  DQBopenFLI = 0
END FUNCTION

SUB DQBcloseFLI
  IF dqbFliOn THEN CLOSE #dqbFliF
  dqbFliOn = 0
END SUB

' decodes the next frame of the FLI file onto a layer
SUB DQBplayFLIstep (Layer)
  IF dqbFliOn = 0 THEN EXIT SUB
  f = dqbFliF
  IF dqbFliCur > dqbFliFrames THEN
    IF dqbFliPos1 > 0 THEN dqbFliPos = dqbFliPos1: dqbFliCur = 1 ELSE dqbFliPos = 129: dqbFliCur = 0
  END IF
  IF dqbFliPos + 16 > LOF(f) + 1 THEN dqbFliPos = 129: dqbFliCur = 0
  c$ = SPACE$(16): GET #f, dqbFliPos, c$
  sz& = CVL(LEFT$(c$, 4))
  IF sz& < 16 OR dqbFliPos + sz& > LOF(f) + 1 THEN dqbFliCur = dqbFliFrames + 1: EXIT SUB
  IF CVI(MID$(c$, 5, 2)) = &HF1FA AND sz& > 16 THEN
    nch = CVI(MID$(c$, 7, 2))
    d$ = SPACE$(sz& - 16): GET #f, , d$
    dqbUse Layer
    DEF SEG = &HA000
    p& = 1
    FOR k = 1 TO nch
      IF p& + 5 > LEN(d$) THEN EXIT FOR
      cs& = CVL(MID$(d$, p&, 4)): ct = CVI(MID$(d$, p& + 4, 2))
      q& = p& + 6
      SELECT CASE ct
      CASE 4, 11
        np = CVI(MID$(d$, q&, 2)): q& = q& + 2
        idx = 0
        FOR pk = 1 TO np
          idx = idx + ASC(MID$(d$, q&, 1)): n = ASC(MID$(d$, q& + 1, 1)): q& = q& + 2
          IF n = 0 THEN n = 256
          OUT &H3C8, idx AND 255
          FOR j = 1 TO n * 3
            v = ASC(MID$(d$, q&, 1)): q& = q& + 1
            IF ct = 4 THEN v = v \ 4
            OUT &H3C9, v
          NEXT
          idx = idx + n
        NEXT
      CASE 13
        LINE (0, 0)-(319, 199), 0, BF
      CASE 15
        FOR r = 0 TO dqbFliH - 1
          q& = q& + 1
          x = 0: o& = r * 320&
          DO WHILE x < dqbFliW
            s = ASC(MID$(d$, q&, 1)): q& = q& + 1
            IF s > 127 THEN s = s - 256
            IF s > 0 THEN
              v = ASC(MID$(d$, q&, 1)): q& = q& + 1
              LINE (x, r)-(x + s - 1, r), v
              x = x + s
            ELSEIF s < 0 THEN
              FOR j = 0 TO -s - 1: POKE o& + x + j, ASC(MID$(d$, q& + j, 1)): NEXT
              q& = q& - s: x = x - s
            ELSE
              EXIT DO
            END IF
          LOOP
        NEXT
      CASE 12
        sl = CVI(MID$(d$, q&, 2)): nl = CVI(MID$(d$, q& + 2, 2)): q& = q& + 4
        FOR r = sl TO sl + nl - 1
          np = ASC(MID$(d$, q&, 1)): q& = q& + 1
          x = 0: o& = r * 320&
          FOR pk = 1 TO np
            x = x + ASC(MID$(d$, q&, 1)): s = ASC(MID$(d$, q& + 1, 1)): q& = q& + 2
            IF s > 127 THEN s = s - 256
            IF s > 0 THEN
              FOR j = 0 TO s - 1: POKE o& + x + j, ASC(MID$(d$, q& + j, 1)): NEXT
              q& = q& + s: x = x + s
            ELSEIF s < 0 THEN
              v = ASC(MID$(d$, q&, 1)): q& = q& + 1
              LINE (x, r)-(x - s - 1, r), v
              x = x - s
            END IF
          NEXT
        NEXT
      CASE 16
        FOR i& = 0 TO 63999: POKE i&, ASC(MID$(d$, q& + i&, 1)): NEXT
      END SELECT
      p& = p& + cs&
    NEXT
    DEF SEG
    dqbDone
  END IF
  dqbFliPos = dqbFliPos + sz&
  dqbFliCur = dqbFliCur + 1
  IF dqbFliCur = 1 THEN dqbFliPos1 = dqbFliPos
END SUB

FUNCTION DQBplayFLI (FileName$, BufLayer, KeyStop, LoopFlag)
  r = DQBopenFLI(FileName$, fr, sp)
  IF r THEN DQBplayFLI = r: EXIT FUNCTION
  lp = LoopFlag
  IF KeyStop = 0 OR dqbKeyOn = 0 THEN lp = 0
  IF sp < 1 THEN sp = 1
  n = 0
  DO
    DQBplayFLIstep BufLayer
    IF BufLayer <> 0 THEN DQBcopyLayer BufLayer, 0
    DQBwait sp
    n = n + 1
    IF dqbKeyOn AND KeyStop <> 0 THEN
      IF DQBkey(KeyStop) THEN EXIT DO
    END IF
    IF n >= fr AND lp = 0 THEN EXIT DO
  LOOP
  DQBcloseFLI
  DQBplayFLI = 0
END FUNCTION

'=============================================================================
' Built-in data: the 8x8 BIOS font and the scancode to ASCII tables
'=============================================================================

SUB dqbLoadBIOS
  f$ = ""
  f$ = f$ + "00000000000000007E81A581BD99817E7EFFDBFFC3E7FF7E6CFEFEFE7C38100010387CFE7C381000387C38FEFED610381010387CFE7C10380000183C3C180000"
  f$ = f$ + "FFFFE7C3C3E7FFFF003C664242663C00FFC399BDBD99C3FF0F070F7DCCCCCC783C6666663C187E183F333F3070F0E0007F637F636367E6C018DB3CE7E73CDB18"
  f$ = f$ + "80E0F8FEF8E08000020E3EFE3E0E0200183C7E18187E3C1866666666660066007FDBDB7B1B1B1B003E63386C6C38CC78000000007E7E7E00183C7E187E3C18FF"
  f$ = f$ + "183C7E1818181800181818187E3C180000180CFE0C180000003060FE603000000000C0C0C0FE0000002466FF6624000000183C7EFFFF000000FFFF7E3C180000"
  f$ = f$ + "000000000000000030787830300030006C6C6C00000000006C6CFE6CFE6C6C00307CC0780CF8300000C6CC183066C600386C3876DCCC76006060C00000000000"
  f$ = f$ + "1830606060301800603018181830600000663CFF3C660000003030FC303000000000000000303060000000FC000000000000000000303000060C183060C08000"
  f$ = f$ + "7CC6CEDEF6E67C00307030303030FC0078CC0C3860CCFC0078CC0C380CCC78001C3C6CCCFE0C1E00FCC0F80C0CCC78003860C0F8CCCC7800FCCC0C1830303000"
  f$ = f$ + "78CCCC78CCCC780078CCCC7C0C18700000303000003030000030300000303060183060C0603018000000FC0000FC00006030180C1830600078CC0C1830003000"
  f$ = f$ + "7CC6DEDEDEC078003078CCCCFCCCCC00FC66667C6666FC003C66C0C0C0663C00F86C6666666CF800FE6268786862FE00FE6268786860F0003C66C0C0CE663E00"
  f$ = f$ + "CCCCCCFCCCCCCC0078303030303078001E0C0C0CCCCC7800E6666C786C66E600F06060606266FE00C6EEFEFED6C6C600C6E6F6DECEC6C600386CC6C6C66C3800"
  f$ = f$ + "FC66667C6060F00078CCCCCCDC781C00FC66667C6C66E60078CC603018CC7800FCB4303030307800CCCCCCCCCCCCFC00CCCCCCCCCC783000C6C6C6D6FEEEC600"
  f$ = f$ + "C6C66C38386CC600CCCCCC7830307800FEC68C183266FE007860606060607800C06030180C060200781818181818780010386CC60000000000000000000000FF"
  f$ = f$ + "30301800000000000000780C7CCC7600E060607C6666DC00000078CCC0CC78001C0C0C7CCCCC7600000078CCFCC07800386C60F06060F000000076CCCC7C0CF8"
  f$ = f$ + "E0606C766666E60030007030303078000C000C0C0CCCCC78E060666C786CE60070303030303078000000CCFEFED6C6000000F8CCCCCCCC00000078CCCCCC7800"
  f$ = f$ + "0000DC66667C60F0000076CCCC7C0C1E0000DC766660F00000007CC0780CF80010307C30303418000000CCCCCCCC76000000CCCCCC7830000000C6D6FEFE6C00"
  f$ = f$ + "0000C66C386CC6000000CCCCCC7C0CF80000FC983064FC001C3030E030301C001818180018181800E030301C3030E00076DC0000000000000010386CC6C6FE00"
  f$ = f$ + "78CCC0CC78180C7800CC00CCCCCC7E001C0078CCFCC078007EC33C063E663F00CC00780C7CCC7E00E000780C7CCC7E003030780C7CCC7E00000078C0C0780C38"
  f$ = f$ + "7EC33C667E603C00CC0078CCFCC07800E00078CCFCC07800CC007030303078007CC6381818183C00E000703030307800C6386CC6FEC6C60030300078CCFCCC00"
  f$ = f$ + "1C00FC607860FC0000007F0C7FCC7F003E6CCCFECCCCCE0078CC0078CCCC780000CC0078CCCC780000E00078CCCC780078CC00CCCCCC7E0000E000CCCCCC7E00"
  f$ = f$ + "00CC00CCCC7C0CF8C3183C66663C1800CC00CCCCCCCC780018187EC0C07E1818386C64F060E6FC00CCCC78FC30FC3030F8CCCCFAC6CFC6C70E1B183C1818D870"
  f$ = f$ + "1C00780C7CCC7E003800703030307800001C0078CCCC7800001C00CCCCCC7E0000F800F8CCCCCC00FC00CCECFCDCCC003C6C6C3E007E0000386C6C38007C0000"
  f$ = f$ + "30003060C0CC7800000000FCC0C00000000000FC0C0C0000C3C6CCDE3366CC0FC3C6CCDB376FCF031818001818181800003366CC6633000000CC663366CC0000"
  f$ = f$ + "228822882288228855AA55AA55AA55AADB77DBEEDB77DBEE181818181818181818181818F81818181818F818F818181836363636F636363600000000FE363636"
  f$ = f$ + "0000F818F81818183636F606F636363636363636363636360000FE06F63636363636F606FE00000036363636FE0000001818F818F800000000000000F8181818"
  f$ = f$ + "181818181F00000018181818FF00000000000000FF181818181818181F18181800000000FF00000018181818FF18181818181F181F1818183636363637363636"
  f$ = f$ + "363637303F00000000003F30373636363636F700FF0000000000FF00F736363636363730373636360000FF00FF0000003636F700F73636361818FF00FF000000"
  f$ = f$ + "36363636FF0000000000FF00FF18181800000000FF363636363636363F00000018181F181F00000000001F181F181818000000003F36363636363636FF363636"
  f$ = f$ + "1818FF18FF18181818181818F8000000000000001F181818FFFFFFFFFFFFFFFF00000000FFFFFFFFF0F0F0F0F0F0F0F00F0F0F0F0F0F0F0FFFFFFFFF00000000"
  f$ = f$ + "000076DCC8DC76000078CCF8CCF8C0C000FCCCC0C0C0C00000FE6C6C6C6C6C00FCCC603060CCFC0000007ED8D8D8700000666666667C60C00076DC1818181800"
  f$ = f$ + "FC3078CCCC7830FC386CC6FEC66C3800386CC6C66C6CEE001C30187CCCCC780000007EDBDB7E0000060C7EDBDB7E60C03860C0F8C060380078CCCCCCCCCCCC00"
  f$ = f$ + "00FC00FC00FC00003030FC303000FC00603018306000FC00183060301800FC000E1B1B18181818181818181818D8D870303000FC003030000076DC0076DC0000"
  f$ = f$ + "386C6C3800000000000000181800000000000000180000000F0C0C0CEC6C3C1C786C6C6C6C000000701830607800000000003C3C3C3C00000000000000000000"
  FOR i = 0 TO 2047: dqbFont(i) = VAL("&H" + MID$(f$, i * 2 + 1, 2)): NEXT
  FOR i = 0 TO 256: dqbCLen(i) = 8: NEXT
  dqbFontStyles
END SUB

SUB dqbLoadKeyMap
  k$ = ""
  k$ = k$ + "1B313233343536373839302D3D080871776572747975696F705B5D0D006173646667686A6B6C3B2760005C7A786376626E6D2C2E2F002A002000000000000000"
  k$ = k$ + "0000000000003738392D3435362B313233302E000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
  k$ = k$ + "1B21402324255E262A28295F3D080851574552545955494F507B7D0D004153444647484A4B4C3A637E007C5A584356424E4D3C3E3F0000002000000000000000"
  k$ = k$ + "0000000000000000002D0035002B0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
  dqbKM = ""
  FOR i = 0 TO 255: dqbKM = dqbKM + CHR$(VAL("&H" + MID$(k$, i * 2 + 1, 2))): NEXT
END SUB
