# QBLike

Un entorno de programación compatible con **QBasic 1.1** para Windows y Linux nativos,
construido sobre [dos-like](https://github.com/mattiasgustavsson/dos-like) de Mattias Gustavsson.

![QBLike](img/welcome.jpg)

Es una reimplementación desde cero (*clean-room*): no contiene código de Microsoft. Se ha escrito a partir
de la documentación del lenguaje y del comportamiento observable de QBasic.

- **Mismo lenguaje**: tipos `%` `&` `!` `#` `$`, `TYPE`, `SUB`/`FUNCTION` con paso por referencia,
  `DEF FN`, `GOSUB`, `ON ERROR`, `SELECT CASE`, `DATA`, archivos secuenciales, aleatorios y binarios,
  `PRINT USING`, etc.
- **Mismo comportamiento**: formato numérico de `PRINT`/`STR$` (`.3333333`, `3.333333E-04`,
  `1D+20`…), redondeo bancario de `CINT`, errores de desbordamiento de enteros, zonas de 14 columnas,
  "Redo from start", y la misma secuencia de `RND` que QBasic para la misma semilla.
- **Mismo entorno**: pantalla azul, menús, ventana *Immediate*, cada SUB en su propia ventana (F2),
  formato automático del código al cambiar de línea, comprobación de sintaxis, depurador (F8/F10/F9),
  ayuda con F1 y "Press any key to continue".
- **Editor moderno**: selección con Shift, Ctrl+C/X/V con el portapapeles del sistema, Ctrl+Z/Y,
  movimiento por palabras con Ctrl+flechas, ratón y resaltado de sintaxis.

## Uso

```
qblike                    abre el IDE
qblike programa.bas       abre un programa en el IDE
qblike /RUN programa.bas  lo abre y lo ejecuta
qblike /X programa.bas    lo ejecuta sin IDE (como un ejecutable normal)
qblike -w ...             arranca en ventana (Alt+Intro o F11 alternan pantalla completa)
```

Los archivos pueden estar en CP437 (como los de MS-DOS) o en UTF-8 (editores modernos). La codificación se
detecta al abrir y se conserva al guardar. En Linux, los nombres de archivo de los programas
(`OPEN "DATOS.TXT"...`) se buscan sin distinguir mayúsculas y minúsculas, y se aceptan rutas estilo DOS.

![QBLike](img/ide.jpg)

## Teclas

| Edición | |
|---|---|
| Shift + flechas / Inicio / Fin / RePág / AvPág | seleccionar |
| Ctrl + ← / → | palabra anterior / siguiente |
| Ctrl + Inicio / Fin | principio / final |
| Ctrl+A | seleccionar todo |
| Ctrl+C, Ctrl+X, Ctrl+V | copiar, cortar, pegar (también Ctrl+Ins, Shift+Supr, Shift+Ins) |
| Ctrl+Z, Ctrl+Y | deshacer, rehacer |
| Ctrl+Retroceso, Ctrl+Supr | borrar palabra |
| Tab / Shift+Tab | indentar / desindentar (también bloques) |
| Ctrl+S, Ctrl+O, Ctrl+N | guardar, abrir, nuevo |
| Ctrl+F, Ctrl+H, F3 | buscar, reemplazar, buscar siguiente |
| Ctrl+G | ir a línea |
| Alt (pulsar y soltar) o Alt+letra | menús |

| Ejecución y depuración | |
|---|---|
| Shift+F5 | ejecutar desde el principio |
| F5 | continuar |
| F8 / F10 | paso a paso (entrando / sin entrar en procedimientos) |
| F7 | ejecutar hasta el cursor |
| F9 | punto de interrupción |
| F4 | ver la pantalla de salida |
| F6 | cambiar a/desde la ventana Immediate |
| F2 | lista de SUBs y FUNCTIONs |
| Ctrl+Pausa (Ctrl+Bloq Despl) | detener el programa |
| F1 / Shift+F1 | ayuda sobre la palabra del cursor / uso de la ayuda |

## Gráficos y sonido

Modos `SCREEN 0, 1, 2, 7, 8, 9, 10, 11, 12, 13`, con páginas (`SCREEN 7,,1,0` + `PCOPY`), `PSET`,
`LINE` (B/BF y estilo), `CIRCLE` (arcos y aspecto), `PAINT` (con mosaicos), `DRAW`, `GET`/`PUT`,
`PALETTE`, `VIEW`, `WINDOW`, `PMAP`, `POINT`. Sonido de altavoz de PC con `BEEP`, `SOUND` y `PLAY`.

![QBLike](img/example.jpg)

## Extensiones para juegos

La referencia completa de todas las instrucciones nuevas y del hardware emulado está en
[EXTENSIONES.md](EXTENSIONES.md).

Se emulan los trucos de hardware que usaban los juegos de DOS, y hay dos añadidos:

```basic
DEF SEG = &HA000: POKE y * 320& + x, c     ' acceso directo a la VGA (SCREEN 13)
WHILE (INP(&H3DA) AND 8) <> 0: WEND        ' sincronización vertical (70 Hz)
WHILE (INP(&H3DA) AND 8) = 0: WEND         '   (o WAIT &H3DA, 8)
OUT &H3C8, 0: OUT &H3C9, r: OUT &H3C9, g: OUT &H3C9, b   ' paleta VGA
k = INP(&H60)                              ' último código de tecla
IF MULTIKEY(75) THEN ...                   ' NUEVO: tecla pulsada (varias a la vez)
MOUSE x, y, b                              ' NUEVO: posición del ratón y botones
MOUSE ON                                   ' NUEVO: muestra el cursor del ratón (MOUSE OFF lo oculta; oculto por defecto)
PUT (x, y), sprite(0), MASK                ' NUEVO: PUT saltándose el color 0
CLIP 0, 20, 319, 199                       ' NUEVO: área de recorte para PUT
```

En QBLike, `PUT` recorta la imagen en lugar de dar error cuando se sale de la pantalla (o del área de `CLIP`).

### Cargar imágenes

```basic
LOADBMP "fondo.bmp", SCREEN, 0, 0, PALETTE   ' dibuja en (0,0) y aplica su paleta
DIM nave%(130)
LOADBMP "nave.bmp", nave%(0)                 ' al array, en formato GET/PUT
PUT (x, y), nave%(0), MASK
LOADGIF "explosion.gif", anim%(0)            ' GIF animado: fotogramas consecutivos
DIM pal&(255)
LOADPAL "fondo.gif", pal&(0)                 ' solo la paleta, formato PALETTE USING
PALETTE USING pal&(0)
```

- Formatos: BMP indexado de 2, 16 o 256 colores (sin comprimir, RLE4 o RLE8, con o sin paleta) y GIF
  (incluido entrelazado y animado).
- Con `SCREEN`, la imagen debe caber en los colores del modo actual (256 colores solo en SCREEN 13,
  16 colores en modos de 16, etc.); si no, da *Illegal function call*. `PALETTE` aplica la paleta del archivo.
- En la forma de array se usa el formato de `GET`/`PUT` del modo actual. Si el array no tiene espacio,
  da *Illegal function call*, igual que `GET`.
- GIF animado: cada fotograma se compone sobre el lienzo lógico del GIF y se guarda tras el anterior.
  En SCREEN 13 con array INTEGER, cada fotograma ocupa `(4 + ancho * alto + 1) \ 2` elementos.
- `LOADPAL` necesita un array LONG (como `PALETTE USING`): un elemento por color con el valor
  `65536 * azul + 256 * verde + rojo` (componentes 0-63).

También se admiten `PEEK(&H417)` (estado de Mayús/Ctrl/Alt) y `PEEK(&H46C)` (ticks del reloj).
La carpeta `examples` incluye `NAVES.BAS`, un juego completo que usa todo esto.

## Opciones

En **Options**: colores, barras de desplazamiento, resaltado de sintaxis, pantalla de 50 líneas, efecto CRT
(curvatura y líneas de barrido de dos-like, o imagen nítida en proporción 4:3), comprobación de sintaxis y
**velocidad** (máxima o aproximada a un 386, 486 o Pentium, útil para juegos antiguos con bucles de espera).
Las opciones se guardan en `~/.qblike.ini` (Linux) o `%APPDATA%\qblike.ini` (Windows).

Si se cierra la ventana con cambios sin guardar, se crea una copia `NOMBRE.RECOVER.BAS`.

## Compilar

- **Windows**: `build_windows.bat` (Visual Studio, MinGW o el TCC incluido con dos-like).
- **Linux**: `sudo apt-get install libsdl2-dev libglew-dev` y después `./build_linux.sh`.
- **macOS**: `brew install sdl2 glew` y después `./build_macos.sh` (sin probar).

No hace falta ningún sistema de construcción: se compila con una sola orden del compilador.

## Estructura

| Archivo | Contenido |
|---|---|
| `src/lexer.c` | tokenizador |
| `src/compiler.c` | análisis y compilación a árbol de sentencias |
| `src/interp.c` | intérprete y funciones integradas |
| `src/using.c` | `PRINT USING` |
| `src/screen.c` | modos de pantalla, gráficos, teclado, emulación de puertos VGA |
| `src/sound.c` | `BEEP`, `SOUND`, `PLAY` |
| `src/files.c` | archivos |
| `src/source.c` | carga y guardado de programas (módulo + SUBs) |
| `src/ide.c` | editor, menús, diálogos, depurador |
| `src/help.c` | ayuda integrada |
| `src/clip.c` | portapapeles del sistema |
| `src/image.c` | carga de BMP y GIF (`LOADBMP`, `LOADGIF`, `LOADPAL`) |
| `src/doslike/` | dos-like (con dos pequeños cambios, abajo) |

Cambios en dos-like: `setcrtmode()` para activar o desactivar el efecto CRT en tiempo de ejecución,
Alt+Intro para pantalla completa, posición del ratón también sin CRT y fuera de la ventana, en Windows impedir que Alt/F10 activen el menú de sistema de la ventana
(que bloqueaba el teclado), y atender el cierre por `SIGTERM`/Ctrl+C en Linux (`SDL_QUIT`). Se compila con `DISABLE_SCREEN_FRAME`
(sin el marco del monitor) y `DISABLE_SYSTEM_CURSOR` (QBLike dibuja su propio cursor de ratón, como el
controlador de DOS: flecha en modos gráficos, bloque invertido en modo texto).

## Limitaciones conocidas

- La interfaz y los mensajes están en inglés, como el QBasic original.
- No implementados: `CHAIN`, `CALL ABSOLUTE`/`INTERRUPT`, `ON PLAY`/`ON COM`/`ON PEN`/`ON STRIG`,
  `ON KEY` (se acepta pero no se dispara), el comando `X` de `PLAY`/`DRAW` con `VARPTR$`, la vista
  dividida (*View > Split*) y la impresión. `LPRINT` se descarta.
- `PEEK`/`POKE` solo funcionan sobre la memoria de vídeo y las zonas de la BIOS indicadas. `VARPTR` devuelve 0.
- La rueda del ratón no está disponible (dos-like no la expone).
- La música en segundo plano (`PLAY "MB..."`) se encola de forma aproximada.

## Licencia

dos-like es de Mattias Gustavsson (MIT o dominio público, a elegir).
qblike tiene licencia MIT.
