# Extensiones de QBLike

Referencia de todo lo que QBLike añade a QBasic 1.1, tanto instrucciones nuevas como emulación del
hardware de la época.

Los programas que no usan las instrucciones nuevas funcionan igual que en QBasic. QBLike añade además el modo
`SCREEN 3` (160×200, 16 colores, de PCjr/Tandy) y los colores por artefacto de la CGA compuesta en SCREEN 2. Como `MULTIKEY`, `MOUSE`, `CLIP`,
`LOADBMP`, `LOADGIF` y `LOADPAL` son palabras reservadas en QBLike, no pueden usarse como nombres de variable.

La única diferencia de comportamiento con QBasic es que `PUT` recorta la imagen en vez de dar
*Illegal function call* cuando se sale de la pantalla (ver [CLIP](#clip)).

## Índice

- [Instrucciones nuevas](#instrucciones-nuevas)
  - ['$INCLUDE](#include)
  - [MULTIKEY](#multikey)
  - [MOUSE](#mouse)
  - [PUT ... MASK](#put--mask)
  - [CLIP (y PUT con recorte)](#clip)
  - [FILL](#fill)
  - [MUSIC](#music)
  - [SOUND (efectos de sonido)](#sound-efectos-de-sonido)
  - [LOADBMP](#loadbmp)
  - [LOADGIF](#loadgif)
  - [LOADPAL](#loadpal)
  - [SCREEN 3 (PCjr / Tandy 1000)](#screen-3-pcjr--tandy-1000)
  - [SCREEN FULL / SCREEN WINDOW](#screen-full--screen-window)
- [Hardware emulado](#hardware-emulado)
  - [Memoria de vídeo (PEEK / POKE)](#memoria-de-vídeo-peek--poke), incluidos los modos planares EGA/VGA
  - [Sincronización vertical (INP &H3DA, WAIT, WAIT VSYNC)](#sincronización-vertical-inp-h3da-wait-wait-vsync)
  - [Paleta VGA (OUT / INP &H3C7-&H3C9)](#paleta-vga-out--inp-h3c7-h3c9)
  - [CGA compuesto: colores por artefacto (OUT &H3D8)](#cga-compuesto-colores-por-artefacto-out-h3d8)
  - [Teclado (INP &H60, PEEK &H417)](#teclado-inp-h60-peek-h417)
  - [AdLib / Sound Blaster FM (OUT / INP &H388)](#adlib--sound-blaster-fm-out--inp-h388)
  - [Reloj de la BIOS (PEEK &H46C)](#reloj-de-la-bios-peek-h46c)
  - [BLOAD / BSAVE](#bload--bsave)
- [Tabla de códigos de tecla](#tabla-de-códigos-de-tecla)
- [Ejemplo completo](#ejemplo-completo)

---

## Instrucciones nuevas

### '$INCLUDE

```
'$INCLUDE "archivo"
'$INCLUDE: 'archivo'
```

Pega el texto de otro archivo donde está la directiva, para compartir código entre programas
(pseudo bibliotecas). No es un sistema de módulos: las líneas del archivo simplemente se insertan,
como si se hubieran escrito ahí. Se aceptan la forma de QuickBASIC (`'$INCLUDE: 'ARCHIVO.BI'`,
también con `REM`) y la más sencilla `'$INCLUDE "ARCHIVO.BI"`. La directiva debe ser un comentario al
principio de una línea.

- Los archivos incluidos pueden contener código del módulo, `TYPE`, `CONST`, `DECLARE`, `DATA` y
  procedimientos `SUB` / `FUNCTION` (pasan a ser procedimientos del programa, aunque no aparecen en
  la lista de F2), y pueden incluir otros archivos, hasta 16 niveles.
- El nombre es relativo al archivo que contiene la directiva; las rutas estilo DOS
  (`'$INCLUDE "LIB\GFX.BI"`) funcionan en todos los sistemas.
- El código del módulo de un archivo incluido se ejecuta donde está la directiva. Una biblioteca
  cuyo código del módulo tiene etiquetas para `GOSUB` u `ON ... GOSUB` suele incluirse después de
  `END`.
- Los errores dentro del texto incluido se indican en la línea del `$INCLUDE` con el archivo y la
  línea: *Division by zero (LIB\MATH.BI, line 9) in main module, line 1*. El depurador no entra
  en el código incluido.
- *File > Make EXE File* mete el texto incluido dentro del ejecutable: los archivos `.BI` no hacen
  falta para ejecutarlo.

```basic
' MAIN.BAS
'$INCLUDE "LIB\MATH.BI"
PRINT Area!(2)

' LIB\MATH.BI
'$INCLUDE: 'CONST.BI'          ' relativo a LIB\, define PI
FUNCTION Area! (r!)
    Area! = PI * r! * r!
END FUNCTION
```

### MULTIKEY

```
MULTIKEY(códigodetecla%)
```

Devuelve `-1` (verdadero) mientras la tecla con ese **código de exploración** (*scan code*) está
pulsada, y `0` si no lo está. A diferencia de `INKEY$`, no consume pulsaciones del búfer y permite
comprobar varias teclas a la vez, que es lo que necesita un juego.

```basic
DO
    IF MULTIKEY(75) THEN x = x - 2          ' flecha izquierda
    IF MULTIKEY(77) THEN x = x + 2          ' flecha derecha
    IF MULTIKEY(57) THEN GOSUB Disparar     ' espacio (a la vez que te mueves)
LOOP UNTIL MULTIKEY(1)                      ' Esc
```

Los códigos están en la [tabla de códigos de tecla](#tabla-de-códigos-de-tecla).

---

### MOUSE

```
MOUSE x%, y%, botones%
MOUSE ON | OFF
```

Lee el ratón y guarda el resultado en tres variables numéricas.

- **`x%`, `y%`**: posición del puntero sobre la pantalla visible, **con la resolución del modo actual**:

  | Modo | x | y |
  |---|---|---|
  | SCREEN 13, 1, 7 | 0 – 319 | 0 – 199 |
  | SCREEN 3 | 0 – 159 | 0 – 199 |
  | SCREEN 2, 8 | 0 – 639 | 0 – 199 |
  | SCREEN 9, 10 | 0 – 639 | 0 – 349 |
  | SCREEN 11, 12 | 0 – 639 | 0 – 479 |
  | SCREEN 0 | columna (desde 0) | fila (desde 0) |

  Valen **`-1, -1`** si el puntero está fuera de la imagen (por ejemplo, sobre las bandas negras en pantalla
  completa) o fuera de la ventana. Las coordenadas son absolutas: no se les aplica `VIEW` ni `WINDOW`.
- **`botones%`**: bit 0 = botón izquierdo y bit 1 = botón derecho. Vale `1` con el izquierdo pulsado,
  `2` con el derecho, `3` con los dos y `0` sin ninguno.

La conversión tiene en cuenta el escalado de la ventana y funciona igual con el efecto CRT activado
o desactivado.

```
MOUSE ON
MOUSE OFF
```

Muestran u ocultan el **cursor del ratón**. Como el controlador de ratón de DOS, QBLike dibuja su propio
cursor sobre la pantalla emulada (el puntero del sistema se oculta sobre la ventana): una flecha blanca con
borde negro en los modos gráficos y un bloque de colores invertidos en SCREEN 0. Se mueve a la resolución
del modo, se ve igual en ventana y en pantalla completa, y sigue el efecto CRT.

- Está **oculto por defecto** al empezar cada programa: hay que usar `MOUSE ON` para verlo. Con el
  cursor oculto, `MOUSE x%, y%, b%` sigue leyendo la posición y los botones (útil si el juego dibuja
  su propio puntero).
- Se superpone solo en la imagen presentada: **no forma parte de la memoria de vídeo**, así que `POINT`,
  `GET` y `PEEK` nunca lo ven, y no hace falta ocultarlo antes de dibujar (al contrario que en DOS).
- La flecha usa los colores más parecidos al blanco y al negro de la paleta actual.
- El editor muestra siempre el bloque invertido, como QBasic.

```basic
SCREEN 13
MOUSE ON                                            ' muestra el cursor
DO
    MOUSE x%, y%, b%
    IF x% >= 0 THEN
        IF b% AND 1 THEN PSET (x%, y%), 15          ' izquierdo: dibuja
        IF b% AND 2 THEN CIRCLE (x%, y%), 5, 0      ' derecho: borra
    END IF
LOOP UNTIL INKEY$ = CHR$(27)
```

Como `MOUSE` lee el estado en el momento, para detectar un clic (pulsar y soltar) hay que comparar
con el valor anterior:

```basic
MOUSE x%, y%, b%
IF (b% AND 1) AND NOT (antes% AND 1) THEN PRINT "clic en"; x%; y%
antes% = b%
```

---

### PUT ... MASK

```
PUT [STEP](x!, y!), nombrearray[(índice%)], MASK
```

Nuevo modo de `PUT`, junto a `PSET`, `PRESET`, `AND`, `OR` y `XOR`. Copia la imagen **saltándose los
píxeles de color 0**, que quedan transparentes. Sirve para dibujar sprites sobre un fondo sin borrarlo.

```basic
SCREEN 13
DIM sprite%(130)
CIRCLE (8, 8), 7, 14: PAINT (8, 8), 14     ' dibuja algo sobre fondo 0
GET (0, 0)-(15, 15), sprite%
CLS
LINE (0, 0)-(319, 199), 1, BF              ' fondo azul
PUT (100, 80), sprite%(0), MASK            ' el círculo, sin el recuadro negro
```

Como todos los `PUT` en QBLike, se recorta al área de [CLIP](#clip) si la imagen se sale de ella.

---

### CLIP

```
CLIP x1%, y1%, x2%, y2%
CLIP
```

Define el rectángulo de recorte de `PUT`, en **coordenadas absolutas de pantalla**: a diferencia de
`VIEW`, no mueve el origen, así que `PUT (0, 0)` sigue siendo la esquina superior izquierda de la pantalla.

- Por defecto el rectángulo es la pantalla completa. Vuelve a serlo con `CLIP` sin argumentos y con cada
  `SCREEN` que indique un modo (un `SCREEN ,,1,0` que solo cambia de página no lo modifica).
- Los extremos se pueden dar en cualquier orden. Lo que quede fuera de la pantalla se ajusta a ella.
- Hace falta un modo gráfico; en `SCREEN 0` da *Illegal function call*.

**PUT con recorte.** En QBLike, `PUT` nunca da error por salirse: dibuja solo la parte de la imagen que
queda dentro del rectángulo de `CLIP` (y de la ventana de `VIEW`, si la hay). Con coordenadas negativas el
sprite sale por arriba o por la izquierda; cerca de `x2`/`y2`, por la derecha o por abajo; y si queda
totalmente fuera, no se dibuja nada. Funciona con todos los modos de `PUT` (`PSET`, `PRESET`, `AND`, `OR`,
`XOR` y `MASK`).

`CLIP` también recorta `LOADBMP`/`LOADGIF ..., SCREEN`. No afecta a `PSET`, `LINE`, `CIRCLE` y demás, que
siguen recortándose solo a la ventana de `VIEW`, como en QBasic.

```basic
SCREEN 13
LINE (0, 0)-(319, 19), 1, BF               ' barra de puntuación
CLIP 0, 20, 319, 199                       ' los sprites no la pisan
FOR x = -16 TO 320                         ' entra por la izquierda y sale por la derecha
    WAIT &H3DA, 8
    LINE (0, 20)-(319, 199), 0, BF
    PUT (x, 10), sprite%(0), MASK          ' la parte superior queda oculta tras la barra
NEXT
```

---

### FILL

```
FILL [STEP](x!, y!)[, color%]
```

Rellena con `color%` la zona de píxeles que tienen el mismo color que el punto `(x, y)` y que se tocan con él
en horizontal o en vertical, sea cual sea el color de lo que la rodea. A diferencia de `PAINT`, que se detiene
en un único color de borde, `FILL` sirve para zonas delimitadas por líneas de varios colores y para recolorear
una figura entera (por ejemplo, su contorno).

- Sin `color%` se usa el color de primer plano.
- Las coordenadas respetan `WINDOW` y `VIEW` (y `STEP`, relativo al último punto), y el relleno no sale de la
  ventana gráfica de `VIEW`.
- Si `(x, y)` ya es de `color%`, o queda fuera de la ventana gráfica, no hace nada.
- No usa recursión ni tiene límite de tamaño: rellenar la pantalla entera es inmediato.
- Hace falta un modo gráfico; en `SCREEN 0` da *Illegal function call*.

`FILL` **no es palabra reservada**: los programas que tienen su propia `SUB`, `FUNCTION`, variable o array
llamados `FILL` siguen funcionando igual.

```basic
SCREEN 13
LINE (10, 10)-(60, 10), 4: LINE (60, 10)-(60, 60), 9   ' borde de varios colores
LINE (60, 60)-(10, 60), 4: LINE (10, 60)-(10, 10), 2
FILL (30, 30), 14                                       ' el interior
FILL (20, 10), 12                                       ' solo la línea superior (color 4)
```

---

### MUSIC

```
MUSIC archivo$[, LOOP]
MUSIC STOP
MUSIC VOLUME volumen%
sonando% = MUSIC
```

Toca música de fondo mientras el programa sigue.

- `MUSIC archivo$` toca un archivo **MOD** (ProTracker, 4 canales) o **MID** (General MIDI, tocado con el
  sintetizador FM OPL emulado, el sonido de una Sound Blaster). El formato se detecta por el contenido, no
  por la extensión. Con `LOOP` la música vuelve a empezar al terminar; sin él, suena una vez.
- Un `MUSIC` nuevo sustituye a la música que esté sonando. `MUSIC STOP` la detiene.
- `MUSIC VOLUME v%` fija el volumen, 0-255 (255 al empezar el programa). Se puede cambiar mientras suena,
  para hacer fundidos.
- `MUSIC` usado como función devuelve -1 (verdadero) mientras suena música y 0 cuando ha terminado o se ha detenido.
- Si el archivo no existe: *File not found*. Si no es un MOD o MID válido: *Illegal function call*.
- La música se detiene al terminar el programa.

`MUSIC` **no es una palabra reservada**: los programas con su propia variable, array o `SUB` llamado
`MUSIC` siguen funcionando (si un programa usa una variable `MUSIC`, el nombre se refiere a la variable).

```basic
MUSIC "TITULO.MOD", LOOP
DO: LOOP WHILE INKEY$ = ""
FOR v% = 255 TO 0 STEP -5: MUSIC VOLUME v%: WAIT VSYNC: NEXT    ' fundido de salida
MUSIC "INICIO.MID"
DO: LOOP WHILE MUSIC                                            ' espera a que acabe
```

### SOUND (efectos de sonido)

```
id% = SOUND archivo$
SOUND PLAY id%[, canal%][, volumen%][, pan%][, LOOP]
SOUND STOP [canal%]
```

Efectos de sonido desde archivos WAV, mezclados con la música y con `PLAY`. El `SOUND frecuencia, duración`
de QBasic sigue funcionando como siempre.

- `id% = SOUND archivo$` carga un archivo WAV (mono o estéreo, 4000-48000 Hz, 8 o 16 bits) y devuelve su
  número. Cargar otra vez el mismo archivo devuelve el mismo número, así que se puede hacer dentro de un bucle.
- `SOUND PLAY` lo toca en uno de los **12 canales** (1-12). Sin `canal%` usa uno libre (o, si están todos
  ocupados, el que lleva más tiempo sonando). Tocar en un canal ocupado sustituye lo que sonaba en él.
- `volumen%` 0-255 (por defecto 255); `pan%` de -100 (izquierda) a 100 (derecha), 0 centrado (por defecto).
  Los argumentos vacíos toman su valor por defecto: `SOUND PLAY boom%, , , -80`.
- Con `LOOP` el sonido se repite hasta `SOUND STOP` (un motor, la lluvia...).
- `SOUND STOP canal%` detiene un canal; `SOUND STOP` sin número detiene los 12.
- Archivo erróneo: *File not found* / *Illegal function call*; canal, volumen o pan fuera de rango o un
  número que no haya devuelto `SOUND archivo$`: *Illegal function call*.

```basic
laser% = SOUND "LASER.WAV"
motor% = SOUND "MOTOR.WAV"
SOUND PLAY motor%, 1, 120, , LOOP          ' canal 1: el motor, todo el rato
DO
    IF MULTIKEY(57) THEN SOUND PLAY laser%, , , (x% - 160) * 100 \ 160   ' desde donde está la nave
    ...
LOOP UNTIL MULTIKEY(1)
SOUND STOP
```

### LOADBMP

```
LOADBMP archivo$, nombrearray[(índice%)]
LOADBMP archivo$, SCREEN, x!, y! [, PALETTE]
```

Carga una imagen **BMP indexada** de 2, 16 o 256 colores (1, 4 u 8 bits por píxel). Acepta BMP sin
comprimir, RLE4 y RLE8, con o sin paleta (también el formato antiguo de OS/2).

Hace falta estar en un modo gráfico; en `SCREEN 0` da *Illegal function call*.

**Forma de array.** Guarda la imagen en el array a partir de `índice%`, con la misma cabecera y el mismo
formato de píxeles que `GET` en el modo de pantalla actual, de modo que después basta con
`PUT (x, y), array(índice)`.

- El array debe ser numérico (INTEGER, LONG, SINGLE o DOUBLE).
- Si no hay espacio suficiente, da *Illegal function call*, igual que `GET`.
- Si la imagen tiene más colores que el modo actual, los índices se reducen a los bits del modo.

**Forma SCREEN.** Dibuja la imagen en la pantalla con la esquina superior izquierda en `(x, y)`.

- La imagen debe poder representarse en el modo actual. Si no, da *Illegal function call*:

  | Imagen | SCREEN 13 | SCREEN 3, 7, 8, 9, 12 | SCREEN 1 | SCREEN 2, 11 |
  |---|---|---|---|---|
  | 2 colores | sí | sí | sí | sí |
  | 16 colores | sí | sí | no | no |
  | 256 colores | sí | no | no | no |

- Las coordenadas respetan `WINDOW` y `VIEW` como `PUT`, y la parte que queda fuera de la ventana
  gráfica o del área de [CLIP](#clip) se recorta.
- Con `PALETTE` se aplica también la paleta del archivo (los primeros 2, 16 o 256 colores, según el modo).

**Tamaño necesario en el array**, en bytes:

| Modo | Bytes |
|---|---|
| SCREEN 13 (256 colores) | `4 + ancho * alto` |
| SCREEN 3, 7, 8, 9, 12 (16 colores) | `4 + alto * 4 * ((ancho + 7) \ 8)` |
| SCREEN 1 (4 colores) | `4 + alto * ((ancho + 3) \ 4)` |
| SCREEN 2, 11 (2 colores) | `4 + alto * ((ancho + 7) \ 8)` |

Para pasarlo a elementos, divide entre el tamaño del elemento (INTEGER 2, LONG y SINGLE 4, DOUBLE 8)
redondeando hacia arriba.

```basic
SCREEN 13
LOADBMP "fondo.bmp", SCREEN, 0, 0, PALETTE
DIM nave%((4 + 16 * 16 + 1) \ 2)
LOADBMP "nave.bmp", nave%(0)
PUT (150, 170), nave%(0), MASK
```

---

### LOADGIF

```
LOADGIF archivo$, nombrearray[(índice%)]
LOADGIF archivo$, SCREEN, x!, y! [, PALETTE]
```

Igual que [LOADBMP](#loadbmp), pero con archivos **GIF** (también entrelazados). Para comprobar si la
imagen cabe en el modo, el número de colores es el tamaño de la paleta del GIF (2, 4, 8… 256).

**GIF animados.** En la forma de array, cada fotograma se compone sobre el lienzo lógico del GIF
(respetando la transparencia y el modo de borrado de cada fotograma) y se guarda **justo detrás del
anterior**. Todos los fotogramas tienen el mismo tamaño, el del lienzo, así que cada uno ocupa:

```
elementos_por_fotograma = (bytes_de_la_imagen + tamaño_elemento - 1) \ tamaño_elemento
```

En SCREEN 13 con un array INTEGER: `(4 + ancho * alto + 1) \ 2`.

La forma `SCREEN` dibuja solo el primer fotograma.

```basic
SCREEN 13
CONST ANCHO = 32, ALTO = 32, FOTOGRAMAS = 8
tam = (4 + ANCHO * ALTO + 1) \ 2
DIM explosion%(tam * FOTOGRAMAS)
LOADGIF "explosion.gif", explosion%(0)
FOR f = 0 TO FOTOGRAMAS - 1
    WAIT &H3DA, 8
    PUT (144, 84), explosion%(f * tam), PSET
NEXT
```

Los píxeles conservan los índices de color del archivo. Si cada fotograma del GIF tiene su propia
paleta local, los índices pueden significar colores distintos en cada uno.

---

### LOADPAL

```
LOADPAL archivo$, nombrearray&[(índice%)]
```

Carga **solo la paleta** de un BMP o GIF (el formato se detecta por el contenido) en un array, a partir
de `índice%`. Guarda un elemento por color con el mismo formato que usa `PALETTE USING` en SCREEN 12 y 13:

```
valor = 65536 * azul + 256 * verde + rojo        ' componentes de 0 a 63
```

- El array debe ser **LONG** (también vale SINGLE o DOUBLE), igual que para `PALETTE USING`; con un array
  INTEGER da *Type mismatch*, porque los valores no caben.
- Si no hay espacio para todos los colores, da *Illegal function call*.
- Si el archivo no tiene paleta, da *Illegal function call*.

```basic
SCREEN 13
DIM pal&(255)
LOADPAL "atardecer.gif", pal&(0)
PALETTE USING pal&(0)

' fundido a negro
FOR paso = 63 TO 0 STEP -1
    WAIT &H3DA, 8
    OUT &H3C8, 0
    FOR i = 0 TO 255
        r = pal&(i) AND 63: g = (pal&(i) \ 256) AND 63: b = (pal&(i) \ 65536) AND 63
        OUT &H3C9, r * paso \ 63: OUT &H3C9, g * paso \ 63: OUT &H3C9, b * paso \ 63
    NEXT
NEXT
```

---

### SCREEN 3 (PCjr / Tandy 1000)

```
SCREEN 3
```

Modo de 160×200 con 16 colores de las IBM PCjr y Tandy 1000 (no existía en QBasic, sí en algunas
versiones de GW-BASIC). Los píxeles son el doble de anchos que de altos, y el texto es de 20×25.

- Colores: los 16 de la paleta IRGB de IBM, como en SCREEN 7. `COLOR primerplano, fondo` y
  `PALETTE atributo, color` (color 0-15) funcionan igual que en SCREEN 7.
- 2 páginas (`SCREEN 3, , 1, 0`, `PCOPY`).
- `GET`/`PUT` usan el mismo formato de array que el resto de modos de 16 colores; `LOADBMP`/`LOADGIF`
  aceptan imágenes de 2 o 16 colores.
- `MOUSE` da `x` de 0 a 159.
- Memoria de vídeo en `&HB800`, como en el hardware original: cada byte son dos píxeles de 4 bits
  (el nibble alto es el de la izquierda) con su color directamente, sin artefactos. Las líneas pares
  empiezan en el desplazamiento 0 y las impares en `&H2000` (8192), 80 bytes por línea. Una pantalla
  completa ocupa 16384 bytes, y se guarda y carga con `BSAVE`/`BLOAD`.

```basic
SCREEN 3
FOR i = 0 TO 15: LINE (i * 10, 0)-(i * 10 + 9, 120), i, BF: NEXT
DEF SEG = &HB800
POKE 8192 + 40 * 80 + 30, &H9A     ' línea 81: píxel 60 = color 9, píxel 61 = color 10
BSAVE "TANDY.PIC", 0, 16384
DEF SEG
```

### SCREEN FULL / SCREEN WINDOW

```
SCREEN FULL
SCREEN WINDOW
```

Pasan QBLike a pantalla completa o a una ventana desde el programa, igual que pulsar Alt+Intro o F11.
El modo de pantalla, su contenido y la paleta no cambian, y el programa sigue enseguida. El estado se
mantiene al terminar el programa; el usuario siempre puede cambiarlo con Alt+Intro.

`FULL` no es una palabra reservada: en un programa con una variable llamada `FULL`, `SCREEN full`
sigue fijando el modo de pantalla guardado en esa variable.

```basic
SCREEN 13
SCREEN FULL                ' el juego a pantalla completa
...
SCREEN WINDOW              ' de vuelta a una ventana antes de mostrar las puntuaciones
```

---

## Hardware emulado

Estos trucos ya existían en QBasic sobre un PC real. QBLike los emula para que los programas antiguos
y los juegos nuevos funcionen igual.

### Memoria de vídeo (PEEK / POKE)

| Segmento (`DEF SEG =`) | Modo | Contenido |
|---|---|---|
| `&HA000` | SCREEN 13 | lineal, un byte por píxel; desplazamiento = `y * 320 + x` (64000 bytes) |
| `&HA000` | SCREEN 7, 8, 9, 12 | planar EGA/VGA: 4 planos de bits (azul, verde, rojo, intensidad); cada byte son 8 píxeles seguidos de un plano; desplazamiento = `y * (ancho \ 8) + x \ 8` |
| `&HB800` | SCREEN 1, 2, 3 | CGA: líneas pares desde 0 y líneas impares desde `&H2000`, 80 bytes por línea (4 píxeles por byte en SCREEN 1, 8 en SCREEN 2, 2 en SCREEN 3) |
| `&HB800` | SCREEN 0 | pares carácter/atributo; desplazamiento = `(fila * columnas + columna) * 2`; cada página ocupa 4096 bytes en 80×25 (2048 en 40×25) |

```basic
SCREEN 13
DEF SEG = &HA000
POKE 100 * 320& + 160, 15        ' píxel blanco en (160, 100)
c = PEEK(100 * 320& + 160)
DEF SEG
```

#### Modos planares (SCREEN 7, 8, 9, 12)

Como en una tarjeta EGA/VGA, cada posición de memoria corresponde a 8 píxeles en cuatro planos, uno por
bit del color. Qué planos se escriben y cuál se lee se elige con los registros de la tarjeta:

| Puertos | Registro | Uso |
|---|---|---|
| `OUT &H3C4, 2: OUT &H3C5, m` | *map mask* del secuenciador | `POKE`/`BLOAD` escriben en los planos con bit a 1 en `m` (1 azul, 2 verde, 4 rojo, 8 intensidad); por defecto `15`, todos |
| `OUT &H3CE, 4: OUT &H3CF, p` | *read map select* | `PEEK`/`BSAVE` leen el plano `p` (0-3); por defecto 0 |

Cada plano ocupa `ancho * alto / 8` bytes: 8000 en SCREEN 7, 16000 en SCREEN 8, 28000 en SCREEN 9 y 38400
en SCREEN 12. Las páginas siguen a la primera cada 8 KB (SCREEN 7), 16 KB (SCREEN 8) o 32 KB (SCREEN 9);
el desplazamiento elige la página, no `SCREEN ,,apage`. Los registros vuelven a sus valores por defecto con cada `SCREEN`.

### Sincronización vertical (INP &H3DA, WAIT, WAIT VSYNC)

`INP(&H3DA)` devuelve el registro de estado de la VGA. El bit 3 (valor 8) está activo durante el
retrazado vertical, que se emula a 70 Hz como en una VGA real. Al empezar cada retrazado se vuelca a la
ventana lo dibujado hasta ese momento, así que sincronizar con él evita parpadeos y cortes.

```basic
WHILE (INP(&H3DA) AND 8) <> 0: WEND     ' espera a que termine el retrazado actual
WHILE (INP(&H3DA) AND 8) = 0: WEND      ' espera a que empiece el siguiente
' ...dibuja aquí...
```

`WAIT &H3DA, 8` hace lo mismo en una sola instrucción, y QBLike añade un alias más legible:

```basic
WAIT VSYNC                              ' = WAIT &H3DA, 8
```

`VSYNC` no es palabra reservada: los programas que tienen su propia `SUB VSync` siguen funcionando. Las tres
formas (`INP`, `WAIT &H3DA` y `WAIT VSYNC`) usan el mismo reloj de 70 Hz, así que se pueden mezclar. Mientras
espera, el programa no consume CPU de forma innecesaria.

### Paleta VGA (OUT / INP &H3C7-&H3C9)

| Puerto | Uso |
|---|---|
| `OUT &H3C8, i` | elige el color `i` que se va a escribir |
| `OUT &H3C9, v` | escribe rojo, verde y azul (0-63) uno tras otro; después avanza solo al color siguiente |
| `OUT &H3C7, i` | elige el color `i` que se va a leer |
| `INP(&H3C9)` | lee rojo, verde y azul uno tras otro |

```basic
OUT &H3C8, 0
OUT &H3C9, 10: OUT &H3C9, 20: OUT &H3C9, 50     ' color 0 = azul oscuro
```

Funciona en todos los modos gráficos; en los de 16 colores actúa sobre los colores 0-15.

### CGA compuesto: colores por artefacto (OUT &H3D8)

En una CGA conectada a un monitor o televisor compuesto NTSC, el modo de 640×200 en blanco y negro
podía mostrar colores: si se activaba la señal de *color burst*, el monitor interpretaba los patrones de
píxeles blancos y negros como colores. Cada grupo de 4 píxeles se veía como un único píxel ancho de uno
de 16 colores, con una resolución efectiva de 160×200. QBLike lo emula:

```basic
SCREEN 2            ' 640x200, burst desactivado (registro = &H1E)
OUT &H3D8, 26       ' &H1A: burst activado, colores por artefacto
OUT &H3D8, 30       ' &H1E: vuelve al blanco y negro
```

El registro de modo de la CGA (`&H3D8`) se emula en SCREEN 2: con el bit 2 a 0 se activan los
colores. Al cambiar de modo con `SCREEN` vuelve a su valor por defecto. Solo cambia la imagen que se
muestra: `POINT`, `GET`, `PEEK` y `BSAVE` siguen viendo píxeles de 0 y 1. Cada grupo empieza en una
columna múltiplo de 4, y el primer píxel es el bit de más peso:

| Patrón | Valor | Color | RGB |
|---|---|---|---|
| 0000 | &H0 | negro | #000000 |
| 0001 | &H1 | verde oscuro | #006E2D |
| 0010 | &H2 | azul | #2D02FF |
| 0011 | &H3 | azul claro | #008BFF |
| 0100 | &H4 | rojo oscuro | #A9002D |
| 0101 | &H5 | gris medio | #777677 |
| 0110 | &H6 | magenta | #EC09FF |
| 0111 | &H7 | violeta claro | #BB92FD |
| 1000 | &H8 | marrón | #2D5A00 |
| 1001 | &H9 | verde brillante | #00DC00 |
| 1010 | &HA | gris claro | #767777 |
| 1011 | &HB | aguamarina | #45F4B9 |
| 1100 | &HC | naranja | #EA6502 |
| 1101 | &HD | amarillo | #BCE500 |
| 1110 | &HE | rosa | #FF80BC |
| 1111 | &HF | blanco | #FFFFFF |

Para pintar en color hay que dibujar los patrones. Por ejemplo, un rectángulo del color `c` (0-15):

```basic
SCREEN 2: OUT &H3D8, 26
c = 12                                   ' naranja
FOR x = 160 TO 319
    IF c AND 2 ^ (3 - (x MOD 4)) THEN LINE (x, 50)-(x, 149), 1
NEXT
```

Con `DEF SEG = &HB800` y `POKE` es aún más directo: cada byte son dos píxeles de color (nibble alto
a la izquierda), así que `POKE desplazamiento, &HC9` pinta un píxel naranja y otro verde brillante.

Sin burst, `OUT &H3D9, color` (0-15) cambia el color de los píxeles encendidos de SCREEN 2, como en la CGA.

### Teclado (INP &H60, PEEK &H417)

- `INP(&H60)` devuelve el código de la última tecla pulsada; al soltarla, el mismo código + 128.
- Con `DEF SEG = 0`, `PEEK(&H417)` devuelve el estado de los modificadores: bit 0 Mayús derecha,
  bit 1 Mayús izquierda, bit 2 Ctrl, bit 3 Alt.

Para leer varias teclas a la vez es más cómodo [MULTIKEY](#multikey).

### AdLib / Sound Blaster FM (OUT / INP &H388)

QBLike emula el sintetizador FM Yamaha OPL de las tarjetas AdLib y Sound Blaster (mediante el emulador
OPL3 de dos-like), así que los programas que tocaban música FM escribiendo en los puertos de la tarjeta
funcionan sin cambios, por ejemplo el reproductor de S3M para QuickBASIC de Bisqwit (`fm.inc`).

| Puertos | Uso |
|---|---|
| `OUT &H388, reg` / `OUT &H389, valor` | selecciona un registro del OPL y lo escribe (también `&H220/&H221` y `&H228/&H229`) |
| `OUT &H38A, reg` / `OUT &H38B, valor` | segundo banco de registros del OPL3 (también `&H222/&H223`) |
| `INP(&H388)` | registro de estado: los dos temporizadores (registros 2, 3 y 4) funcionan, así que la rutina clásica de detección de AdLib encuentra la tarjeta |

```
SUB FMwrite (reg%, valor%)
    OUT &H388, reg%: x% = INP(&H388)     ' las lecturas INP son la espera que necesitaban las tarjetas reales
    OUT &H389, valor%: x% = INP(&H388)
END SUB

FMwrite &H20, &H01: FMwrite &H40, &H10: FMwrite &H60, &HF0: FMwrite &H80, &H77
FMwrite &HA0, &H98: FMwrite &HB0, &H31   ' nota: un tono de unos 440 Hz en el canal 0
```

Las escrituras suenan con una latencia fija de unos 80 ms, de modo que el ritmo se mantiene exacto aunque
el programa esté ocupado un momento. Las voces FM se silencian al terminar el programa. Los puertos del
MPU-401 (MIDI) `&H330/&H331` no se emulan: al leerlos se obtiene `&HFF`, así que las rutinas de detección
informan de que no hay interfaz MIDI en lugar de quedarse colgadas.

`examples/ADLIB.BAS` es un ejemplo completo (detección, un instrumento y una melodía a dos voces).

Estos reproductores suelen sacar el ritmo de `ON PLAY(n) GOSUB` y `PLAY "MB..."` con `VARPTR$`; QBLike lo
implementa todo como QBasic (una cola de música en segundo plano real, de hasta 32 notas).

### Reloj de la BIOS (PEEK &H46C)

Con `DEF SEG = 0`, `PEEK(&H46C)` a `PEEK(&H46F)` dan el contador de ticks del reloj desde medianoche
(18,2 por segundo), igual que en la BIOS de un PC.

### BLOAD / BSAVE

```
BSAVE archivo$, desplazamiento%, longitud%
BLOAD archivo$[, desplazamiento%]
BSAVE archivo$, VARPTR(array(índice)), longitud%
BLOAD archivo$, VARPTR(array(índice))
```

Usan el formato de archivo de QBasic: un byte `&HFD` y tres palabras de 16 bits (*little-endian*) con el
segmento, el desplazamiento y la longitud, seguidos de los datos. Así se pueden cargar los archivos de
juegos y programas antiguos y guardar archivos que QBasic también entiende.

En un PC moderno no hay segmentos, así que QBLike emula los dos casos útiles:

**1. Arrays** (sprites, mapas, tablas…). Si el desplazamiento se escribe como `VARPTR(array(índice))`, los
datos se guardan desde el array o se cargan en él tal cual, byte a byte y a partir de ese elemento, sin
importar `DEF SEG` ni el segmento guardado en el archivo. Sirve para cualquier array numérico
(INTEGER, LONG, SINGLE, DOUBLE, de una o varias dimensiones); los de cadenas y de tipos `TYPE` dan
*Type mismatch*.

- `BLOAD` da *Illegal function call* si los datos no caben en el array desde ese elemento (como `GET`),
  y no modifica nada.
- `BSAVE` da *Illegal function call* si se pide guardar más bytes de los que tiene el array.
- La longitud se cuenta en bytes: 2 por INTEGER, 4 por LONG o SINGLE, 8 por DOUBLE. Para un sprite de
  `GET`, es el tamaño de la imagen (por ejemplo, `4 + ancho * alto` en SCREEN 13).
- Hay que escribir `VARPTR(...)` directamente como segundo argumento; si es parte de una expresión
  (`VARPTR(a(0)) + 2`) o una variable, se trata como un desplazamiento normal.

```basic
SCREEN 13
DIM nave%(129)                                 ' 16x16 en SCREEN 13: 4 + 256 bytes = 130 INTEGER
GET (0, 0)-(15, 15), nave%
BSAVE "NAVE.SPR", VARPTR(nave%(0)), 260        ' guarda el sprite

DIM otra%(129)
DEF SEG = VARSEG(otra%(0))                     ' forma clásica; en QBLike es opcional
BLOAD "NAVE.SPR", VARPTR(otra%(0))             ' lo carga
DEF SEG
PUT (100, 100), otra%, MASK
```

Varios sprites seguidos en un mismo array se guardan o cargan igual, con el índice del primero y la suma
de sus tamaños.

**2. Memoria de vídeo.** Si no se usa `VARPTR`, el segmento es el de `DEF SEG` cuando apunta a la
memoria de vídeo (`&HA000` o `&HB800`) y se indica un desplazamiento; si no, el guardado en el archivo.
Los datos van a la pantalla según la [tabla de memoria de vídeo](#memoria-de-vídeo-peek--poke):

- **SCREEN 13**, segmento `&HA000`: se copian a la pantalla; lo que pase de 64000 bytes se ignora.
- **SCREEN 7, 8, 9, 12**, segmento `&HA000`: cada archivo es un plano de bits, y va a los planos
  elegidos con `OUT &H3C4, 2: OUT &H3C5, m`. `BSAVE` guarda el plano elegido con `OUT &H3CE, 4: OUT &H3CF, p`.
- **SCREEN 1, 2, 3**, segmento `&HB800`: memoria CGA o PCjr/Tandy (16384 bytes, líneas pares e impares por separado).
- **SCREEN 0**, segmento `&HB800`: pantalla de texto, pares carácter/atributo (4000 bytes en 80×25).

Los archivos con otro segmento, o con un segmento de vídeo que no corresponde al modo actual, se
ignoran sin error.

```basic
SCREEN 13
DEF SEG = &HA000
BSAVE "PANTALLA.BIN", 0, 64000     ' &HFA00 también vale, aunque como INTEGER sea negativo
DEF SEG
BLOAD "PANTALLA.BIN"               ' segmento y desplazamiento del archivo
```

Una pantalla EGA de 16 colores guardada en cuatro archivos, uno por plano:

```basic
SCREEN 7
DEF SEG = &HA000
OUT &H3C4, 2: OUT &H3C5, 1: BLOAD archivo$ + ".AZU", 0    ' plano azul
OUT &H3C4, 2: OUT &H3C5, 2: BLOAD archivo$ + ".VER", 0    ' verde
OUT &H3C4, 2: OUT &H3C5, 4: BLOAD archivo$ + ".ROJ", 0    ' rojo
OUT &H3C4, 2: OUT &H3C5, 8: BLOAD archivo$ + ".INT", 0    ' intensidad
OUT &H3C4, 2: OUT &H3C5, 15
DEF SEG
```

Y para guardarla:

```basic
DEF SEG = &HA000
FOR p = 0 TO 3
    OUT &H3CE, 4: OUT &H3CF, p
    BSAVE archivo$ + "." + MID$("AZUVERROJINT", p * 3 + 1, 3), 0, 8000
NEXT
DEF SEG
```

---

## Tabla de códigos de tecla

Códigos de exploración (*scan codes*) del teclado de PC, válidos para `MULTIKEY` e `INP(&H60)`. Corresponden a
la posición física de la tecla en un teclado estadounidense; en un teclado español, por ejemplo, `Ñ` es la
tecla del 39.

| Tecla | Código | Tecla | Código | Tecla | Código |
|---|---|---|---|---|---|
| Esc | 1 | 1 … 9, 0 | 2 … 11 | Retroceso | 14 |
| Tab | 15 | Q W E R T Y U I O P | 16 … 25 | Intro | 28 |
| Ctrl | 29 | A S D F G H J K L | 30 … 38 | Mayús izq. | 42 |
| Z X C V B N M | 44 … 50 | Mayús der. | 54 | Alt | 56 |
| Espacio | 57 | Bloq Mayús | 58 | F1 … F10 | 59 … 68 |
| F11 | 87 | F12 | 88 | Inicio | 71 |
| ↑ | 72 | RePág | 73 | ← | 75 |
| → | 77 | Fin | 79 | ↓ | 80 |
| AvPág | 81 | Insert | 82 | Supr | 83 |

---

## Ejemplo completo

La carpeta `examples` incluye `NAVES.BAS`, un pequeño juego que usa `MULTIKEY`, la espera de retrazado
con `INP(&H3DA)`, `PUT ... MASK`, la paleta por puertos y `SOUND`.

```basic
' Esqueleto de un juego en SCREEN 13 con las extensiones de QBLike
DEFINT A-Z
SCREEN 13
LOADBMP "fondo.bmp", SCREEN, 0, 0, PALETTE
DIM fondo(32002)
GET (0, 0)-(319, 199), fondo                 ' guarda el fondo entero
DIM nave((4 + 16 * 16 + 1) \ 2)
LOADBMP "nave.bmp", nave(0)
x = 152: y = 170
DO
    IF MULTIKEY(75) AND x > 0 THEN x = x - 2
    IF MULTIKEY(77) AND x < 304 THEN x = x + 2
    WAIT &H3DA, 8
    PUT (0, 0), fondo, PSET
    PUT (x, y), nave(0), MASK
LOOP UNTIL MULTIKEY(1)
```
