' MUSICA.BAS - PLAY y SOUND
CLS
PRINT "Tocando una melodía con PLAY..."
PLAY "T140 O3 L8 E E P8 E P8 C E P8 G P4 > G P4"
PRINT "Escala con SOUND..."
FOR f = 262 TO 523 STEP 30
    SOUND f, 2
NEXT
PRINT "Fin."
