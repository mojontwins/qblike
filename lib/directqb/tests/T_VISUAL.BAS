' DirectQB layer visual test: press a key to end
DEFINT A-Z
'$INCLUDE: '..\DIRECTQB.BI'
IF DQBinit(2, 0, 0) THEN DQBclose: PRINT DQBerror$: END
DQBinitVGA
FOR i = 0 TO 63: DQBsetCol 64 + i, i, 0, 0: DQBsetCol 128 + i, 0, i, 63 - i: NEXT
DQBclearLayer 1
DQBboxf 1, 0, 0, 319, 199, 1
' text styles
DQBprint 1, "Normal", 4, 4, 15
DQBprints 1, "Bold", 64, 4, 15, BOLD
DQBprints 1, "Italic", 110, 4, 15, ITALIC
DQBprints 1, "Underlined", 170, 4, 15, UNDERLINED
DQBsetTextBackCol 4
DQBprints 1, "Solid", 260, 4, 15, SOLID
DIM tex(40)
FOR y = 0 TO 7: FOR x = 0 TO 7: DQBpset 2, x, y, 64 + y * 8: NEXT: NEXT
DQBget 2, 0, 0, 7, 7, VARSEG(tex(0)), VARPTR(tex(0))
DQBsetFontTexture VARSEG(tex(0)), VARPTR(tex(0))
x = DQBloadFont("HITECH.FNT")
DQBprints 1, "TEXTURED HITECH FONT", 4, 18, 15, TEXTURED + BOLD
x = DQBloadFont("SCRIPT.FNT")
DQBprint 1, "Script font, centered", CENTERED, 30, 14
DQBsetBIOSfont
' blender map: color 15 over anything -> lighter
x = DQBcreateBMap(1, 15, 15)
FOR c = 0 TO 255: DQBsetBMap 1, 15, c, (c AND 15) + 16: NEXT
DQBsetTextBMap 1
DQBprints 1, "BLENDED", 4, 44, 15, BLENDED
DQBfilterBox 1, 200, 40, 310, 60, 15, 1
' primitives
DQBgline 1, 4, 60, 150, 60, 64, 127
DQBellipse 1, 50, 100, 40, 25, 14
DQBpaint 1, 50, 100, 2
DQBbox 1, 100, 75, 150, 125, 13
DQBline 1, 100, 75, 150, 125, 13
' triangles
DQBtri 1, 170, 75, 230, 75, 170, 125, 12
DQBgtri 1, 240, 75, 64, 310, 80, 127, 260, 125, 100
DIM tx(2049)
FOR y = 0 TO 63: FOR x = 0 TO 63: DQBpset 2, x, y, 128 + ((x \ 8 + y \ 8) AND 1) * 40 + y \ 2: NEXT: NEXT
DQBget 2, 0, 0, 63, 63, VARSEG(tx(0)), VARPTR(tx(0))
DQBttri 1, 10, 135, 90, 140, 30, 195, 0, 0, 63, 0, 0, 63, VARSEG(tx(0)), VARPTR(tx(0))
DQBfttri 1, 100, 135, 180, 140, 120, 195, 0, 0, 63, 0, 0, 63, VARSEG(tx(0)), VARPTR(tx(0))
' sprites
DIM spr(200)
DQBclearLayer 2
DQBboxf 2, 2, 2, 13, 13, 10: DQBline 2, 2, 2, 13, 7, 4: DQBpset 2, 3, 12, 15
DQBget 2, 0, 0, 15, 15, VARSEG(spr(0)), VARPTR(spr(0))
DQBput 1, 190, 140, VARSEG(spr(0)), VARPTR(spr(0))
DQBmPut 1, 210, 140, VARSEG(spr(0)), VARPTR(spr(0)), HOR
DQBmPut 1, 230, 140, VARSEG(spr(0)), VARPTR(spr(0)), VER
DQBsPut 1, 250, 140, VARSEG(spr(0)), VARPTR(spr(0)), 32, 24
DQBrPut 1, 290, 140, VARSEG(spr(0)), VARPTR(spr(0)), 32, 100
DQBhPut 1, 190, 170, VARSEG(spr(0)), VARPTR(spr(0)), 15
DQBpPut 1, 210, 170, VARSEG(spr(0)), VARPTR(spr(0)), &HAA
DQBbPut 1, 230, 170, VARSEG(spr(0)), VARPTR(spr(0)), 1
DQBcopyLayer 1, VIDEO
DQBinstallKeyboard
DQBwaitKey KEYANY
DQBclose
