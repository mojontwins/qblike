# DirectQB para QBLike

[English](README.md)

`DIRECTQB.BI` es una capa de compatibilidad con **DirectQB 1.61**, la librería en ensamblador para juegos
en QuickBASIC 4.5 de Angelo Mottola (Enhanced Creations, 1998-99). Contiene todos los SUB y FUNCTION de
DirectQB, con los mismos nombres, parámetros y resultados, escritos en BASIC de QBLike puro. Un juego
hecho con DirectQB funciona en QBLike sin cambios: no hay `.LIB` ni `.QLB` que cargar, el archivo de
inclusión es la librería entera.

## Uso

1. Copia `DIRECTQB.BI` junto al juego, en lugar del original.
2. Abre el `.BAS` principal en QBLike y ejecútalo. El programa ya tiene la línea

   ```basic
   '$INCLUDE: 'DIRECTQB.BI'
   ```

   al principio, como pedía el manual de DirectQB. Déjala antes de cualquier otro código: el archivo
   declara las constantes (`TRUE`, `VIDEO`, `B0`, `KEYESC`...), `DEFINT A-Z` y el estado de la librería.
3. *Archivo > Crear archivo EXE* también funciona: la librería va dentro del ejecutable.

```basic
DEFINT A-Z
'$INCLUDE: 'DIRECTQB.BI'
IF DQBinit(1, 0, 0) THEN DQBclose: PRINT DQBerror$: END
DQBinitVGA
DQBinstallKeyboard
DIM spr(DQBsize(0, 0, 15, 15) \ 2)
DQBboxf VIDEO, 0, 0, 15, 15, 4
DQBget VIDEO, 0, 0, 15, 15, VARSEG(spr(0)), VARPTR(spr(0))
DO
  DQBclearLayer 1
  DQBput 1, x, 100, VARSEG(spr(0)), VARPTR(spr(0))
  x = (x + 1) MOD 320
  DQBwait 1
  DQBcopyLayer 1, VIDEO
LOOP UNTIL DQBkey(KEYESC)
DQBclose
```

## Cómo funciona

| DirectQB | QBLike |
|---|---|
| Modo VGA 13h, `VIDEO` (`&HA000`) | `SCREEN 13`, página 0 |
| Capas en EMS (1 a n) | páginas 1 a n de `SCREEN 13` (QBLike tiene 64 páginas en SCREEN 13) |
| Capas base `B0`-`B9` | las páginas que siguen a las capas |
| Sprites en arrays de QB, pasados con `VARSEG`/`VARPTR` | igual: QBLike emula las direcciones de los arrays, y `GET`/`PUT ... SEG s, o` los dibujan de forma nativa |
| Caja de recorte | el rectángulo de `CLIP` (para `PUT`) y `VIEW SCREEN` (para líneas, texto...) |
| Zona de datos en EMS (`DQBpeek`/`DQBpoke`) | un array |
| Controlador de teclado | `MULTIKEY` |
| Controlador de ratón | `MOUSE` |
| Mezclador de Sound Blaster | `SOUND` / `SOUND PLAY` (12 canales, cualquier frecuencia de muestreo) |
| Temporizador (`DQBsetFrameRate`) | `TIMER` |
| `DQBwait` (retrazado vertical) | `WAIT VSYNC` |

Dibujar sprites (`DQBput`, `DQBget`, `DQBfPut`, `DQBxPut`, `DQBmPut`, `DQBsPut`, `DQBputOver`), copiar y
borrar capas, el desplazamiento, cajas, líneas, elipses, el relleno y el texto usan instrucciones nativas
de QBLike (`PUT`/`GET` con búferes de imagen, `PCOPY`, `LINE` con patrones de estilo, `FILL`...), así que
son rápidos. Los efectos píxel a píxel están escritos en BASIC: la rotación (`DQBrPut`), `DQBhPut`,
`DQBpPut`, `DQBtPut` con `BIT.AND`, la mezcla de colores (`DQBbPut`, `DQBfilterBox`, `DQBcopyBlendLayer`,
texto mezclado o con textura), los triángulos con sombreado gouraud y con textura, y la decodificación de
imágenes y FLI. Un `DQBrPut` de pantalla completa tarda unos 50 ms; los del tamaño de un sprite son
instantáneos.

## Diferencias con el original

Todo lo que un juego puede ver funciona como en DirectQB, con estas excepciones:

- **Las capas necesitan SCREEN 13.** Se pueden pedir hasta 53 capas (`DQBinit` devuelve el error 3 por
  encima). Dibujar en una capa antes de `DQBinitVGA` pasa a SCREEN 13 en ese momento (en DOS seguía viéndose
  la pantalla de texto hasta entonces); `DQBinitVGA` no borra las capas después.
