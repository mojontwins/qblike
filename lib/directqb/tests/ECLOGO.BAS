DEFINT A-Z
'$INCLUDE: '..\DIRECTQB.BI'
DECLARE SUB Logo ()

IF DQBinit(2, 0, 0) THEN DQBclose: PRINT DQBerror$: END

DQBinitVGA
Logo
DQBclose
END

SUB Logo
' Animates the Enhanced Creations logo
DIM Pal AS STRING * 768, LogoPic(32001) AS INTEGER
DIM t AS SINGLE, White AS INTEGER, a

IF DQBloadImage(2, 0, 0, "ECLOGO.PCX", Pal, a, a) THEN PRINT "Error loading ECLOGO.PCX, program aborted": DQBclose: END
DQBget 2, 0, 0, 319, 199, VARSEG(LogoPic(0)), VARPTR(LogoPic(0))
DQBsetPal Pal
White = DQBfindCol(63, 63, 63)
WHILE INKEY$ <> "": WEND
FOR i = 0 TO 50
  DQBboxf 1, 0, 0, 319, 199, White
  DQBrPut 1, 0, 0, VARSEG(LogoPic(0)), VARPTR(LogoPic(0)), (150 - (i * 3)), (500 - (i * 8))
  DQBwait 1
  DQBcopyLayer 1, VIDEO
  IF INKEY$ <> "" THEN
    DQBfadeTo 0, 0, 0
    EXIT SUB
  END IF
NEXT i
DQBcopyLayer 2, VIDEO
t = TIMER
DO
  IF INKEY$ <> "" THEN
    DQBfadeTo 0, 0, 0
    EXIT SUB
  END IF
LOOP WHILE TIMER < t + 3
FOR i = 0 TO 99
  DQBboxf 1, 0, 0, 319, 199, White
  DQBrPut 1, EXP(i / 18), 0, VARSEG(LogoPic(0)), VARPTR(LogoPic(0)), (i * 3), (100 - i)
  DQBwait 1
  DQBcopyLayer 1, VIDEO
  IF INKEY$ <> "" THEN
    DQBfadeTo 0, 0, 0
    EXIT SUB
  END IF
NEXT i
DQBfadeTo 0, 0, 0

END SUB

