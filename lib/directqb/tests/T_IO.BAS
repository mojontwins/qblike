' DirectQB layer test: images, datafile, FLI, fonts, sound, disk
DEFINT A-Z
'$INCLUDE: '..\DIRECTQB.BI'
DECLARE SUB Check (name$, got&, want&)
DIM SHARED fails, tests
OPEN "T_IO.LOG" FOR OUTPUT AS #9
IF DQBinit(4, 3, 0) THEN PRINT #9, "init failed": END
DQBinitVGA
' --- images: PCX load, then save and reload in the three formats
DIM Pal AS STRING * 768
Check "load pcx", DQBloadImage(1, 0, 0, "ECLOGO.PCX", Pal, w, h), 0
Check "pcx size", w * 1000& + h, 320200
c1 = DQBpoint(1, 160, 100): c2 = DQBpoint(1, 37, 150)
Check "pcx pal", LEN(Pal), 768
FOR f = 0 TO 2
  n$ = "T_IMG." + MID$("BSVBMPPCX", f * 3 + 1, 3)
  Check "save " + n$, DQBsaveImage(1, 0, 0, 319, 199, n$, Pal, f), 0
  DQBclearLayer 2
  p$ = ""
  Check "load " + n$, DQBloadImage(2, 0, 0, n$, p$, w, h), 0
  Check "size " + n$, w * 1000& + h, 320200
  Check "pix1 " + n$, DQBpoint(2, 160, 100), c1
  Check "pix2 " + n$, DQBpoint(2, 37, 150), c2
  Check "pal " + n$, -(p$ = Pal), 1
  KILL n$
NEXT
' a small image, saved at an offset and loaded elsewhere
Check "save small", DQBsaveImage(1, 100, 50, 139, 69, "T_SM.BSV", Pal, BSV), 0
DQBclearLayer 2
Check "load small", DQBloadImage(2, 10, 10, "T_SM.BSV", p$, w, h), 0
Check "small size", w * 1000& + h, 40020
Check "small pix", DQBpoint(2, 10 + 5, 10 + 7), DQBpoint(1, 105, 57)
KILL "T_SM.BSV"
Check "load missing", DQBloadImage(2, 0, 0, "NOFILE.PCX", p$, w, h), 1
' --- fonts
Check "load font", DQBloadFont("SMALL.FNT"), 0
l1 = DQBlen("Hello")
DQBsetBIOSfont
Check "bios len", DQBlen("Hello"), 40
Check "small len", l1 < 40, -1
' --- datafile
Check "unpack closed", DQBunpackPal(3, p$), 1
Check "open datafile", DQBopenDataFile("T_DATA.DQB", "test"), 0
Check "open twice", DQBopenDataFile("T_DATA.DQB", "test"), 4
f$ = ""
Check "unpack font", DQBunpackFont(0, f$), 0
ff = FREEFILE: OPEN "HITECH.FNT" FOR BINARY AS #ff: g$ = SPACE$(2305): GET #ff, 1, g$: CLOSE #ff
Check "font data", -(f$ = g$), 1
DQBclearLayer 3
Check "unpack image", DQBunpackImage(1, 3, 100, 100), 0
Check "image pix", DQBpoint(3, 100 + 5, 100 + 2), 8
DIM spr(100)
Check "unpack sprite", DQBunpackSprite(1, VARSEG(spr(0)), VARPTR(spr(0))), 0
Check "sprite hdr", spr(0) * 1000& + spr(1), 128008
Check "unpack pal", DQBunpackPal(3, p$), 0
Check "pal byte", ASC(MID$(p$, 4, 1)), 2
x = DQBcreateBMap(2, 10, 11)
Check "unpack bmap", DQBunpackBMap(4, 2), 0
Check "bmap value", DQBgetBMap(2, 11, 3), ((256 + 3) * 7) MOD 256
Check "unpack cursor", DQBunpackCursor(5, c$), 0
Check "cursor", ASC(MID$(c$, 64, 1)), 63
DIM u(10)
Check "unpack user", DQBunpackUser(6, VARSEG(u(0)), VARPTR(u(0))), 0
Check "user data", u(0), ASC("H") + 256 * ASC("E")
Check "bad id", DQBunpackPal(9, p$), 2
Check "bad type", DQBunpackPal(0, p$), 3
DQBcloseDataFile
Check "bad password", DQBopenDataFile("T_DATA.DQB", "WRONG"), 0
Check "bad password data", DQBunpackPal(3, p$), 3
DQBcloseDataFile
' --- sound
Check "install no", DQBinstallSB(TRUE, 4, 22050, AUTO, AUTO, AUTO), 0
Check "load wav", DQBloadSound(1, "T_SOUND.WAV"), 0
Check "bad slot", DQBloadSound(9, "T_SOUND.WAV"), 6
Check "raw sound", DQBloadRawSound(2, "T_SOUND.WAV", 44, 1000), 0
Check "raw past eof", DQBloadRawSound(2, "T_SOUND.WAV", 4000, 2000), 3
x = DQBopenDataFile("T_DATA.DQB", "TEST")
Check "unpack sound", DQBunpackSound(2, 3), 0
DQBcloseDataFile
DQBplaySound 1, 1, 11025, ONCE
Check "inuse", DQBinUse(1), -1
DQBstopVoice 1
Check "stopped", DQBinUse(1), 0
DQBplaySound 3, 2, 22050, LOOPED
Check "looped", DQBinUse(2), -1
DQBremoveSB
Check "removed", DQBinUse(2), 0
' --- FLI
Check "open fli", DQBopenFLI("T_ANIM.FLI", fr, sp), 0
Check "fli info", fr * 100 + sp, 305
Check "open fli twice", DQBopenFLI("T_ANIM.FLI", fr, sp), 5
DQBplayFLIstep 4
Check "fli frame0", DQBpoint(4, 25, 50), 2
DQBgetCol 63, r, g, b
Check "fli pal", r, 63
DQBplayFLIstep 4
Check "fli frame1", DQBpoint(4, 15, 100), 200
DQBplayFLIstep 4
Check "fli frame2", DQBpoint(4, 15, 110), 201
DQBplayFLIstep 4
Check "fli ring", DQBpoint(4, 15, 100), 1
DQBplayFLIstep 4
Check "fli loop", DQBpoint(4, 15, 100), 200
DQBcloseFLI
Check "play fli", DQBplayFLI("T_ANIM.FLI", 4, NONE, ONCE), 0
' --- disk
Check "dir", -(DQBdir$("T_DATA.DQB", ATTRIB.A) = "T_DATA.DQB"), 1
n = 0: f$ = DQBdir$("*.FNT", ATTRIB.A)
DO WHILE f$ <> "": n = n + 1: f$ = DQBdir$("", 0): LOOP
Check "dir count", n, 3
Check "drive", LEN(DQBdrive$), 1
Check "chdir bad", DQBchDir("NODIR"), -1
Check "chdir ..", DQBchDir(".."), 0
Check "chdir back", DQBchDir("tests"), 0
PRINT #9, "path: "; DQBpath$
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