- **`DQBmapLayer(capa)` devuelve `&HA000`** y hace de esa capa la página activa, así que
  `DEF SEG = DQBmapLayer(n): POKE ...` escribe en la capa n como antes. Mientras está mapeada una capa
  distinta de `VIDEO`, las instrucciones de QBasic (`PSET`, `PRINT`, `POKE &HA000`...) también van a esa
  capa; llama a `DQBmapLayer(VIDEO)` para volver.
- **`DEF SEG`.** Las llamadas de DirectQB que leen datos de sprites (y `DQBpeek`, `DQBsort`, las rutinas de
  imágenes...) dejan `DEF SEG` en su valor por defecto.
- **Sonido.** `DQBinstallSB` acepta cualquier configuración y siempre tiene éxito (si `DQBinit` reservó
  ranuras de sonido). Los sonidos son WAV de 8 bits mono, como en DirectQB. Las voces por encima de 12
  comparten los 12 canales de QBLike. `DQBsetVoiceVol` y `DQBsetVolume` se aplican desde el siguiente
  `DQBplaySound`. QBLike no puede pausar sonidos: `DQBpauseSound` los detiene y `DQBresumeSound` no hace nada.
- **Joystick.** No hay: `DQBjoyDetected` devuelve `FALSE` y el resto de funciones de joystick, 0.
- **Ratón.** El cursor es la flecha de QBLike: `DQBsetMouseShape` y `DQBsetMouseSpeed` se ignoran.
  `DQBsetMousePos` no puede mover el puntero del sistema, así que desplaza la posición que devuelven las
  funciones.
- **Teclado.** `DQBkey` funciona aunque no se llame a `DQBinstallKeyboard`, e `INKEY$` sigue funcionando
  con el controlador "instalado" (se vacía al instalarlo y al quitarlo).
- **Disco.** `DQBnumDrives` devuelve 3, `DQBsetDrive` no hace nada y `DQBdir$` no devuelve etiquetas de
  volumen. `DQBpath$` usa barras invertidas en todos los sistemas.
- **Errores.** `DQBsaveImage` y `DQBsaveBMap` siempre devuelven 0; un error de disco detiene el programa
  con un mensaje de error de QBLike. Las funciones de DirectQB que colgaban la máquina con argumentos
  erróneos (capas que no existen, sprites fuera de rango) dibujan en `VIDEO` o dan *Illegal function call*.
- **Errores corregidos.** Algunos fallos de DirectQB 1.61 se comportan como describe el manual:
  `DQBgetBMap` con cualquier color de primer plano, la longitud de los paquetes de sonido y de usuario de
  los archivos de datos, `DQBcollideOnLayer` con el sprite asomando por encima de la caja de recorte,
  `DQBpPut` en modo sólido y la grabación de mapas de mezcla.
- Los programas que además llaman directamente a los controladores de ratón o EMS (`CALL INTERRUPT`,
  `CALL ABSOLUTE`) necesitan reescribir esas partes: QBLike no ejecuta código máquina.

Las extensiones de QBLike sobre las que se apoya esta librería están en
[EXTENSIONES_es.md](../../EXTENSIONES_es.md): búferes de imagen y `GET`/`PUT ... SEG`, direcciones de
arrays con `VARSEG`/`VARPTR`, páginas en SCREEN 13, `SOUND(canal)`, `SETMEM`, `DIR$` y `CURDIR$`.

## Archivos

| Archivo | Contenido |
|---|---|
| `DIRECTQB.BI` | la librería |
| `tests/T_CORE.BAS` | prueba automática: capas, dibujo, sprites, recorte, colisiones, bits, paleta, texto, mapas de mezcla, triángulos (escribe `T_CORE.LOG`) |
| `tests/T_IO.BAS` | prueba automática: imágenes BMP/PCX/BSAVE, fuentes, archivos de datos, sonidos, animaciones FLI, funciones de disco (escribe `T_IO.LOG`) |
| `tests/T_VISUAL.BAS` | una pantalla con los estilos de texto, primitivas, triángulos y todos los tipos de sprite |
| `tests/ECLOGO.BAS` | la animación del logotipo de Enhanced Creations que venía con DirectQB (`ECLOGO.PCX`) |
| `tests/*.FNT` | las fuentes que venían con DirectQB |
| `tests/mkdata.py` | crea el archivo de datos, la FLI y el WAV de las pruebas (`T_DATA.DQB`, `T_ANIM.FLI`, `T_SOUND.WAV`) |

Ejecuta las pruebas desde la carpeta `tests`; cada registro termina con el número de comprobaciones que
han fallado.

## Créditos

DirectQB es freeware de Angelo Mottola - Enhanced Creations 1998-99. Su autor pedía que los juegos hechos
con ella le citasen en los créditos; por favor, sigue haciéndolo. Esta capa de compatibilidad se escribió
a partir del manual y el código fuente de DirectQB 1.61; las fuentes y `ECLOGO.PCX` de `tests` vienen del
paquete de DirectQB.
