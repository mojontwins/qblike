@echo off
rem ---------------------------------------------------------------
rem  Compila QBLike para Windows. Usa UNA de estas opciones:
rem ---------------------------------------------------------------
set SRC=src\lexer.c src\compiler.c src\interp.c src\util.c src\using.c src\screen.c src\sound.c src\files.c src\source.c src\ide.c src\help.c src\clip.c src\image.c src\main.c src\doslike\dos.c

rem  1) Visual Studio: ejecutar desde "Developer Command Prompt"
where cl >nul 2>nul
if %errorlevel%==0 (
    cl /nologo /O2 /DDISABLE_SCREEN_FRAME /Fe:qblike.exe %SRC%
    goto fin
)

rem  2) MinGW-w64 / MSYS2 (gcc en el PATH)
where gcc >nul 2>nul
if %errorlevel%==0 (
    gcc -O2 -DDISABLE_SCREEN_FRAME -o qblike.exe %SRC% -mwindows -lopengl32 -lgdi32 -lwinmm -lole32 -luser32
    goto fin
)

rem  3) Tiny C Compiler incluido con dos-like (copiar la carpeta tcc aqui)
if exist tcc\tcc.exe (
    tcc\tcc -DDISABLE_SCREEN_FRAME -o qblike.exe %SRC%
    goto fin
)

echo No se encontro ningun compilador (cl, gcc o tcc\tcc.exe).
:fin
