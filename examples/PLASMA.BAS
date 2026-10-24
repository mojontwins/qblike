' PLASMA.BAS - Efecto de plasma con acceso directo a la memoria VGA
' y rotación de paleta por los puertos &H3C8 / &H3C9. Pulsa una tecla para salir.
DEFINT A-Z
SCREEN 13
DIM s(255)
FOR i = 0 TO 255: s(i) = INT(SIN(i * 3.14159 / 128) * 63): NEXT
DEF SEG = &HA000
FOR y = 0 TO 199
    FOR x = 0 TO 319
        c = (s((x * 2) AND 255) + s((y * 3) AND 255) + s((x + y) AND 255)) \ 3 + 128
        POKE y * 320& + x, c AND 255
    NEXT
NEXT
DEF SEG
t = 0
DO
    WAIT &H3DA, 8
    OUT &H3C8, 0
    FOR i = 0 TO 255
        OUT &H3C9, ABS(s((i + t) AND 255))
        OUT &H3C9, ABS(s((i * 2 + t) AND 255)) \ 2
        OUT &H3C9, 63 - ABS(s((i + t * 2) AND 255))
    NEXT
    t = (t + 2) AND 255
LOOP WHILE INKEY$ = ""
