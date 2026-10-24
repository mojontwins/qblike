' FIGURAS.BAS - Gráficos en SCREEN 12 (640x480, 16 colores)
SCREEN 12
FOR i = 1 TO 200
    x = RND * 640: y = RND * 480
    SELECT CASE INT(RND * 3)
        CASE 0: CIRCLE (x, y), RND * 60, RND * 15
        CASE 1: LINE (x, y)-(x + RND * 100, y + RND * 80), RND * 15, B
        CASE 2: LINE (x, y)-(RND * 640, RND * 480), RND * 15
    END SELECT
NEXT
LINE (170, 200)-(470, 280), 0, BF
LINE (170, 200)-(470, 280), 15, B
LOCATE 15, 30: PRINT "Pulsa una tecla..."
SLEEP
