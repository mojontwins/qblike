Cosas: 

1. Al moverse con los cursores, si le damos a la izquieda debe detectar si la columna es múltiplo del número de espacios configurados para TAB, y moverse este número de espacios.

2. El número de espacios para TAB debe ser 4 por defecto.

3. En pantalla completa, no debería permitirse que el ratón salga del área de la pantalla virtual, ya que es fácil "perderlo" y complicado pulsar en los bordes de la pantalla, al haber un borde alrededor. En ventana funciona correctamente.

4. A veces el cursor no es visible en los cuadros de diálogo del editor y no sabemos qué control estamos editando.

5. Añadir un beautifier que indente correctamente el código y deshaga los ":" de forma inteligente, por ejemplo:

```
	b%=1:INPUT a%
	IF a%=7 THEN PRINT "HOLA": a%=9: b%=0
```

Debe cambiarse a

```
	b% = 1
	INPUT a%
	IF a% = 7 THEN
		PRINT "HOLA"
		a% = 9
		b% = 0
	END IF
```

6. Pressing left ALT in the editor should highlight the menu so shortcuts are visible.

7. qblike closed silently executing one of my old programs in the middle of this:

```
SUB restorecuadro
	restorecuadro:
	PUT (ix1, iy1), matrix(1), PSET
END SUB
```

Finding the label of the same name as the sub crashed qblike on execution. I know this is ugly code, but this was valid in the original QBasic.

