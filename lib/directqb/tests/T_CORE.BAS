' DirectQB layer test: core, drawing, sprites, bits, palette
DEFINT A-Z
'$INCLUDE: '..\DIRECTQB.BI'
DECLARE SUB Check (name$, got&, want&)
DIM SHARED fails, tests
OPEN "T_CORE.LOG" FOR OUTPUT AS #9
IF DQBinit(3, 2, 64) THEN PRINT #9, "init failed": END
Check "init again", DQBinit(1, 0, 0), 4
Check "ver", DQBver, &H13D
DQBinitVGA
' --- layers
DQBclearLayer 1
DQBboxf 1, 10, 10, 50, 50, 4
Check "point layer1", DQBpoint(1, 20, 20), 4
Check "video untouched", DQBpoint(VIDEO, 20, 20), 0
DQBcopyLayer 1, VIDEO
Check "copy layer", DQBpoint(VIDEO, 20, 20), 4
DQBclearLayer 2
DQBpset 2, 100, 100, 7
DQBcopyTransLayer 2, VIDEO
Check "trans keeps", DQBpoint(VIDEO, 20, 20), 4
Check "trans copies", DQBpoint(VIDEO, 100, 100), 7
DQBcopyHitLayer 2, 3, 9
Check "hit color", DQBpoint(3, 100, 100), 9
Check "hit zero", DQBpoint(3, 20, 20), 0
' --- base layers
Check "base set", DQBsetBaseLayer(B0) <> 0, -1
Check "base twice", DQBsetBaseLayer(B0), 0
DQBpset B0, 5, 5, 33
Check "base point", DQBpoint(B0, 5, 5), 33
Check "base not video", DQBpoint(VIDEO, 5, 5), 0
' --- mapLayer + POKE
s = DQBmapLayer(2)
DEF SEG = s: POKE 320 * 3 + 3, 77: DEF SEG
Check "map poke", DQBpoint(2, 3, 3), 77
DQBpset VIDEO, 1, 1, 5
s = DQBmapLayer(VIDEO)
Check "map pset video", DQBpoint(VIDEO, 1, 1), 5
' --- clipping
DQBclearLayer VIDEO
DQBsetClipBox 100, 100, 200, 150
DQBline VIDEO, 0, 120, 319, 120, 15
Check "clip line in", DQBpoint(VIDEO, 150, 120), 15
Check "clip line out", DQBpoint(VIDEO, 50, 120), 0
DQBpset VIDEO, 10, 10, 3
Check "clip pset", DQBpoint(VIDEO, 10, 10), 0
DQBsetClipBox 0, 0, 319, 199
DQBpset VIDEO, 10, 10, 3
Check "unclip pset", DQBpoint(VIDEO, 10, 10), 3
' --- get/put
DIM spr(DQBsize(0, 0, 15, 15) \ 2)
Check "size", DQBsize(0, 0, 15, 15), 260
DQBclearLayer 1
DQBboxf 1, 0, 0, 15, 15, 0
DQBboxf 1, 4, 4, 11, 11, 12
DQBpset 1, 0, 15, 2
DQBget 1, 0, 0, 15, 15, VARSEG(spr(0)), VARPTR(spr(0))
Check "spr hdr w", spr(0), 128
DQBclearLayer VIDEO
DQBboxf VIDEO, 0, 0, 319, 199, 1
DQBput VIDEO, 100, 50, VARSEG(spr(0)), VARPTR(spr(0))
Check "put trans bg", DQBpoint(VIDEO, 100, 50), 1
Check "put pix", DQBpoint(VIDEO, 106, 56), 12
DQBsetSolidPut
DQBput VIDEO, 200, 50, VARSEG(spr(0)), VARPTR(spr(0))
Check "put solid", DQBpoint(VIDEO, 200, 50), 0
DQBsetTransPut
DQBmPut VIDEO, 0, 100, VARSEG(spr(0)), VARPTR(spr(0)), HOR
Check "mput hor", DQBpoint(VIDEO, 15, 115), 2
DQBmPut VIDEO, 20, 100, VARSEG(spr(0)), VARPTR(spr(0)), VER
Check "mput ver", DQBpoint(VIDEO, 20, 100), 2
DQBmPut VIDEO, 40, 100, VARSEG(spr(0)), VARPTR(spr(0)), HOR + VER
Check "mput both", DQBpoint(VIDEO, 55, 100), 2
DQBsPut VIDEO, 60, 100, VARSEG(spr(0)), VARPTR(spr(0)), 32, 32
Check "sput", DQBpoint(VIDEO, 60 + 16, 100 + 16), 12
Check "sput corner", DQBpoint(VIDEO, 60, 131), 2
DQBhPut VIDEO, 100, 100, VARSEG(spr(0)), VARPTR(spr(0)), 14
Check "hput", DQBpoint(VIDEO, 106, 106), 14
Check "hput trans", DQBpoint(VIDEO, 101, 101), 1
DQBrPut VIDEO, 120, 100, VARSEG(spr(0)), VARPTR(spr(0)), 0, 100
Check "rput 0", DQBpoint(VIDEO, 126, 106), 12
DQBrPut VIDEO, 140, 100, VARSEG(spr(0)), VARPTR(spr(0)), 64, 100
Check "rput 64 center", DQBpoint(VIDEO, 148, 108), 12
DQBtPut VIDEO, 160, 100, VARSEG(spr(0)), VARPTR(spr(0)), BIT.OR
Check "tput or", DQBpoint(VIDEO, 166, 106), 13
DQBtPut VIDEO, 180, 100, VARSEG(spr(0)), VARPTR(spr(0)), BIT.AND
Check "tput and", DQBpoint(VIDEO, 186, 106), 0
Check "tput and trans", DQBpoint(VIDEO, 181, 101), 1
DQBpPut VIDEO, 200, 100, VARSEG(spr(0)), VARPTR(spr(0)), &H55
Check "pput on", DQBpoint(VIDEO, 204, 104), 12
Check "pput off", DQBpoint(VIDEO, 205, 104), 1
DQBxPut 1, 0, 0, 15, 15, VIDEO, 220, 100
Check "xput", DQBpoint(VIDEO, 226, 106), 12
DQBfPut VIDEO, 240, 100, VARSEG(spr(0)), VARPTR(spr(0))
Check "fput", DQBpoint(VIDEO, 240, 100), 0
' clipped put
DQBput VIDEO, -8, -8, VARSEG(spr(0)), VARPTR(spr(0))
Check "put clipped", DQBpoint(VIDEO, 0, 0), 12
' putOver
DIM back(DQBsize(0, 0, 31, 31) \ 2)
DQBboxf 1, 0, 0, 31, 31, 5
DQBget 1, 0, 0, 31, 31, VARSEG(back(0)), VARPTR(back(0))
DQBputOver VARSEG(back(0)), VARPTR(back(0)), 20, 20, VARSEG(spr(0)), VARPTR(spr(0))
DQBput VIDEO, 0, 150, VARSEG(back(0)), VARPTR(back(0))
Check "putover", DQBpoint(VIDEO, 26, 176), 12
Check "putover back", DQBpoint(VIDEO, 2, 152), 5
' collisions
Check "collide box", DQBcollide(0, 0, VARSEG(spr(0)), VARPTR(spr(0)), 15, 15, VARSEG(spr(0)), VARPTR(spr(0))), -1
Check "collide none", DQBcollide(0, 0, VARSEG(spr(0)), VARPTR(spr(0)), 16, 0, VARSEG(spr(0)), VARPTR(spr(0))), 0
DQBsetCollideMethod PIXEL
Check "collide pixel no", DQBcollide(0, 0, VARSEG(spr(0)), VARPTR(spr(0)), 13, 0, VARSEG(spr(0)), VARPTR(spr(0))), 0
Check "collide pixel yes", DQBcollide(0, 0, VARSEG(spr(0)), VARPTR(spr(0)), 6, 6, VARSEG(spr(0)), VARPTR(spr(0))), -1
DQBclearLayer 3
DQBpset 3, 107, 57, 44
Check "collide layer", DQBcollideOnLayer(3, 100, 50, VARSEG(spr(0)), VARPTR(spr(0))), 44
Check "collide layer no", DQBcollideOnLayer(3, 0, 0, VARSEG(spr(0)), VARPTR(spr(0))), 0
' --- scroll
DQBclearLayer 1
DQBpset 1, 10, 10, 9
DQBscroll 1, 5, 2
Check "scroll", DQBpoint(1, 15, 12), 9
DQBscroll 1, -5, -2
Check "scroll back", DQBpoint(1, 10, 10), 9
DQBscrollArea 1, 0, 0, 50, 50, UP
Check "scrollarea up", DQBpoint(1, 10, 9), 9
DQBscrollArea 1, 0, 0, 50, 50, RIGHT
Check "scrollarea right", DQBpoint(1, 11, 9), 9
' --- paint / ellipse / gline
DQBclearLayer 1
DQBbox 1, 10, 10, 30, 30, 4
DQBpaint 1, 20, 20, 6
Check "paint", DQBpoint(1, 20, 20), 6
Check "box", DQBpoint(1, 10, 20), 4
DQBellipse 1, 100, 100, 20, 10, 8
Check "ellipse", DQBpoint(1, 120, 100), 8
Check "ellipse top", DQBpoint(1, 100, 90), 8
DQBgline 1, 0, 190, 9, 190, 10, 19
Check "gline first", DQBpoint(1, 0, 190), 10
Check "gline last", DQBpoint(1, 9, 190), 19
' --- bits
Check "setbit", DQBsetBit(0, 15), -32768
Check "resetbit", DQBresetBit(-1, 0), -2
Check "readbit", DQBreadBit(4, 2), -1
Check "togglebit", DQBtoggleBit(5, 0), 4
Check "shl", DQBshiftLeft(1, 4), 16
Check "shr", DQBshiftRight(-32768, 15), 1
Check "angle", DQBangle(0, 0, 10, 0), 64
Check "angle2", DQBangle(10, 0, 0, 0), 192
' --- palette
DQBsetCol 200, 10, 20, 30
DQBgetCol 200, r, g, b
Check "getcol", r * 10000& + g * 100 + b, 102030
Check "findcol", DQBfindCol(10, 20, 30), 200
DQBgetPal p$
Check "getpal len", LEN(p$), 768
Check "findpalcol", DQBfindPalCol(p$, 10, 20, 30), 200
DQBpalRotate 200, 201, FORWARD
DQBgetCol 201, r, g, b
Check "rotate", r, 10
DQBsetPal p$
DQBfadeStepTo 63, 63, 63
DQBgetCol 0, r, g, b
Check "fadestep", r, 1
DQBfadeIn p$
DQBgetCol 0, r, g, b
Check "fadein", r, 0
' --- text
Check "len", DQBlen("ABC"), 24
DQBsetTextSpacing 1
Check "len spacing", DQBlen("ABC"), 27
DQBsetTextSpacing 0
DQBclearLayer 1
DQBprint 1, "A", 0, 0, 15
Check "print A", DQBpoint(1, 2, 0), 15
DQBprints 1, "A", 0, 10, 15, SOLID
DQBsetTextBackCol 3
DQBprints 1, "A", 20, 10, 15, SOLID
Check "print solid bg", DQBpoint(1, 20, 10), 3
DQBprint 1, "AB", CENTERED, 50, 15
Check "centered", DQBpoint(1, 154, 50), 15
' --- datafile memory
DIM buf(10)
buf(0) = 1234
DQBpoke VARSEG(buf(0)), VARPTR(buf(0)), 100, 2
buf(0) = 0
DQBpeek VARSEG(buf(0)), VARPTR(buf(0)), 100, 2
Check "peek/poke", buf(0), 1234
' --- sort
DIM rec(4)
rec(0) = 5: rec(1) = -2: rec(2) = 9: rec(3) = 0: rec(4) = 1
DQBsort VARSEG(rec(0)), VARPTR(rec(0)), 5, 2, 0
Check "sort", rec(0) * 1000& + rec(1) * 100 + rec(4), -2000 + 0 + 9
' --- blending
Check "bmap create", DQBcreateBMap(1, 40, 41), 0
Check "bmap again", DQBcreateBMap(1, 40, 41), 2
DQBsetBMap 1, 40, 32, 13
Check "getbmap", DQBgetBMap(1, 40, 32), 13
Check "getbmap default", DQBgetBMap(1, 41, 32), 41
DQBclearLayer 1
DQBboxf 1, 0, 0, 10, 10, 32
DQBfilterBox 1, 0, 0, 5, 5, 40, 1
Check "filterbox", DQBpoint(1, 2, 2), 13
Check "filterbox out", DQBpoint(1, 8, 8), 32
Check "save bmap", DQBsaveBMap(1, "T_CORE.BMP"), 0
DQBremoveBMap 1
Check "load inactive", DQBloadBMap(1, "T_CORE.BMP"), 1
x = DQBcreateBMap(1, 40, 41)
Check "load bmap", DQBloadBMap(1, "T_CORE.BMP"), 0
Check "loaded value", DQBgetBMap(1, 40, 32), 13
KILL "T_CORE.BMP"
' --- triangles
DQBclearLayer 1
DQBtri 1, 10, 10, 50, 10, 10, 50, 7
Check "tri", DQBpoint(1, 15, 15), 7
Check "tri out", DQBpoint(1, 45, 45), 0
DQBgtri 1, 100, 10, 0, 140, 10, 40, 100, 50, 0
Check "gtri", DQBpoint(1, 120, 12) > 10, -1
DIM tex(2049)
FOR i = 0 TO 63: DQBline 1, i, 100, i, 163, i + 1: NEXT
DQBget 1, 0, 100, 63, 163, VARSEG(tex(0)), VARPTR(tex(0))
DQBclearLayer 1
DQBttri 1, 0, 0, 63, 0, 0, 63, 0, 0, 63, 0, 0, 63, VARSEG(tex(0)), VARPTR(tex(0))
Check "ttri", DQBpoint(1, 20, 5), 21
DQBfttri 1, 100, 0, 163, 0, 100, 63, 0, 0, 63, 0, 0, 63, VARSEG(tex(0)), VARPTR(tex(0))
Check "fttri", DQBpoint(1, 120, 5), 21
' --- error messages
Check "error msg", DQBloadFont("NOFILE.FNT"), 1
PRINT #9, DQBerror$
Check "font", DQBloadFont("..\..\..\..\..\tmp\none"), 1
PRINT #9, "tests:"; tests; " failed:"; fails
CLOSE #9
DQBclose
END

SUB Check (name$, got&, want&)
  tests = tests + 1
  IF got& <> want& THEN
    fails = fails + 1
    PRINT #9, "FAIL "; name$; ": got"; got&; " want"; want&
  END IF
END SUB
