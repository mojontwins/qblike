' NAVES.BAS - Pequeño juego de ejemplo para QBLike
' Usa: MULTIKEY, espera de VSYNC con INP(&H3DA), PUT ... MASK,
'      paleta con OUT &H3C8/&H3C9 y SOUND.
' Controles: flechas izquierda/derecha, espacio = disparar, Esc = salir

DEFINT A-Z
DECLARE SUB CargaSprite (spr() AS INTEGER, etiqueta$)
DECLARE SUB EsperaVSync ()

CONST NESTRELLAS = 60, NENEMIGOS = 6, NBALAS = 8

SCREEN 13
RANDOMIZE TIMER

' --- paleta: colores 32..39 para los sprites -------------------------
OUT &H3C8, 32
FOR i = 0 TO 7
    OUT &H3C9, i * 8: OUT &H3C9, 20 + i * 5: OUT &H3C9, 63 - i * 4
NEXT

DIM SHARED nave(130), enemigo(130), bala(20)
CargaSprite nave(), "nave"
CargaSprite enemigo(), "enemigo"
LINE (0, 0)-(1, 5), 15, BF
GET (0, 0)-(1, 5), bala

DIM sx(NESTRELLAS), sy(NESTRELLAS), sv(NESTRELLAS)
FOR i = 1 TO NESTRELLAS
    sx(i) = RND * 319: sy(i) = RND * 199: sv(i) = 1 + RND * 3
NEXT
DIM ex(NENEMIGOS), ey(NENEMIGOS), ev(NENEMIGOS)
FOR i = 1 TO NENEMIGOS
    ex(i) = RND * 300: ey(i) = -RND * 200: ev(i) = 1 + RND * 2
NEXT
DIM bx(NBALAS), by(NBALAS), bv(NBALAS)

px = 152: py = 178: puntos = 0: vidas = 3: espera = 0

' --- pantalla de título ---------------------------------------------
CLS
COLOR 14: LOCATE 8, 15: PRINT "N A V E S"
COLOR 7: LOCATE 12, 6: PRINT "Flechas: mover   Espacio: disparar"
LOCATE 14, 13: PRINT "Pulsa una tecla"
DO: LOOP WHILE INKEY$ = ""

DO
    ' --- entrada (varias teclas a la vez) ---
    IF MULTIKEY(75) THEN px = px - 3
    IF MULTIKEY(77) THEN px = px + 3
    IF px < 0 THEN px = 0
    IF px > 304 THEN px = 304
    IF espera > 0 THEN espera = espera - 1
    IF MULTIKEY(57) AND espera = 0 THEN
        FOR b = 1 TO NBALAS
            IF bv(b) = 0 THEN bx(b) = px + 7: by(b) = py - 4: bv(b) = 1: espera = 8: EXIT FOR
        NEXT
    END IF
    IF MULTIKEY(1) THEN EXIT DO

    ' --- lógica ---
    FOR i = 1 TO NESTRELLAS
        sy(i) = sy(i) + sv(i)
        IF sy(i) > 199 THEN sy(i) = 0: sx(i) = RND * 319
    NEXT
    FOR b = 1 TO NBALAS
        IF bv(b) THEN by(b) = by(b) - 6: IF by(b) < 0 THEN bv(b) = 0
    NEXT
    FOR i = 1 TO NENEMIGOS
        ey(i) = ey(i) + ev(i)
        IF ey(i) > 199 THEN ey(i) = -16: ex(i) = RND * 300
        FOR b = 1 TO NBALAS
            IF bv(b) THEN
                IF bx(b) >= ex(i) AND bx(b) <= ex(i) + 15 AND by(b) >= ey(i) AND by(b) <= ey(i) + 15 THEN
                    bv(b) = 0: ey(i) = -16: ex(i) = RND * 300: puntos = puntos + 10
                    SOUND 600, .5
                END IF
            END IF
        NEXT
        IF ABS(ex(i) - px) < 14 AND ABS(ey(i) - py) < 14 THEN
            vidas = vidas - 1: ey(i) = -16: SOUND 100, 3
            IF vidas = 0 THEN EXIT DO
        END IF
    NEXT

    ' --- dibujo, sincronizado con el retrazado vertical ---
    EsperaVSync
    LINE (0, 0)-(319, 199), 0, BF
    FOR i = 1 TO NESTRELLAS
        PSET (sx(i), sy(i)), 16 + sv(i) * 4
    NEXT
    FOR b = 1 TO NBALAS
        IF bv(b) THEN PUT (bx(b), by(b)), bala, PSET
    NEXT
    FOR i = 1 TO NENEMIGOS
        IF ey(i) >= 0 AND ey(i) <= 183 THEN PUT (ex(i), ey(i)), enemigo, MASK
    NEXT
    PUT (px, py), nave, MASK
    LOCATE 1, 1: COLOR 15: PRINT "Puntos:"; puntos; TAB(30); "Vidas:"; vidas;
LOOP

LOCATE 12, 15: COLOR 12: PRINT "FIN DEL JUEGO"
LOCATE 14, 13: COLOR 7: PRINT "Puntos:"; puntos
SLEEP 3
END

nave:
DATA "0000000220000000"
DATA "0000002772000000"
DATA "0000002772000000"
DATA "0000027777200000"
DATA "0000027557200000"
DATA "0000277557720000"
DATA "0002777777772000"
DATA "0027777777777200"
DATA "0277777777777720"
DATA "2777722772277772"
DATA "2777200220027772"
DATA "2772000000002772"
DATA "0220000000000220"
DATA "0000000000000000"
DATA "0000000000000000"
DATA "0000000000000000"
enemigo:
DATA "0000444444440000"
DATA "0004444444444000"
DATA "0044ccc44ccc4400"
DATA "044ccfcc4ccfcc40"
DATA "044cccc44cccc440"
DATA "0444444444444440"
DATA "0044440000444400"
DATA "0004440000444000"
DATA "0000440000440000"
DATA "0004400000044000"
DATA "0044000000004400"
DATA "0000000000000000"
DATA "0000000000000000"
DATA "0000000000000000"
DATA "0000000000000000"
DATA "0000000000000000"

SUB CargaSprite (spr() AS INTEGER, etiqueta$)
    ' Dibuja un sprite de 16x16 leído de DATA y lo captura con GET.
    ' Los dígitos hexadecimales son colores; 0 = transparente (con PUT MASK).
    IF etiqueta$ = "nave" THEN RESTORE nave ELSE RESTORE enemigo
    LINE (0, 0)-(15, 15), 0, BF
    FOR y = 0 TO 15
        READ fila$
        FOR x = 0 TO 15
            c = VAL("&H" + MID$(fila$, x + 1, 1))
            IF c = 7 THEN c = 35
            IF c = 5 THEN c = 39
            IF c THEN PSET (x, y), c
        NEXT
    NEXT
    GET (0, 0)-(15, 15), spr
END SUB

SUB EsperaVSync
    ' Espera a que termine el retrazado actual y a que empiece el siguiente
    WHILE (INP(&H3DA) AND 8) <> 0: WEND
    WHILE (INP(&H3DA) AND 8) = 0: WEND
END SUB
