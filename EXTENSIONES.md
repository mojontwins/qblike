# Extensiones de QBLike

Referencia de todo lo que QBLike añade a QBasic 1.1, tanto instrucciones nuevas como emulación del
hardware de la época.

Los programas que no usan las instrucciones nuevas funcionan igual que en QBasic. Como `MULTIKEY`, `CLIP`,
`LOADBMP`, `LOADGIF` y `LOADPAL` son palabras reservadas en QBLike, no pueden usarse como nombres de variable.

La única diferencia de comportamiento con QBasic es que `PUT` recorta la imagen en vez de dar
*Illegal function call* cuando se sale de la pantalla (ver [CLIP](#clip)).

## Índice

- [Instrucciones nuevas](#instrucciones-nuevas)
  - [MULTIKEY](#multikey)
  - [PUT ... MASK](#put--mask)
  - [CLIP (y PUT con recorte)](#clip)
  - [LOADBMP](#loadbmp)
  - [LOADGIF](#loadgif)
  - [LOADPAL](#loadpal)
- [Hardware emulado](#hardware-emulado)
  - [Memoria de vídeo (PEEK / POKE)](#memoria-de-vídeo-peek--poke)
  - [Sincronización vertical (INP &H3DA, WAIT)](#sincronización-vertical-inp-h3da-wait)
  - [Paleta VGA (OUT / INP &H3C7-&H3C9)](#paleta-vga-out--inp-h3c7-h3c9)
  - [Teclado (INP &H60, PEEK &H417)](#teclado-inp-h60-peek-h417)
  - [Reloj de la BIOS (PEEK &H46C)](#reloj-de-la-bios-peek-h46c)
  - [BLOAD / BSAVE](#bload--bsave)
- [Tabla de códigos de tecla](#tabla-de-códigos-de-tecla)
- [Ejemplo completo](#ejemplo-completo)

---

## Instrucciones nuevas

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

  | Imagen | SCREEN 13 | SCREEN 7, 8, 9, 12 | SCREEN 1 | SCREEN 2, 11 |
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
| SCREEN 7, 8, 9, 12 (16 colores) | `4 + alto * 4 * ((ancho + 7) \ 8)` |
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

## Hardware emulado

Estos trucos ya existían en QBasic sobre un PC real. QBLike los emula para que los programas antiguos
y los juegos nuevos funcionen igual.

### Memoria de vídeo (PEEK / POKE)

| Segmento (`DEF SEG =`) | Modo | Contenido |
|---|---|---|
| `&HA000` | SCREEN 13 | un byte por píxel; desplazamiento = `y * 320 + x` |
| `&HB800` | SCREEN 0 | pares carácter/atributo; desplazamiento = `(fila * columnas + columna) * 2`; cada página ocupa 4096 bytes |

```basic
SCREEN 13
DEF SEG = &HA000
POKE 100 * 320& + 160, 15        ' píxel blanco en (160, 100)
c = PEEK(100 * 320& + 160)
DEF SEG
```

### Sincronización vertical (INP &H3DA, WAIT)

`INP(&H3DA)` devuelve el registro de estado de la VGA. El bit 3 (valor 8) está activo durante el
retrazado vertical, que se emula a 70 Hz como en una VGA real. Al empezar cada retrazado se vuelca a la
ventana lo dibujado hasta ese momento, así que sincronizar con él evita parpadeos y cortes.

```basic
WHILE (INP(&H3DA) AND 8) <> 0: WEND     ' espera a que termine el retrazado actual
WHILE (INP(&H3DA) AND 8) = 0: WEND      ' espera a que empiece el siguiente
' ...dibuja aquí...
```

`WAIT &H3DA, 8` hace lo mismo en una sola instrucción. Mientras espera, el programa no consume CPU de forma innecesaria.

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

### Teclado (INP &H60, PEEK &H417)

- `INP(&H60)` devuelve el código de la última tecla pulsada; al soltarla, el mismo código + 128.
- Con `DEF SEG = 0`, `PEEK(&H417)` devuelve el estado de los modificadores: bit 0 Mayús derecha,
  bit 1 Mayús izquierda, bit 2 Ctrl, bit 3 Alt.

Para leer varias teclas a la vez es más cómodo [MULTIKEY](#multikey).

### Reloj de la BIOS (PEEK &H46C)

Con `DEF SEG = 0`, `PEEK(&H46C)` a `PEEK(&H46F)` dan el contador de ticks del reloj desde medianoche
(18,2 por segundo), igual que en la BIOS de un PC.

### BLOAD / BSAVE

Funcionan sobre la memoria de vídeo emulada (`DEF SEG = &HA000` en SCREEN 13, `&HB800` en SCREEN 0)
con el formato de archivo de QBasic (cabecera de 7 bytes).

```basic
SCREEN 13
DEF SEG = &HA000
BSAVE "pantalla.bin", 0, 64000
BLOAD "pantalla.bin"
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
