' CGA compuesto: colores por artefacto en SCREEN 2
SCREEN 2
FOR n = 0 TO 15
  FOR x = n * 40 TO n * 40 + 39
    IF (n AND 2 ^ (3 - (x MOD 4))) THEN LINE (x, 0)-(x, 150), 1
  NEXT
NEXT
LOCATE 22, 1: PRINT "Pulsa C para color / blanco y negro, Esc para salir"
burst = 1: OUT &H3D8, 26
DO
  k$ = UCASE$(INKEY$)
  IF k$ = "C" THEN burst = 1 - burst: IF burst THEN OUT &H3D8, 26 ELSE OUT &H3D8, 30
LOOP UNTIL k$ = CHR$(27)
