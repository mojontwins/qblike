' HOLA.BAS - Primer programa
CLS
INPUT "¿Cómo te llamas"; nombre$
FOR i = 1 TO 10
    COLOR i
    PRINT "Hola, "; nombre$; "!"
NEXT
COLOR 7
