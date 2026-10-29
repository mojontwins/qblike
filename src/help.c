/* help.c - built-in help text. Links are written as <<Topic>> and shown as ◄Topic►. */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct HelpEnt { const char *name; const char *syntax; const char *text; const char *see; } HelpEnt;

static const HelpEnt H[] = {
{ "$INCLUDE", "'$INCLUDE \"filespec\"\n'$INCLUDE: 'filespec'\nREM $INCLUDE: 'filespec'", "Metacommand: pastes the text of another file where the directive is,\nto share code between programs (pseudo libraries). It must be a comment\nat the start of a line. Included files can contain module-level code,\nTYPE, CONST, DECLARE, DATA and SUB / FUNCTION procedures, and can include\nother files (up to 16 levels). The file name is relative to the file that\nincludes it; DOS-style paths work on every system.\n\nThe text is pasted as is: module-level code in an included file runs\nwhere the directive is. A library whose module code contains labels or\nGOSUB targets is usually included after END.\n\nErrors inside included text are reported at the $INCLUDE line, with the\nfile and line, e.g. Division by zero (LIB\\MATH.BI, line 9). The editor\ndoesn't step into included code. File > Make EXE puts the included text\ninside the executable.\n\n  '$INCLUDE \"LIB\\GFX.BI\"\n  '$INCLUDE: 'QB.BI'", "Make EXE DECLARE" },
{ "ABS", "ABS(numeric-expression)", "Returns the absolute value of a number.\n\n  PRINT ABS(-5.5)     ' 5.5", "SGN" },
{ "ASC", "ASC(stringexpression$)", "Returns the ASCII code (0-255) of the first character of a string.\nIllegal function call if the string is empty.\n\n  PRINT ASC(\"A\")      ' 65", "CHR$" },
{ "ATN", "ATN(numeric-expression)", "Returns the arctangent of a number, in radians (-PI/2 to PI/2).\nPI can be computed as 4 * ATN(1).", "COS SIN TAN" },
{ "BEEP", "BEEP", "Sounds the speaker: an 800 Hz tone for a quarter of a second.", "SOUND PLAY" },
{ "BLOAD", "BLOAD filespec$[, offset%]\nBLOAD filespec$, VARPTR(array(index))", "Loads a file saved with BSAVE.\n\nWith VARPTR(array(index)) as offset the data is copied raw into the\nnumeric array, byte by byte, from that element (DEF SEG and the segment\nin the file are ignored). Illegal function call if it does not fit:\n\n  BLOAD \"SHIP.SPR\", VARPTR(ship%(0))\n\nOtherwise the data goes to emulated video memory: DEF SEG when it is\n&HA000 or &HB800 and an offset is given, else the segment in the file.\n  &HA000  SCREEN 13: linear (up to 64000 bytes)\n          SCREEN 7, 8, 9, 12: one bit plane, written to the planes\n          enabled with OUT &H3C4, 2: OUT &H3C5, mask\n  &HB800  SCREEN 1, 2: CGA memory; SCREEN 0: character/attribute pairs\nFiles for other segments are ignored.", "BSAVE DEF SEG VARPTR" },
{ "BSAVE", "BSAVE filespec$, offset%, length%\nBSAVE filespec$, VARPTR(array(index)), length%", "Saves length% bytes to a file in BSAVE format (&HFD, segment, offset,\nlength, data).\n\nWith VARPTR(array(index)) the bytes come from the numeric array, from\nthat element (2 bytes per INTEGER, 4 per LONG or SINGLE, 8 per DOUBLE):\n\n  GET (0, 0)-(15, 15), ship%\n  BSAVE \"SHIP.SPR\", VARPTR(ship%(0)), 260\n\nOtherwise they come from emulated video memory at DEF SEG (see BLOAD).\nIn SCREEN 7, 8, 9 and 12 one bit plane is saved, the one selected with\nOUT &H3CE, 4: OUT &H3CF, plane.", "BLOAD DEF SEG VARPTR" },
{ "CALL", "[CALL] name [([argumentlist])]", "Transfers control to a SUB procedure. Without CALL the parentheses\naround the argument list are omitted. Arguments are passed by\nreference unless enclosed in parentheses.", "SUB DECLARE" },
{ "CASE", "CASE expressionlist", "See SELECT CASE.", "SELECT" },
{ "CDBL", "CDBL(numeric-expression)", "Converts a numeric expression to DOUBLE precision.", "CINT CLNG CSNG" },
{ "CHAIN", "CHAIN filespec$", "Transfers control to another program. Not supported in QBLike: raises\n\"Advanced feature unavailable\". Combine the programs or use RUN.", "RUN COMMON" },
{ "CHDIR", "CHDIR pathspec$", "Changes the current directory.\n\n  CHDIR \"GAMES\"", "MKDIR RMDIR FILES" },
{ "CHR$", "CHR$(code%)", "Returns the character with the given ASCII code (0-255).\n\n  PRINT CHR$(65); CHR$(3)     ' A and a heart", "ASC" },
{ "CINT", "CINT(numeric-expression)", "Rounds to the nearest INTEGER; halves round to the even number\n(CINT(2.5) = 2, CINT(3.5) = 4). Range -32768 to 32767 (Overflow\notherwise).", "CLNG FIX INT" },
{ "CIRCLE", "CIRCLE [STEP] (x!,y!),radius![,[color%] [,[start!] [,[end!] [,aspect!]]]]", "Draws a circle, ellipse or arc. Start and end angles are in radians;\nnegative angles also draw a radius line.", "LINE PSET PAINT" },
{ "CLEAR", "CLEAR [,,stack&]", "Closes all files, erases all arrays and resets every module-level\nvariable to zero or \"\". The stack size argument of QBasic is accepted\nand ignored (QBLike's stack is not limited this way).", "ERASE" },
{ "CLIP", "CLIP x1%, y1%, x2%, y2%\nCLIP", "QBLike extension. Sets the clipping rectangle used by PUT (and by\nLOADBMP/LOADGIF ..., SCREEN), in absolute screen coordinates: the origin\nis not moved, unlike VIEW. CLIP alone, or SCREEN with a mode, resets it to\nthe whole screen.\n\nPUT never fails for being off-screen: only the part of the image inside\nthe rectangle is drawn, so sprites can leave the screen on any side.\n\n  CLIP 0, 20, 319, 199     ' keep the top 20 lines (score bar) intact\n  PUT (x%, y%), sprite%(0), MASK", "PUT VIEW" },
{ "COM", "COM(n%) ON | OFF | STOP\nON COM(n%) GOSUB line", "Serial port event trapping. Accepted so old programs run, but QBLike has\nno serial ports: the event never fires.", "ON TIMER" },
{ "COMMAND$", "COMMAND$", "Returns the command line used to invoke the program: everything typed\nafter the program name, without leading blanks, in upper case (like\nQuickBASIC). In programs made with File > Make EXE it holds the\narguments of the executable; with qblike /X PROG.BAS the words after\nthe file name. Run from the editor, it is \"\".\n\nCOMMAND$ comes from QuickBASIC (QBasic 1.1 doesn't have it); a program\nthat uses its own variable COMMAND$ keeps it.\n\n  IF COMMAND$ = \"/EASY\" THEN lives% = 5", "MAKE EXE" },
{ "CVSMBF", "CVSMBF(4-byte-string$)\nCVDMBF(8-byte-string$)\nMKSMBF$(single-expression!)\nMKDMBF$(double-expression#)", "Convert between numbers and strings in Microsoft Binary Format, the\nfloating point format of GW-BASIC and early QuickBASIC data files.\nCVSMBF/CVDMBF read such a string; MKSMBF$/MKDMBF$ write one.", "CVI MKI$" },
{ "ENVIRON", "ENVIRON$(name$)\nENVIRON$(n%)\nENVIRON \"name=value\"", "ENVIRON$(name$) returns the value of an environment variable (\"\" if it\ndoes not exist); ENVIRON$(n%) returns the n-th entry as \"NAME=value\".\nThe ENVIRON statement sets a variable for this program and the programs\nit starts with SHELL.", "SHELL" },
{ "ERDEV", "ERDEV\nERDEV$", "Return the code and name of the device that caused the last critical\nerror in DOS. In QBLike they always return 0 and \"\".", "ERR" },
{ "FIELD", "FIELD [#]filenumber%, width% AS stringvariable$ [, width% AS ...]", "Divides the record buffer of a file opened FOR RANDOM into string\nvariables. Assign them with LSET or RSET (never with =), then write the\nrecord with PUT; GET fills them again:\n\n  OPEN \"DATA.DAT\" FOR RANDOM AS #1 LEN = 30\n  FIELD #1, 20 AS name$, 10 AS phone$\n  LSET name$ = \"Ann\": LSET phone$ = \"555-1234\"\n  PUT #1, 1", "LSET OPEN GET" },
{ "FILEATTR", "FILEATTR(filenumber%, attribute%)", "attribute% = 1 returns the mode the file was opened in: 1 INPUT,\n2 OUTPUT, 4 RANDOM, 8 APPEND, 32 BINARY. attribute% = 2 (the DOS file\nhandle) returns 0.", "OPEN" },
{ "FILL", "FILL [STEP] (x!, y!)[, color%]", "QBLike extension. Fills with color% the area of pixels that have the same\ncolor as (x, y) and touch it horizontally or vertically, whatever colors\nsurround it (PAINT needs a single border color). Without color%, the\nforeground color is used. Coordinates follow WINDOW and VIEW, and the fill\nstays inside the VIEW viewport. If (x, y) already has color%, nothing\nhappens.\n\n  LINE (10, 10)-(60, 60), 4, B\n  FILL (30, 30), 14            ' interior of the box\n  FILL (20, 10), 2             ' recolor the box itself\n\nFILL is not a reserved word: a program with its own SUB, FUNCTION,\nvariable or array called FILL keeps using it.", "PAINT POINT" },
{ "CLNG", "CLNG(numeric-expression)", "Rounds to the nearest LONG integer (halves to even). Range\n-2147483648 to 2147483647.", "CINT" },
{ "CLOSE", "CLOSE [[#]filenumber%[,[#]filenumber%]...]", "Closes one or more open files. CLOSE alone closes all files.", "OPEN RESET" },
{ "CLS", "CLS [{0 | 1 | 2}]", "Clears the screen and moves the cursor to the top-left corner.\n  CLS      clears the active viewport (graphics VIEW or text VIEW PRINT)\n  CLS 0    clears the whole screen\n  CLS 1    clears the graphics viewport\n  CLS 2    clears the text viewport (VIEW PRINT area)\nThe screen is filled with the current background color.", "VIEW VIEW PRINT" },
{ "COLOR", "SCREEN 0:       COLOR [foreground%] [,[background%] [,border%]]\nSCREEN 7-10:    COLOR [foreground%] [,background%]\nSCREEN 12-13:   COLOR [foreground%]",
  "Sets screen display colors. In text mode foreground 0-31 (16-31 blink)\nand background 0-7.\n\n 0 Black    4 Red       8 Gray         12 Light Red\n 1 Blue     5 Magenta   9 Light Blue   13 Light Magenta\n 2 Green    6 Brown    10 Light Green  14 Yellow\n 3 Cyan     7 White    11 Light Cyan   15 Bright White", "PALETTE SCREEN" },
{ "COMMON", "COMMON [SHARED] variablelist", "Shares variables between chained programs. QBLike does not chain\nprograms, so COMMON is treated like DIM: with SHARED the variables are\nvisible in every SUB and FUNCTION.\n\nNote: the types are decided where COMMON appears. A COMMON SHARED\nbefore DEFINT A-Z declares SINGLE variables; the INTEGER ones with the\nsame names used later are different variables.", "DIM SHARED" },
{ "CONST", "CONST constantname = expression [,constantname = expression]...", "Declares symbolic constants that can be used instead of numbers or\nstrings. A constant's value cannot change. Constants declared in the\nmodule are visible in all procedures.\n\n  CONST PI = 3.141593, TITLE$ = \"Space Game\"\n  CONST TRUE = -1, FALSE = NOT TRUE\n  CONST MAXX% = 319", "DIM" },
{ "COS", "COS(angle)", "Returns the cosine of an angle given in radians (degrees * PI / 180).", "SIN TAN ATN" },
{ "CSNG", "CSNG(numeric-expression)", "Converts a numeric expression to SINGLE precision.", "CDBL" },
{ "CSRLIN", "CSRLIN", "Returns the current row of the text cursor (1-based). POS(0) returns\nthe column.", "POS LOCATE" },
{ "CURDIR$", "CURDIR$[(drive$)]", "QuickBASIC PDS function, available in QBLike. Returns the current\ndirectory as a native path of the system (C:\\GAMES on Windows,\n/home/user/games on Linux). drive$ is accepted and ignored.\nNot a reserved word (see DIR$).", "DIR$ CHDIR" },
{ "CVI", "CVI(2-byte-string)  CVL(4-byte-string)  CVS(4-byte-string)  CVD(8-byte-string)", "Convert strings created by MKI$, MKL$, MKS$ and MKD$ (2, 4, 4 and\n8 bytes) back to numbers. Used with FIELD buffers and binary files.", "MKI$" },
{ "CVL", "CVL(4-byte-string)", "See CVI.", "CVI MKL$" },
{ "CVS", "CVS(4-byte-string)", "See CVI.", "CVI MKS$" },
{ "CVD", "CVD(8-byte-string)", "See CVI.", "CVI MKD$" },
{ "DATA", "DATA constant[,constant]...", "Stores numeric and string constants that READ assigns to variables.\nDATA statements can be anywhere in the module (not inside a SUB or\nFUNCTION); they are read in order. Strings that contain commas, colons\nor leading spaces must be in quotes. Use RESTORE to read them again.\n\n  FOR i% = 1 TO 3: READ n$, age%: PRINT n$; age%: NEXT\n  DATA Ann, 31, \"Smith, John\", 45, Bob, 27", "READ RESTORE" },
{ "DATE$", "DATE$", "Returns the current date as \"mm-dd-yyyy\". (Setting the date with\nDATE$ = ... is accepted and ignored.)", "TIME$ TIMER" },
{ "DECLARE", "DECLARE {SUB | FUNCTION} name [([parameterlist])]", "Declares a procedure. QBLike adds DECLARE lines automatically when\nsaving; they are not required to run.", "SUB FUNCTION" },
{ "DEF FN", "DEF FNname[(parameterlist)] = expression\n\nDEF FNname[(parameterlist)]\n    [statementblock]\n    FNname = expression\nEND DEF", "Defines a function inside the module, in the older style. The name\nmust begin with FN. Module variables are visible inside DEF FN\n(unlike SUB/FUNCTION, where they need SHARED). FUNCTION procedures\nare preferred.\n\n  DEF FNarea (r) = 3.14159 * r ^ 2\n  PRINT FNarea(10)", "FUNCTION" },
{ "DEF SEG", "DEF SEG [=address]", "Sets the segment used by PEEK, POKE, BLOAD and BSAVE.\nEmulated segments:\n  &HA000  SCREEN 13: 1 byte per pixel, offset = y*320+x, on the active\n          page (SCREEN ,, page)\n          SCREEN 7, 8, 9, 12: EGA/VGA bit planes, offset = y*(width\\8)+x\\8;\n          OUT &H3C4, 2: OUT &H3C5, mask selects the planes written,\n          OUT &H3CE, 4: OUT &H3CF, plane selects the plane read\n  &HB800  SCREEN 0: character/attribute pairs; SCREEN 1, 2: CGA memory\n  0       BIOS: PEEK(&H417) keyboard flags, PEEK(&H46C) timer ticks,\n          PEEK(&H449) video mode (&H13 in SCREEN 13, 3 in SCREEN 0),\n          PEEK(&H44A) text columns, PEEK(&H462) visual page,\n          PEEK(&H484) text rows - 1\n  VARSEG of a numeric array: the bytes of the array (see <<VARPTR>>)", "PEEK POKE BLOAD VARPTR" },
{ "DEFINT", "DEFINT letterrange [,letterrange]...\nDEFLNG / DEFSNG / DEFDBL / DEFSTR", "Sets the default data type for variables whose names begin with the\nspecified letters. Example: DEFINT A-Z", "DIM" },
{ "DIM", "DIM [SHARED] variable[(subscripts)] [AS type] [,variable[(subscripts)] [AS type]]...\n\nsubscripts: [lower% TO] upper% [,[lower% TO] upper%]...\ntype: INTEGER, LONG, SINGLE, DOUBLE, STRING, STRING * n, or a TYPE name", "Declares a variable or array and its type.\n\n- Arrays with constant bounds are static: they are created when the\n  program starts. Arrays with variable bounds (or created with REDIM)\n  are dynamic: ERASE frees them and REDIM resizes\n  them.\n- Without DIM, an array used with subscripts is created with bounds\n  0 TO 10 (or 1 TO 10 with OPTION BASE 1).\n- SHARED makes the variable visible in every SUB and FUNCTION.\n- As in QBasic, elements are stored with the FIRST subscript varying\n  fastest: a(0,0), a(1,0), a(0,1)... This matters when GET, PUT,\n  BLOAD or BSAVE use part of a multi-dimensional array, e.g. one\n  sprite per column: GET (0, 0)-(15, 15), sprites%(0, i%)\n\nQBLike limits: up to 64 million elements per array and subscripts in\nthe whole LONG range (QBasic: 64 KB per array, -32768 to 32767).\n\n  DIM a%(100), grid(1 TO 20, 1 TO 20), title AS STRING * 20\n  DIM SHARED score&, player AS PlayerType", "REDIM ERASE TYPE SHARED OPTION BASE" },
{ "DIR$", "DIR$[(filespec$[, attributes%])]", "QuickBASIC PDS function, available in QBLike. With filespec$ (wildcards\n* and ? allowed, with a path or not) returns the first matching file\nname; without arguments, the next one; \"\" when there are no more.\nattributes% &H10 also lists directories. \".\" and \"..\" are never listed.\n\n  f$ = DIR$(\"*.BAS\")\n  DO WHILE LEN(f$): PRINT f$: f$ = DIR$: LOOP\n\nDIR$ is not a reserved word: a program with its own DIR$ variable or\narray keeps using it.", "CURDIR$ FILES KILL" },
{ "DO", "DO [{WHILE | UNTIL} condition]\n    [statementblock]\nLOOP\n\nDO\n    [statementblock]\nLOOP [{WHILE | UNTIL} condition]", "Repeats a block of statements while a condition is true or until it\nbecomes true. EXIT DO leaves the loop.", "WHILE FOR EXIT" },
{ "DRAW", "DRAW commandstring$", "Draws an object.\n\n U/D/L/R [n]  up/down/left/right    E/F/G/H [n]  diagonals\n M [+|-]x,y   move to / by          B  prefix: move without drawing\n N  prefix: return after drawing    A n  angle (0-3)   TA n  turn angle\n C n  color   S n  scale (default 4) P paint,border\n X + VARPTR$(a$)  runs a$   any n can be \"=\" + VARPTR$(var%)", "LINE PSET VARPTR$" },
{ "END", "END [{DEF | FUNCTION | IF | SELECT | SUB | TYPE}]", "END alone ends the program: files are closed and, in the editor, the\nscreen waits for a key (\"Press any key to continue\").\n\nEND IF, END SELECT, END SUB, END FUNCTION, END DEF and END TYPE close\nthe corresponding block.", "STOP SYSTEM" },
{ "EOF", "EOF(filenumber%)", "Returns true (-1) when the end of a file has been reached, false (0)\notherwise.\n\n  DO UNTIL EOF(1): LINE INPUT #1, l$: LOOP", "LOF LOC OPEN" },
{ "ERASE", "ERASE arrayname [,arrayname]...", "For static arrays, sets every element to zero or \"\". For dynamic\narrays, frees the memory: the array must be dimensioned again (DIM or\nREDIM) before it is used.\n\n  ERASE a%, names$", "DIM REDIM" },
{ "ERR", "ERR\nERL", "ERR returns the code of the last error, ERL the nearest line number\nbefore the statement that caused it.", "ON ERROR ERROR Errors" },
{ "ERROR", "ERROR expression%", "Simulates the occurrence of the error with the given code (1-255),\nwhich can be trapped with ON ERROR. Codes not used by BASIC can be\nused for the program's own errors. See <<Errors>> for the codes.\n\n  IF lives% < 0 THEN ERROR 200", "ON ERROR ERR" },
{ "EXIT", "EXIT {DEF | DO | FOR | FUNCTION | SUB}", "Leaves a block early:\n  EXIT DO        continues after LOOP\n  EXIT FOR       continues after NEXT\n  EXIT SUB       returns from the SUB\n  EXIT FUNCTION  returns from the FUNCTION (with the value assigned so far)\n  EXIT DEF       returns from the DEF FN", "DO FOR SUB FUNCTION" },
{ "EXP", "EXP(numeric-expression)", "Returns e (2.718282) raised to the given power.", "LOG" },
{ "FILES", "FILES [filespec$]", "Lists the names of the files that match filespec$ (wildcards * and ?\nallowed; default all files) in the current or given directory, like\nthe DIR command of DOS.\n\n  FILES \"*.BAS\"\n  FILES \"DATA\\*.*\"", "CHDIR KILL" },
{ "FIX", "FIX(numeric-expression)", "Truncates toward zero: FIX(-3.7) = -3. INT rounds down instead:\nINT(-3.7) = -4.", "INT CINT" },
{ "FOR", "FOR counter = start TO end [STEP increment]\n    [statementblock]\nNEXT [counter [,counter]...]", "Repeats a block a specified number of times. The counter goes from\nstart to end, adding increment (default 1) each time; with a negative\nSTEP it counts down. The loop does not run at all if start is already\npast end. EXIT FOR leaves it early.\n\n  FOR i% = 1 TO 10: PRINT i%;: NEXT i%\n  FOR x! = 1 TO 0 STEP -.1: PRINT x!: NEXT", "DO EXIT" },
{ "FRE", "FRE(stringexpression$)\nFRE(numeric-expression)", "Returns free memory. QBasic reported a few tens of KB; QBLike has no\nsuch limits (strings up to 1 GB, arrays up to 64 million elements,\nabout 100000 levels of recursion), so it returns 268435456 (256 MB),\nor 67108864 for FRE(-2) (stack).", "DIM" },
{ "FREEFILE", "FREEFILE", "Returns the next unused file number, to use with OPEN.\n\n  f% = FREEFILE: OPEN \"LOG.TXT\" FOR APPEND AS #f%", "OPEN" },
{ "FUNCTION", "FUNCTION name[(parameterlist)] [STATIC]\n    [statementblock]\n    name = expression\nEND FUNCTION", "Defines a FUNCTION procedure. Type FUNCTION name in the editor (or use\nEdit > New FUNCTION) and it gets its own window; press F2 to switch.", "SUB DECLARE EXIT" },
{ "GET", "GET [#]filenumber%[,[record&][,variable]]        (files)\nGET [surface,] [STEP](x1!,y1!)-[STEP](x2!,y2!), data   (graphics)\n\nsurface, data:  arrayname[(index%)]  |  SEG segment%, offset%", "Files: reads a record from a file opened FOR RANDOM, or bytes from a\nfile opened FOR BINARY, into variable (or the FIELD buffer). record&\nis the record number (RANDOM) or byte position (BINARY), counting from\n1; omitted, the next one is read.\n\nGraphics: stores the rectangle between the two points in a numeric\narray, to draw it later with PUT. The array needs\n4 + INT((width * bitsperpixel + 7) / 8) * planes * height bytes\n(SCREEN 13: 4 + width * height).\n\n  DIM sprite%(130)\n  GET (0, 0)-(15, 15), sprite%\n\nQBLike extensions (SCREEN 13):\n  * surface: reads from an image stored in an array (a GET image used as\n    an off-screen buffer) instead of the screen; the coordinates are\n    pixels of that image:\n      GET buffer%(0), (16, 0)-(31, 15), frame%\n  * SEG segment%, offset%: the array element at that address, as given by\n    VARSEG/VARPTR (libraries that receive addresses, like DirectQB):\n      GET (0, 0)-(15, 15), SEG VARSEG(spr%(0)), VARPTR(spr%(0))", "PUT VARPTR OPEN" },
{ "GOSUB", "GOSUB line\n...\nRETURN [line]", "GOSUB jumps to a label or line number in the same module or\nprocedure; RETURN goes back to the statement after the GOSUB. RETURN\nline returns to another label instead.\n\n  GOSUB DrawScore\n  ...\n  END\n  DrawScore:\n      LOCATE 1, 1: PRINT score%\n  RETURN", "RETURN ON GOSUB" },
{ "GOTO", "GOTO line", "Jumps to a line label or number in the same module or procedure.\n\n  start:\n      INPUT \"Again (Y/N)\"; a$\n      IF UCASE$(a$) = \"Y\" THEN GOTO start", "GOSUB" },
{ "HEX$", "HEX$(expression&)", "Returns the hexadecimal representation of a number (rounded to an\ninteger) as a string.\n\n  PRINT HEX$(255)     ' FF", "OCT$" },
{ "IF", "IF condition THEN thenpart [ELSE elsepart]\n\nIF condition1 THEN\n    [statementblock-1]\n[ELSEIF condition2 THEN\n    [statementblock-2]]...\n[ELSE\n    [statementblock-n]]\nEND IF", "Executes statements depending on conditions. A condition is true\nwhen it is not zero. The single-line form can contain several\nstatements separated by colons, and THEN line / ELSE line jump to a\nlabel. The block form ends with END IF.\n\n  IF x% > 10 THEN PRINT \"Big\" ELSE PRINT \"Small\"\n  IF a$ = \"\" THEN\n      PRINT \"Empty\"\n  ELSEIF LEN(a$) > 10 THEN\n      PRINT \"Long\"\n  ELSE\n      PRINT a$\n  END IF", "SELECT" },
{ "INKEY$", "INKEY$", "Reads a character from the keyboard without waiting. Extended keys\nreturn CHR$(0) + scan code (e.g. up arrow = CHR$(0) + \"H\").", "INPUT$ MULTIKEY INP" },
{ "INP", "INP(port%)", "Emulated ports:\n  &H60   last keyboard scan code (bit 7 set on release)\n  &H3DA  VGA status: bit 3 = vertical retrace (70 Hz)\n  &H3C9  palette data read (after OUT &H3C7, index)\n  &H388  AdLib FM status (timers work: AdLib detection succeeds)\n  &H330  MPU-401: not emulated, returns &HFF\n\nVSYNC wait:\n  WHILE (INP(&H3DA) AND 8) <> 0: WEND\n  WHILE (INP(&H3DA) AND 8) = 0: WEND", "OUT WAIT MULTIKEY" },
{ "INPUT", "INPUT [;] [\"prompt\"{; | ,}] variablelist\nINPUT #filenumber%, variablelist", "Reads values typed at the keyboard (separated by commas) into the\nvariables, after showing the prompt. With ; after the prompt a\nquestion mark is shown; with , it is not. If the values don't match the\nvariables, \"Redo from start\" is shown and the input is asked again.\nWith ; right after INPUT the cursor stays on the line after Enter.\nINPUT # reads values separated by commas or line ends from a file.\n\n  INPUT \"Name, age\"; n$, age%", "LINE INPUT INPUT$" },
{ "INPUT$", "INPUT$(n[,[#]filenumber%])", "Returns n characters read from the keyboard (without showing them\nand without waiting for Enter) or from a file. n can be 1 to 32767.\n\n  PRINT \"Press a key\": k$ = INPUT$(1)\n  header$ = INPUT$(4, #1)", "INKEY$" },
{ "INSTR", "INSTR([start%,]stringexpression1$,stringexpression2$)", "Returns the position of the first occurrence of stringexpression2$\nin stringexpression1$, searching from position start% (default 1), or\n0 if it is not found. Returns a LONG (strings can be very long in\nQBLike).\n\n  PRINT INSTR(\"HELLO WORLD\", \"O\")       ' 5\n  PRINT INSTR(6, \"HELLO WORLD\", \"O\")    ' 8", "MID$" },
{ "INT", "INT(numeric-expression)", "Returns the largest integer less than or equal to a number:\nINT(3.7) = 3, INT(-3.2) = -4.\n\n  dice% = INT(RND * 6) + 1", "FIX CINT" },
{ "IOCTL", "IOCTL [#]filenumber%, string$\nIOCTL$([#]filenumber%)", "Send and read control strings to a device driver. Accepted; IOCTL does\nnothing and IOCTL$ returns \"\".", "OPEN" },
{ "KEY", "KEY n%, string$\nKEY LIST | ON | OFF\nKEY(n%) ON | OFF | STOP\nON KEY(n%) GOSUB line", "Function key assignments and key event trapping. Accepted so old\nprograms run, but ON KEY events never fire. To read keys use INKEY$, or\nMULTIKEY for several keys at once.", "INKEY$ MULTIKEY" },
{ "KILL", "KILL filespec$", "Deletes the files that match filespec$ (wildcards * and ? allowed, as\nin DOS). File not found if none matches.\n\n  KILL \"TEMP.DAT\"\n  KILL \"SAVES\\*.TMP\"", "FILES NAME" },
{ "LBOUND", "LBOUND(array[,dimension%])\nUBOUND(array[,dimension%])", "Return the lowest and highest subscript of an array dimension\n(dimension% 1 by default).\n\n  DIM a(5 TO 20)\n  FOR i = LBOUND(a) TO UBOUND(a): a(i) = i: NEXT", "DIM" },
{ "LCASE$", "LCASE$(stringexpression$)\nUCASE$(stringexpression$)", "Convert a string to lowercase / uppercase (letters A-Z only).\n\n  IF UCASE$(answer$) = \"Y\" THEN ...", "" },
{ "LEFT$", "LEFT$(stringexpression$,n%)\nRIGHT$(stringexpression$,n%)", "Return the leftmost / rightmost n% characters of a string (the whole\nstring if n% is larger than its length).\n\n  PRINT LEFT$(\"QBASIC\", 2), RIGHT$(\"QBASIC\", 3)     ' QB   SIC", "MID$ LEN" },
{ "LEN", "LEN(stringexpression$)\nLEN(variable)", "Returns the number of characters in a string, or the number of bytes\na variable takes (2 for INTEGER, 4 LONG/SINGLE, 8 DOUBLE, the record\nsize for a TYPE variable). Returns a LONG (strings can be up to 1 GB in\nQBLike).\n\n  PRINT LEN(\"QBASIC\")     ' 6", "" },
{ "LET", "[LET] variable=expression", "Assigns the value of an expression to a variable. The word LET is\noptional. Numbers are converted between numeric types (rounding to\ninteger types); assigning a string to a number or vice versa is a Type\nmismatch.\n\n  LET x = 5\n  name$ = \"Ann\": total& = total& + x", "" },
{ "LINE", "LINE [[STEP](x1!,y1!)]-[STEP](x2!,y2!) [,[color%] [,[B | BF] [,style%]]]", "Draws a line between two points, or a rectangle:\n  B     draws a box with the two points as opposite corners\n  BF    draws a filled box\nstyle% is a 16-bit pattern for dotted lines (&HFFFF solid, &HAAAA\ndotted, &HF0F0 dashed). Without the first point, the line starts at the\nlast point drawn. STEP makes a point relative to the previous one.\n\n  LINE (0, 0)-(319, 199), 14\n  LINE (10, 10)-(100, 50), 4, BF\n  LINE -STEP(20, 0), , , &HAAAA", "CIRCLE PSET LINE INPUT" },
{ "LINE INPUT", "LINE INPUT [;] [\"prompt\";] variable$\nLINE INPUT #filenumber%, variable$", "Reads a whole line (up to Enter), including commas and quotes, into\na string variable. From a file, reads up to the end of the line. With\n; after LINE INPUT the cursor stays on the same line after Enter.\n\n  LINE INPUT \"Your name: \"; n$\n  DO UNTIL EOF(1): LINE INPUT #1, l$: PRINT l$: LOOP", "INPUT" },
{ "LOADBMP", "LOADBMP filespec$, arrayname[(index%)]\nLOADBMP filespec$, SCREEN, x!, y! [, PALETTE]",
  "QBLike extension. Loads an indexed BMP (2, 16 or 256 colors; uncompressed,\nRLE4 or RLE8).\n\nArray form: stores the image in GET/PUT format for the current screen\nmode, so PUT (x, y), arrayname(index) draws it. The array must be large\nenough (Illegal function call otherwise, like GET).\n\nSCREEN form: draws the image at (x, y). The image must fit the current\nmode's colors (256 colors needs SCREEN 13, 16 colors a 16-color mode).\nPALETTE also sets the screen palette from the file.\n\n  SCREEN 13\n  LOADBMP \"fondo.bmp\", SCREEN, 0, 0, PALETTE\n  DIM nave%(130): LOADBMP \"nave.bmp\", nave%(0)\n  PUT (100, 100), nave%(0), MASK", "LOADGIF LOADPAL PUT GET" },
{ "LOADGIF", "LOADGIF filespec$, arrayname[(index%)]\nLOADGIF filespec$, SCREEN, x!, y! [, PALETTE]",
  "QBLike extension. Like LOADBMP but for GIF files.\n\nAnimated GIFs: every frame is composed on the GIF's logical screen and\nstored in the array one after another, each frame taking\n  (4 + bytes of pixels + elementsize - 1) \\ elementsize  elements.\nIn SCREEN 13 with an INTEGER array: (4 + w * h + 1) \\ 2 per frame.\nThe SCREEN form draws the first frame.", "LOADBMP LOADPAL PUT" },
{ "LOADPAL", "LOADPAL filespec$, arrayname&[(index%)]",
  "QBLike extension. Loads only the palette of a BMP or GIF file into a LONG\narray, one element per color, in PALETTE USING format\n(65536 * blue + 256 * green + red, each 0-63).\n\n  DIM pal&(255)\n  LOADPAL \"fondo.gif\", pal&(0)\n  PALETTE USING pal&(0)", "PALETTE LOADBMP LOADGIF" },
{ "LOC", "LOC(filenumber%)", "Returns the current position within a file: for RANDOM files the\nnumber of the last record read or written, for BINARY files the last\nbyte position, and for sequential files the position divided by 128.", "SEEK EOF" },
{ "LOCATE", "LOCATE [row%] [,[column%] [,[cursor%] [,start% [,stop%]]]]", "Moves the text cursor and sets its shape.\n  row%, column%   new position (1-based); omitted values stay the same\n  cursor%         1 shows the cursor, 0 hides it\n  start%, stop%   first and last scan line of the cursor shape\n\n  LOCATE 12, 30: PRINT \"GAME OVER\"\n  LOCATE , , 0                   ' hide the cursor", "CSRLIN POS" },
{ "LOCK", "LOCK [#]filenumber% [, {record& | [start&] TO end&}]\nUNLOCK [#]filenumber% [, {record& | [start&] TO end&}]", "Lock part of a file against other programs on a network. Accepted; they\nhave no effect.", "OPEN" },
{ "LOF", "LOF(filenumber%)", "Returns the length of an open file in bytes.", "EOF" },
{ "LOG", "LOG(numeric-expression)", "Returns the natural logarithm (base e) of a positive number. For\nbase 10: LOG(x) / LOG(10).", "EXP" },
{ "LPRINT", "LPRINT [expressionlist] [{; | ,}]\nLPRINT USING formatstring$; expressionlist", "Prints on the printer. QBLike has no printer: the output is discarded.\nLPOS(n%) returns 0.", "PRINT" },
{ "LSET", "LSET stringvariable$ = stringexpression$\nRSET stringvariable$ = stringexpression$\nLSET record1 = record2", "Copy a string into a variable without changing its length: LSET aligns\nit to the left, RSET to the right, padding with spaces or cutting the\nend. Used with FIELD buffers and fixed-length strings. LSET also copies\none record variable into another of a different TYPE.", "FIELD MID$" },
{ "LTRIM$", "LTRIM$(stringexpression$)\nRTRIM$(stringexpression$)", "LTRIM$ removes the leading spaces of a string, RTRIM$ the trailing\nones.\n\n  PRINT \"[\" + LTRIM$(STR$(5)) + \"]\"     ' [5]", "" },
{ "MID$", "MID$(stringexpression$,start%[,length%])        (function)\nMID$(stringvariable$,start%[,length%])=expression$   (statement)", "Function: returns length% characters of a string starting at\nposition start% (all the rest if length% is omitted).\nStatement: replaces characters of a string variable, starting at\nstart%, with those of expression$; the length of the variable does\nnot change.\n\n  PRINT MID$(\"QBASIC 1.1\", 3, 4)        ' ASIC\n  a$ = \"HELLO\": MID$(a$, 2) = \"AL\": PRINT a$    ' HALLO", "LEFT$ INSTR" },
{ "MKI$", "MKI$(integer%)  MKL$(long&)  MKS$(single!)  MKD$(double#)", "Convert numbers to strings holding their binary representation:\nMKI$ 2 bytes, MKL$ 4, MKS$ 4, MKD$ 8. Used to store numbers in FIELD\nbuffers; CVI, CVL, CVS and CVD convert them back.", "CVI" },
{ "MKDIR", "MKDIR pathspec$", "Creates a directory.", "CHDIR RMDIR" },
{ "MOD", "numeric-expression1 MOD numeric-expression2", "Remainder of an integer division. The operands are rounded to integers\nfirst; the result has the sign of the first one.\n\n  PRINT 17 MOD 5, -7 MOD 3     ' 2  -1", "Operators" },
{ "MOUSE", "MOUSE x%, y%, buttons%   or   MOUSE ON | OFF", "QBLike extension. Reads the mouse into three numeric variables.\n\nx% and y% get the pointer position on the visible screen in the current\nmode's resolution (0-319 by 0-199 in SCREEN 13 and SCREEN 1, 0-639 by\n0-479 in SCREEN 12; text column and row, from 0, in SCREEN 0). They are\n-1, -1 when the pointer is outside the picture or the window. Coordinates\nare absolute: VIEW and WINDOW are not applied.\n\nbuttons%: bit 0 = left button, bit 1 = right button\n(1 left, 2 right, 3 both, 0 none).\n\nMOUSE ON / MOUSE OFF show or hide the mouse cursor (an arrow in graphics\nmodes, an inverted block in SCREEN 0). It is hidden when a program\nstarts; MOUSE still reads the mouse while it is hidden. The cursor is overlaid on the displayed picture only, so POINT,\nGET and PEEK never see it.\n\n  DO\n      MOUSE x%, y%, b%\n      IF b% AND 1 THEN PSET (x%, y%), 15\n  LOOP UNTIL INKEY$ = CHR$(27)", "MULTIKEY INKEY$" },
{ "MULTIKEY", "MULTIKEY(scancode%)", "QBLike extension. Returns -1 while the key with the given scan code is\nheld down, 0 otherwise. Several keys can be checked at once, ideal for\ngames.\n\nCommon scan codes: Esc 1, Enter 28, Space 57, Ctrl 29, Alt 56,\nLeft Shift 42, Up 72, Down 80, Left 75, Right 77, A 30, D 32, S 31, W 17.", "INKEY$ INP" },
{ "MUSIC", "MUSIC filespec$[, LOOP]\nMUSIC STOP\nMUSIC VOLUME volume%\nMUSIC", "QBLike extension. Plays background music while the program goes on.\n\n  MUSIC file$        plays a MOD (ProTracker) or MID (MIDI, on the FM\n                     synthesizer) file; with LOOP it starts again at the\n                     end. A new MUSIC replaces the current music.\n  MUSIC STOP         stops it.\n  MUSIC VOLUME v%    0-255 (255 at start), also while playing.\n  MUSIC              as a function: -1 while music plays, 0 if not.\n\n  MUSIC \"TITLE.MOD\", LOOP\n  ...\n  MUSIC \"GAMEOVER.MID\"\n  DO: LOOP WHILE MUSIC\n\nMUSIC is not a reserved word: programs with their own variable or\nSUB called MUSIC keep working.", "SOUND PLAY" },
{ "NAME", "NAME oldspec$ AS newspec$", "Renames a file or moves it to another directory on the same drive.\n\n  NAME \"OLD.TXT\" AS \"NEW.TXT\"", "KILL" },
{ "OCT$", "OCT$(numeric-expression&)", "Returns the octal representation of a number (rounded to an integer)\nas a string.\n\n  PRINT OCT$(8)       ' 10", "HEX$" },
{ "ON ERROR", "ON ERROR GOTO line\nON ERROR GOTO 0\n...\nRESUME [NEXT | line]", "ON ERROR GOTO line turns on error trapping: when an error happens,\nthe program jumps to the handler at line. ERR gives the error code and\nERL the line number. The handler ends with RESUME. ON ERROR GOTO 0\nturns trapping off (an error inside the handler then stops the\nprogram).\n\n  ON ERROR GOTO Handler\n  OPEN \"DATA.TXT\" FOR INPUT AS #1\n  ...\n  Handler:\n      IF ERR = 53 THEN PRINT \"File not found\": RESUME NEXT\n      ON ERROR GOTO 0", "RESUME ERR ERROR" },
{ "ON GOSUB", "ON expression% GOSUB line-list\nON expression% GOTO line-list", "Jumps to the n-th label of the list, where n is the value of the\nexpression. If it is 0 or greater than the number of labels, the\nprogram continues with the next statement.\n\n  ON choice% GOSUB NewGame, LoadGame, Quit", "GOSUB GOTO" },
{ "ON PLAY", "ON PLAY(n%) GOSUB line\nPLAY ON | OFF | STOP", "Calls a subroutine when the background music queue drops below n%\nnotes (1-32), so it can be refilled:\n\n  ON PLAY(2) GOSUB More: PLAY ON\n  PLAY \"MB L8 CDEFG\"\n  ...\n  More: PLAY \"MB EDC\": RETURN", "PLAY" },
{ "ON TIMER", "ON TIMER(n%) GOSUB line\nTIMER ON | OFF | STOP", "Calls a subroutine every n% seconds (1 to 86400) once TIMER ON has\nbeen executed. TIMER OFF disables the event; TIMER STOP delays it\nuntil TIMER ON. The event is checked between statements.\n\n  ON TIMER(1) GOSUB Clock: TIMER ON\n  DO: LOOP UNTIL INKEY$ <> \"\"\n  END\n  Clock: LOCATE 1, 70: PRINT TIME$: RETURN", "TIMER" },
{ "OPEN", "OPEN file$ [FOR mode] [ACCESS access] [lock] AS [#]filenumber% [LEN=reclen%]\n\nmode: APPEND, BINARY, INPUT, OUTPUT, RANDOM\n\nOPEN mode$, [#]filenumber%, file$[, reclen%]", "Opens a file. DOS-style names (C:\\DIR\\FILE.TXT) are accepted;\non Linux the name is matched case-insensitively.", "CLOSE GET PUT INPUT PRINT" },
{ "OPTION BASE", "OPTION BASE {0 | 1}", "Sets the default lower bound of arrays (0 or 1) for DIM statements\nthat give only the upper bound. Must appear before any array is\ndimensioned.", "DIM" },
{ "OUT", "OUT port%, data%", "Emulated ports:\n  &H3C8  select palette index to write\n  &H3C9  write R, G, B (0-63) successively\n  &H3C7  select palette index to read\n  &H3C4  sequencer index; &H3C5 data (index 2 = planes written)\n  &H3CE  graphics index; &H3CF data (index 4 = plane read)\n  &H3D8  CGA mode control: in SCREEN 2, bit 2 = 0 turns the color\n         burst on (composite artifact colors): OUT &H3D8, 26\n  &H3D9  CGA color select: in SCREEN 2, foreground color (0-15)\n  &H388  AdLib FM (OPL) register index; &H389 register data\n         (also &H220/&H221; &H38A/&H38B = OPL3 second bank)\n\n  OUT &H3C8, 0: OUT &H3C9, 10: OUT &H3C9, 20: OUT &H3C9, 50", "INP PALETTE SCREEN DEF SEG" },
{ "PAINT", "PAINT [STEP](x!,y!)[,[{color% | tile$}] [,[bordercolor%] [,background$]]]", "Fills an area, starting at (x, y), with color% or with the tile\npattern tile$, until it reaches pixels of bordercolor% (by default the\nfill color). The area must be completely enclosed by that color. To\nfill regardless of the border colors, see <<FILL>>.\n\n  CIRCLE (160, 100), 50, 15\n  PAINT (160, 100), 4, 15", "CIRCLE LINE DRAW" },
{ "PALETTE", "PALETTE [attribute%,color&]\nPALETTE USING arrayname#[(index%)]", "Changes colors in the palette. In SCREEN 12 and 13 color& is\n65536 * blue + 256 * green + red (each 0-63).", "COLOR OUT SCREEN" },
{ "PCOPY", "PCOPY sourcepage%, destinationpage%", "Copies one video page to another (SCREEN 0, 7, 8, 9 and 13 have several\npages). Used for flicker-free animation: draw on a hidden page and copy\nit to the visible one.\n\n  SCREEN 7, 0, 1, 0        ' draw on page 1, show page 0\n  ... draw ...\n  PCOPY 1, 0", "SCREEN" },
{ "PEEK", "PEEK(address)", "Returns the byte at the given offset of the segment set with DEF SEG.\nIn QBLike it works on emulated video memory, some BIOS areas and the\nnumeric arrays (addresses given by VARSEG/VARPTR): see <<DEF SEG>>.", "POKE DEF SEG VARPTR" },
{ "PEN", "PEN(n%)\nPEN ON | OFF | STOP\nON PEN GOSUB line", "Light pen. PEN(n%) always returns 0 and the event never fires.", "MOUSE" },
{ "PLAY", "PLAY commandstring$\nPLAY(n)\nPLAY ON | OFF | STOP", "Plays music described by a string of commands (upper or lower case,\nspaces ignored). Octave, length, tempo and mode stay in effect for the\nnext PLAY.\n\n  A-G [#|+|-] [n]  a note; # or + sharp, - flat; n length (1-64)\n  N n              note number 0-84 (0 = rest); octave and length as set\n  O n              octave 0-6 (default 4); < and > go down / up one\n  L n              default length: 1 whole note, 4 quarter (default),\n                   8 eighth... up to 64\n  P n              pause (rest) of length n\n  .                after a note or pause: 1.5 times its length (.. 1.75)\n  T n              tempo: quarter notes per minute, 32-255 (default 120)\n  MN ML MS         normal (7/8 of the length), legato, staccato (3/4)\n  MF MB            foreground (PLAY waits) or background (the program\n                   goes on while up to 32 notes play)\n  X + VARPTR$(a$)  plays the commands in a$\n\nAny number can be written as \"=\" + VARPTR$(variable).\nPLAY(n) returns the number of notes still in the background queue.\nPLAY ON / OFF / STOP enable the ON PLAY event.\n\n  PLAY \"T160 O3 L8 CDEFGAB > C4\"\n  PLAY \"MB L16 EGEG >C<G\"          ' the program continues meanwhile\n  PLAY \"T=\" + VARPTR$(tempo%)", "ON PLAY SOUND BEEP" },
{ "PMAP", "PMAP(startcoordinate#, n%)", "Maps between logical (WINDOW) and physical coordinates.\nn: 0 = x to physical, 1 = y to physical, 2 = x to logical, 3 = y to logical.", "WINDOW VIEW" },
{ "POINT", "POINT(x%,y%)\nPOINT(number%)", "Returns a pixel color, or the graphics cursor position\n(0 = physical x, 1 = physical y, 2 = logical x, 3 = logical y).", "PSET" },
{ "POKE", "POKE address, byte%", "Writes a byte to memory at the DEF SEG segment.\nExample (SCREEN 13): DEF SEG = &HA000: POKE y * 320 + x, color", "PEEK DEF SEG" },
{ "POS", "POS(0)", "Returns the current column of the text cursor (1-based). The argument\nis ignored. CSRLIN returns the row.", "CSRLIN LOCATE" },
{ "PRINT", "PRINT [#filenumber%,] [expressionlist] [{; | ,}]", "Writes data to the screen or a file. ; keeps the cursor after the last\nitem, , moves to the next 14-column zone. Positive numbers are printed\nwith a leading space and all numbers with a trailing space.\n? is an abbreviation for PRINT.", "PRINT USING WRITE LPRINT" },
{ "PRINT USING", "PRINT [#filenumber%,] USING formatstring$; expressionlist [{; | ,}]", "Prints formatted output.\n\n #  digit      .  decimal point    ,  thousands      +  sign\n -  trailing minus   $$  floating $    **  * fill     ^^^^  exponent\n !  first char  \\  \\  n chars       &  whole string   _  literal", "PRINT" },
{ "PSET", "PSET [STEP](x!,y!) [,color%]\nPRESET [STEP](x!,y!) [,color%]", "Draws a point at (x, y) in color% (default: the foreground color for\nPSET, the background color for PRESET). STEP makes the coordinates\nrelative to the last point drawn.\n\n  FOR x% = 0 TO 319: PSET (x%, 100 + SIN(x% / 20) * 50), 10: NEXT", "POINT LINE" },
{ "PUT", "PUT [#]filenumber%[,[record&][,variable]]        (files)\nPUT [surface,] [STEP](x!,y!), data [,actionverb]   (graphics)\n\nsurface, data:  arrayname[(index%)]  |  SEG segment%, offset%\nactionverb: PSET, PRESET, AND, OR, XOR (default) or MASK", "Writes to a file, or draws an image captured with GET.\nMASK (QBLike extension) skips pixels of color 0, so sprites can have\ntransparent backgrounds: PUT (x%, y%), sprite%(0), MASK\nIn QBLike PUT is clipped to the CLIP rectangle instead of failing when\nthe image is partly or totally off-screen.\n\nQBLike extensions (SCREEN 13):\n  * surface: draws into an image stored in an array instead of the\n    screen, clipped to that image. A full-screen buffer is\n    4 + 320 * 200 bytes (DIM buf%(32001)) with its header set, or taken\n    with GET; compose a frame there and PUT it to the screen at once:\n      buf%(0) = 320 * 8: buf%(1) = 200       ' header: width * 8, height\n      PUT buf%(0), (x%, y%), ship%(0), MASK\n      PUT (0, 0), buf%(0), PSET\n  * SEG segment%, offset%: the image (or the surface) at that address, as\n    given by VARSEG/VARPTR:\n      PUT (x%, y%), SEG s%, o%, MASK", "GET CLIP VARPTR" },
{ "RANDOMIZE", "RANDOMIZE [seed%]\nRANDOMIZE TIMER", "Initializes the random-number generator so RND gives a different\nsequence. Without a seed it asks for one; RANDOMIZE TIMER uses the\nclock, which gives a different sequence each run.", "RND" },
{ "READ", "READ variablelist", "Reads values from DATA statements into variables, in order. Reading\npast the last DATA gives the error Out of DATA. See <<DATA>>.", "DATA RESTORE" },
{ "REDIM", "REDIM [PRESERVE] [SHARED] variable(subscripts) [AS type] [,...]", "Changes the size of a dynamic array (and creates it if needed). The\ncontents are erased, unless PRESERVE is used, which keeps the values\n(in memory order, so they stay in place when only the upper bound\nof the last dimension changes). The type and number of dimensions\ncannot change.\n\n  REDIM enemies(count%) AS EnemyType\n  REDIM PRESERVE scores%(1 TO n% + 10)", "DIM ERASE" },
{ "REM", "REM remark\n' remark", "Adds a comment: the rest of the line is ignored. An apostrophe (') does\nthe same and can follow a statement on the same line.\n\n  REM Main loop\n  x% = 0   ' reset the position", "" },
{ "RESET", "RESET", "Closes all open files.", "CLOSE" },
{ "RESTORE", "RESTORE [line]", "Makes the next READ start again from the first DATA statement, or\nfrom the first DATA after the given label.\n\n  RESTORE Level2: READ w%, h%", "DATA READ" },
{ "RESUME", "RESUME [0]\nRESUME NEXT\nRESUME line", "Ends an error handler (see ON ERROR):\n  RESUME or RESUME 0   runs the statement that caused the error again\n  RESUME NEXT          continues with the statement after it\n  RESUME line          continues at a label", "ON ERROR" },
{ "RETURN", "RETURN [line]", "Returns from a GOSUB subroutine (or event handler such as ON TIMER)\nto the statement after the GOSUB, or to another label with\nRETURN line.", "GOSUB" },
{ "RIGHT$", "RIGHT$(stringexpression$,n%)", "See LEFT$.", "LEFT$" },
{ "RMDIR", "RMDIR pathspec$", "Removes an empty directory.", "MKDIR" },
{ "RND", "RND[(n#)]", "Returns a single-precision random number between 0 and 1.\nThe sequence is the same as in QBasic for the same seed.", "RANDOMIZE" },
{ "RUN", "RUN [line]", "Restarts the current program from the beginning: variables are\ncleared and files closed. RUN with a line number or file name is not\nsupported (Advanced feature unavailable).", "END" },
{ "SCREEN", "SCREEN mode% [,[colorswitch%] [,[activepage%] [,visualpage%]]]\nSCREEN FULL | WINDOW\n\nSCREEN(row%, column% [,colorflag%])   (function)",
  "Sets the screen mode.\n\n  0  text 80x25 (40x25, 80x43, 80x50 with WIDTH), 16 colors, 8 pages\n  1  320x200, 4 colors          2  640x200, 2 colors\n  3  160x200, 16 colors, 2 pages (PCjr/Tandy; QBLike extension)\n  7  320x200, 16 colors, 8 pages\n  8  640x200, 16 colors, 4 pages\n  9  640x350, 16 colors, 2 pages\n 11  640x480, 2 colors          12  640x480, 16 colors\n 13  320x200, 256 colors, 64 pages (QBasic: 1; QBLike extension)\n\nSCREEN 2 with OUT &H3D8, 26 turns the CGA color burst on: composite\nartifact colors, each group of 4 pixels shown as one of 16 colors.\nOUT &H3D8, 30 turns it off again.\n\nSCREEN 3 (PCjr/Tandy 1000): pixels are twice as wide as tall, 20x25 text.\nMemory at &HB800: 2 pixels per byte (high nibble = left), even lines at\noffset 0 and odd lines at &H2000, 80 bytes per line.\n\nSCREEN FULL and SCREEN WINDOW (QBLike extension) switch QBLike to full\nscreen or to a window, like Alt+Enter or F11. The screen mode and its\ncontents don't change. FULL is not a reserved word: if the program has a\nvariable called FULL, SCREEN full sets the mode in that variable.\n\nThe function SCREEN(row, col) returns the character code at a position.", "COLOR PALETTE PCOPY WIDTH OUT" },
{ "SEEK", "SEEK [#]filenumber%, position&\nSEEK(filenumber%)", "SEEK statement: sets the position for the next read or write: the\nrecord number for RANDOM files, the byte position (from 1) for other\nfiles. SEEK function: returns the current position the same way.", "LOC" },
{ "SELECT", "SELECT CASE testexpression\nCASE expressionlist1\n    [statementblock-1]\n[CASE expressionlist2\n    [statementblock-2]]...\n[CASE ELSE\n    [statementblock-n]]\nEND SELECT\n\nexpressionlist: expression, expression TO expression, IS relop expression", "Executes the block of the first CASE that matches the value of the\ntest expression; CASE ELSE runs if none matches.\n\n  SELECT CASE score%\n      CASE 100: PRINT \"Perfect\"\n      CASE 90 TO 99: PRINT \"Great\"\n      CASE IS < 50, 77: PRINT \"Hmm\"\n      CASE ELSE: PRINT \"OK\"\n  END SELECT", "IF" },
{ "SETMEM", "SETMEM(numeric-expression&)", "QuickBASIC 4.5 function: enlarges (positive value) or reduces (negative)\nthe far heap and returns its new size. QBLike has no such limit, so it\nonly keeps the count, starting at 393216 bytes; programs that free\nmemory for assembly libraries (dummy& = SETMEM(-66000)) work as before.\nNot a reserved word.", "FRE" },
{ "SGN", "SGN(numeric-expression)", "Returns -1 if the number is negative, 0 if it is zero and 1 if it is\npositive.", "ABS" },
{ "SHARED", "SHARED variable[()] [AS type] [,variable[()] [AS type]]...", "Inside a SUB or FUNCTION, gives access to module-level variables\nwithout declaring them DIM SHARED in the module.\n\n  SUB AddPoints (n%)\n      SHARED score&\n      score& = score& + n%\n  END SUB", "DIM STATIC" },
{ "SHELL", "SHELL [commandstring$]", "Runs an operating system command (cmd.exe on Windows, the shell on\nLinux) and waits until it finishes. SHELL alone does nothing in\nQBLike.", "" },
{ "SIN", "SIN(angle)", "Returns the sine of an angle given in radians (degrees * PI / 180).\n\n  x = 160 + COS(a) * r: y = 100 + SIN(a) * r", "COS TAN ATN" },
{ "SLEEP", "SLEEP [seconds&]", "Suspends execution until a key is pressed or the given number of\nseconds has passed. SLEEP or SLEEP 0 waits only for a key. The key\nstays in the keyboard buffer (read it with INKEY$).", "INKEY$" },
{ "SOUND", "SOUND frequency, duration\nid% = SOUND filespec$\nSOUND PLAY id%[, channel%][, volume%][, pan%][, rate&][, LOOP]\nSOUND STOP [channel%]\nplaying% = SOUND(channel%)", "SOUND frequency, duration generates a tone. Duration is in clock ticks\n(18.2 per second).\n\nQBLike extension, sound effects from WAV files (mono or stereo,\n4000-48000 Hz):\n  id% = SOUND file$   loads the file and returns its number (loading\n                      the same file again returns the same number)\n  SOUND PLAY          plays it on a channel (1-12; omitted = a free one).\n                      volume% 0-255 (255), pan% -100 left to 100 right\n                      (0), rate& plays the sound as if it had been\n                      recorded at that sample rate (1000-96000 Hz;\n                      omitted = its own rate), so 2 * rate is an octave\n                      up. LOOP repeats it until SOUND STOP. Playing on\n                      a busy channel replaces what was playing there.\n  SOUND STOP          stops a channel, or all of them without channel%.\n  SOUND(channel%)     -1 while that channel is playing, 0 if it is free\n                      (SOUND(0): any channel).\n\n  boom% = SOUND \"BOOM.WAV\"\n  SOUND PLAY boom%, , 200, -60\n  SOUND PLAY boom%, 3, , , 22050       ' an octave higher (11025 Hz file)", "PLAY BEEP MUSIC" },
{ "SPACE$", "SPACE$(n%)", "Returns a string of n% spaces.\n\n  PRINT \"A\"; SPACE$(5); \"B\"", "STRING$ SPC" },
{ "SPC", "SPC(n%)", "Used in PRINT: skips n% spaces.\n\n  PRINT \"A\"; SPC(10); \"B\"", "TAB PRINT" },
{ "SQR", "SQR(numeric-expression)", "Returns the square root of a number (Illegal function call if it is\nnegative).\n\n  d = SQR((x2 - x1) ^ 2 + (y2 - y1) ^ 2)", "" },
{ "STATIC", "STATIC variablelist", "Inside a SUB or FUNCTION, makes the listed variables keep their\nvalues between calls (they are not reset on each call). SUB name\nSTATIC does this for all local variables.\n\n  SUB Counter\n      STATIC calls%\n      calls% = calls% + 1: PRINT calls%\n  END SUB", "SHARED SUB" },
{ "STICK", "STICK(n%)\nSTRIG(n%)\nSTRIG(n%) ON | OFF | STOP\nON STRIG(n%) GOSUB line", "Joystick position and buttons. There is no joystick support: STICK and\nSTRIG return 0 and the events never fire.", "MOUSE MULTIKEY" },
{ "STOP", "STOP", "Halts the program and returns to the editor (like a breakpoint).", "END" },
{ "STR$", "STR$(numeric-expression)", "Returns a number as a string, as PRINT would show it: with a leading\nspace for positive numbers (no trailing space).\n\n  PRINT \"[\" + STR$(42) + \"]\"     ' [ 42]", "VAL" },
{ "STRING$", "STRING$(length%,{ascii-code% | stringexpression$})", "Returns a string of length% copies of a character, given by its\nASCII code or as the first character of a string.\n\n  PRINT STRING$(40, \"-\"); STRING$(3, 3)", "SPACE$" },
{ "SUB", "SUB name[(parameterlist)] [STATIC]\n    [statementblock]\nEND SUB", "Defines a SUB procedure. Type SUB name in the editor (or use\nEdit > New SUB) and it gets its own window; press F2 to switch.", "CALL FUNCTION SHARED STATIC" },
{ "SWAP", "SWAP variable1, variable2", "Exchanges the values of two variables or array elements of the same\ntype.\n\n  IF a% > b% THEN SWAP a%, b%", "" },
{ "SYSTEM", "SYSTEM", "Ends the program (and closes QBLike when run with /X).", "END" },
{ "TAB", "TAB(column%)", "Used in PRINT: moves to column column% (to the next line if the cursor\nis already past it).\n\n  PRINT \"Name\"; TAB(20); \"Score\"", "SPC PRINT" },
{ "TAN", "TAN(angle)", "Returns the tangent of an angle given in radians.", "SIN COS ATN" },
{ "TIME$", "TIME$", "Returns the current time as \"hh:mm:ss\" (24-hour clock). (Setting the\ntime with TIME$ = ... is accepted and ignored.)", "DATE$ TIMER" },
{ "TIMER", "TIMER\nTIMER ON | OFF | STOP", "TIMER returns the number of seconds since midnight, with fractions\n(accurate to milliseconds in QBLike). TIMER ON / OFF / STOP control the\nON TIMER event.\n\n  t! = TIMER: DO: LOOP WHILE TIMER < t! + .5    ' wait half a second", "ON TIMER TIME$" },
{ "TRON", "TRON\nTROFF", "TRON / TROFF turned the line-number trace on and off. Accepted for\ncompatibility; use F8 (step) or Debug > Trace On in the editor.", "" },
{ "TYPE", "TYPE usertype\n    elementname AS typename\n    ...\nEND TYPE", "Defines a record data type. Elements can be INTEGER, LONG, SINGLE,\nDOUBLE, STRING * n or another TYPE.", "DIM" },
{ "UBOUND", "UBOUND(array[,dimension%])", "See LBOUND.", "LBOUND" },
{ "UCASE$", "UCASE$(stringexpression$)", "See LCASE$.", "LCASE$" },
{ "VAL", "VAL(stringexpression$)", "Converts a string to a number, reading up to the first character that\nis not part of a number. Blanks are ignored; &H (hexadecimal) and &O\n(octal) prefixes are accepted. Returns 0 if there is no number.\n\n  PRINT VAL(\"12abc\"), VAL(\"&HFF\")     ' 12   255", "STR$" },
{ "VARPTR", "VARPTR(variable)\nVARSEG(variable)", "Return the offset and the segment of a variable in memory.\n\nQBLike emulates real-mode addresses for the elements of numeric arrays:\nVARSEG/VARPTR of an element give an address where PEEK, POKE, BLOAD,\nBSAVE and GET/PUT ... SEG see that array's bytes (as INTEGER values,\nlike in QBasic: offsets over 32767 are negative). Arrays get their\naddress the first time VARSEG or VARPTR is used on them, inside the\nconventional memory range (&H1000-&H9FFF and &HC000-&HEFFF).\n\n  DIM spr%(129)\n  DEF SEG = VARSEG(spr%(0))\n  PRINT PEEK(VARPTR(spr%(0)))           ' low byte of spr%(0)\n  POKE VARPTR(spr%(2)), 15              ' first pixel of a GET image\n  DEF SEG\n\nFor other variables (scalars, strings) both return 0.", "DEF SEG PEEK POKE BLOAD" },
{ "VARPTR$", "VARPTR$(variable)", "Returns a 3-character reference to a variable, used inside the PLAY\nand DRAW command strings:\n\n  DRAW \"R=\" + VARPTR$(n%) + \"X\" + VARPTR$(shape$)\n  PLAY \"P=\" + VARPTR$(p%)", "PLAY DRAW VARPTR" },
{ "VARSEG", "VARSEG(variable)", "See VARPTR.", "VARPTR" },
{ "VIEW", "VIEW [[SCREEN] (x1!,y1!)-(x2!,y2!) [,[color%] [,border%]]]", "Defines a rectangle of the screen as the graphics viewport: drawing\nis clipped to it. Without SCREEN, coordinates are relative to the\nviewport's corner; with SCREEN, they stay absolute. color% fills it,\nborder% draws a frame. VIEW alone restores the whole screen.", "WINDOW VIEW PRINT CLS" },
{ "VIEW PRINT", "VIEW PRINT [toprow% TO bottomrow%]", "Limits text output and scrolling to the rows toprow% to bottomrow%.\nVIEW PRINT alone restores the whole screen. CLS 2 clears the area.\n\n  VIEW PRINT 3 TO 22", "VIEW CLS" },
{ "WAIT", "WAIT &H3DA, 8\nWAIT VSYNC", "Waits for the start of the next vertical retrace (emulated at 70 Hz, like\na VGA card) and shows what has been drawn, so animation doesn't flicker.\nOther ports are ignored.\n\nWAIT VSYNC is a QBLike alias of WAIT &H3DA, 8. VSYNC is not a reserved word:\nprograms with their own SUB VSync keep working.\n\n  DO\n      WAIT VSYNC\n      PUT (x, y), sprite, PSET\n  LOOP", "INP" },
{ "WHILE", "WHILE condition\n    [statementblock]\nWEND", "Repeats the block while the condition is true (checked at the start).\nDO...LOOP is more flexible (EXIT DO, condition at the end).\n\n  WHILE INKEY$ = \"\": WEND", "DO" },
{ "WIDTH", "WIDTH [columns%] [,rows%]", "Changes the text screen size (40/80 columns; 25, 43 or 50 rows in\nSCREEN 0; 30 or 60 rows in SCREEN 12; 20 columns in SCREEN 3).", "SCREEN" },
{ "WINDOW", "WINDOW [[SCREEN] (x1!,y1!)-(x2!,y2!)]", "Defines logical coordinates for graphics: (x1, y1) and (x2, y2) are\nthe corners of the viewport. Without SCREEN, y grows upwards (like in\nmathematics); WINDOW SCREEN keeps y growing downwards. WINDOW alone\nreturns to pixel coordinates. PMAP converts between both.\n\n  WINDOW (-10, -10)-(10, 10)\n  LINE (-10, 0)-(10, 0): LINE (0, -10)-(0, 10)", "VIEW PMAP" },
{ "WRITE", "WRITE [#filenumber%,] expressionlist", "Writes values separated by commas, with strings in quotes, so INPUT\ncan read them back exactly.\n\n  WRITE #1, name$, age%, score!      ' \"Ann\",31,1500", "PRINT INPUT" },
{ NULL, NULL, NULL, NULL }
};

static const char *aliases[][2] = {
    { "LOOP", "DO" }, { "UNTIL", "DO" }, { "WEND", "WHILE" }, { "NEXT", "FOR" }, { "TO", "FOR" }, { "STEP", "FOR" },
    { "THEN", "IF" }, { "ELSE", "IF" }, { "ELSEIF", "IF" }, { "DEF", "DEF FN" }, { "DEFLNG", "DEFINT" }, { "DEFSNG", "DEFINT" },
    { "DEFDBL", "DEFINT" }, { "DEFSTR", "DEFINT" }, { "PRESET", "PSET" }, { "ERL", "ERR" }, { "MKL$", "MKI$" }, { "MKS$", "MKI$" },
    { "MKD$", "MKI$" }, { "RTRIM$", "LTRIM$" }, { "USING", "PRINT USING" }, { "AS", "DIM" }, { "INTEGER", "DIM" }, { "LONG", "DIM" },
    { "SINGLE", "DIM" }, { "DOUBLE", "DIM" }, { "STRING", "DIM" }, { "ON", "ON ERROR" }, { "BASE", "OPTION BASE" }, { "OPTION", "OPTION BASE" },
    { "TROFF", "TRON" }, { "IS", "SELECT" }, { "AND", "Operators" }, { "OR", "Operators" }, { "NOT", "Operators" }, { "XOR", "Operators" },
    { "EQV", "Operators" }, { "IMP", "Operators" }, { "SEG", "DEF SEG" }, { "PRESERVE", "REDIM" }, { "?", "PRINT" },
    { "ACCESS", "OPEN" }, { "APPEND", "OPEN" }, { "BINARY", "OPEN" }, { "OUTPUT", "OPEN" }, { "RANDOM", "OPEN" },
    { "ANY", "DECLARE" }, { "OFF", "ON TIMER" }, { "LIST", "KEY" }, { "UNLOCK", "LOCK" }, { "RSET", "LSET" },
    { "STRIG", "STICK" }, { "MAKE", "Make EXE" }, { "INCLUDE", "$INCLUDE" }, { "$INCLUDE:", "$INCLUDE" }, { "EXE", "Make EXE" }, { "CVDMBF", "CVSMBF" }, { "MKSMBF$", "CVSMBF" }, { "MKDMBF$", "CVSMBF" },
    { "ENVIRON$", "ENVIRON" }, { "ERDEV$", "ERDEV" }, { "IOCTL$", "IOCTL" }, { "LPOS", "LPRINT" },
    { NULL, NULL }
};

static const char *extra_topics[][2] = {
    { "Contents",
      "  " QB_NAME " Help: Contents\n\n"
      "  <<Using Help>>            <<Editing Keys>>          <<Debugging>>\n"
      "  <<Index>>                 <<Data Types>>            <<Operators>>\n"
      "  <<Errors>>                <<Game Programming>>      <<Menus>>\n\n"
      "  Keywords by task:\n\n"
      "  Program flow:   <<IF>> <<SELECT>> <<FOR>> <<DO>> <<WHILE>> <<GOSUB>> <<GOTO>>\n"
      "                  <<SUB>> <<FUNCTION>> <<CALL>> <<EXIT>> <<END>> <<ON ERROR>>\n"
      "  Variables:      <<DIM>> <<REDIM>> <<CONST>> <<TYPE>> <<SHARED>> <<STATIC>> <<DEFINT>>\n"
      "  Text screen:    <<PRINT>> <<PRINT USING>> <<INPUT>> <<LINE INPUT>> <<LOCATE>> <<COLOR>> <<CLS>>\n"
      "  Graphics:       <<SCREEN>> <<PSET>> <<LINE>> <<CIRCLE>> <<PAINT>> <<DRAW>> <<GET>> <<PUT>>\n"
      "                  <<PALETTE>> <<VIEW>> <<WINDOW>> <<PCOPY>> <<POINT>>\n"
      "                  <<LOADBMP>> <<LOADGIF>> <<LOADPAL>> <<CLIP>> <<FILL>>\n"
      "  Keyboard/mouse: <<INKEY$>> <<MULTIKEY>> <<MOUSE>> <<INPUT$>> <<SLEEP>>\n"
      "  Sound:          <<BEEP>> <<SOUND>> <<PLAY>> <<ON PLAY>> <<MUSIC>>\n"
      "  Strings:        <<LEFT$>> <<MID$>> <<INSTR>> <<LEN>> <<STR$>> <<VAL>> <<CHR$>> <<ASC>>\n"
      "  Math:           <<ABS>> <<INT>> <<FIX>> <<SQR>> <<SIN>> <<RND>> <<RANDOMIZE>>\n"
      "  Files:          <<OPEN>> <<CLOSE>> <<GET>> <<PUT>> <<EOF>> <<KILL>> <<FILES>> <<DIR$>>\n"
      "  Hardware:       <<DEF SEG>> <<PEEK>> <<POKE>> <<VARPTR>> <<INP>> <<OUT>> <<WAIT>>\n" },
    { "Using Help",
      "  Using Help\n\n"
      "  F1          Help on the keyword under the cursor\n"
      "  Shift+F1    This screen\n"
      "  Tab         Move to the next link (shown as <<Index>>)\n"
      "  Enter       Follow the selected link (or click it with the mouse)\n"
      "  Alt+F1      Go back to the previous topic\n"
      "  PgUp/PgDn   Scroll\n"
      "  Esc         Close Help\n\n"
      "  See also: <<Contents>> <<Index>>\n" },
    { "Editing Keys",
      "  Editing Keys\n\n"
      "  The editor follows modern conventions:\n\n"
      "  Arrows, Home, End, PgUp, PgDn    move           Ctrl+Left/Right   word by word\n"
      "  Ctrl+Home / Ctrl+End             start / end    Ctrl+Up/Down      scroll\n"
      "  Shift + any movement key         select text    Ctrl+A            select all\n"
      "  Mouse drag, double click         select         Shift+click       extend selection\n"
      "  Ctrl+C  Ctrl+X  Ctrl+V           copy, cut, paste (system clipboard;\n"
      "                                   without a selection: the whole line)\n"
      "  Ctrl+Ins  Shift+Del  Shift+Ins   copy, cut, paste (classic keys)\n"
      "  Ctrl+Z  Ctrl+Y                   undo, redo\n"
      "  Ctrl+Backspace  Ctrl+Del         delete word\n"
      "  Tab / Shift+Tab                  indent / unindent (also selected lines)\n"
      "  Left at a tab stop in spaces     moves back a whole tab (Options > Display)\n"
      "  Ctrl+Shift+F                     Edit > Format Code (see below)\n"
      "  Insert                           toggle overwrite mode\n"
      "  Ctrl+S  Ctrl+O  Ctrl+N           save, open, new\n"
      "  Ctrl+F  Ctrl+H  F3               find, change, find next\n"
      "  Ctrl+G                           go to line\n"
      "  F2                               list of SUBs / FUNCTIONs\n"
      "  Shift+F2 / Ctrl+F2               next / previous procedure\n"
      "  F6                               switch to/from the Immediate window\n"
      "  Alt (tap) or Alt+letter          menus (holding Alt shows the shortcuts)\n\n"
      "  When you leave a line, keywords are capitalised, spacing is tidied and\n"
      "  the line is checked for syntax errors, like in QBasic.\n\n"
      "  Edit > Format Code reformats the selected lines, or the whole view (the\n"
      "  module or the SUB/FUNCTION shown) if nothing is selected, after asking:\n"
      "  statements joined with ':' go on separate lines, single-line IFs with\n"
      "  several statements become block IFs, and blocks are re-indented by the\n"
      "  tab size. Strings, comments, DATA, labels and line numbers are kept.\n"
      "  Ctrl+Z undoes it.\n\n"
      "  See also: <<Debugging>> <<Contents>>\n" },
    { "Debugging",
      "  Running and Debugging\n\n"
      "  Shift+F5   Start the program from the beginning\n"
      "  F5         Continue (or start)\n"
      "  F8         Step: execute the next statement (enters procedures)\n"
      "  F10        Procedure step: step over SUB/FUNCTION calls\n"
      "  F7         Run to the cursor line\n"
      "  F9         Toggle breakpoint on the current line\n"
      "  F4         Show the output screen\n"
      "  Ctrl+Break  stop a running program (also Ctrl+Alt+Esc, for keyboards\n"
      "             without Break, and Ctrl+Pause / Ctrl+Scroll Lock)\n\n"
      "  When a program is stopped, the editor shows the statement where it\n"
      "  stopped. F5 continues from there, F8/F10 step. During INPUT it stops\n"
      "  at the INPUT, which asks again when you continue. If you edit the\n"
      "  program, it has to be restarted.\n\n"
      "  While stopped you can inspect variables in the Immediate window\n"
      "  (F6), e.g. PRINT x, and use Debug > Set Next Statement.\n\n"
      "  See also: <<Editing Keys>> <<STOP>>\n" },
    { "Data Types",
      "  Data Types\n\n"
      "  Suffix  Type      Range\n"
      "    %     INTEGER   -32768 to 32767\n"
      "    &     LONG      -2147483648 to 2147483647\n"
      "    !     SINGLE    7 digits (default type)\n"
      "    #     DOUBLE    15-16 digits\n"
      "    $     STRING    up to 1 GB (QBasic: 32767); STRING * n fixed length\n\n"
      "  Use <<DEFINT>> or <<DIM>> AS to change types. <<TYPE>> defines records.\n" },
    { "Operators",
      "  Operators (highest precedence first)\n\n"
      "  ^              exponentiation\n"
      "  -              negation\n"
      "  * /            multiplication, floating-point division\n"
      "  \\              integer division\n"
      "  MOD            remainder\n"
      "  + -            addition, subtraction (+ also joins strings)\n"
      "  = <> < > <= >= relational (true = -1, false = 0)\n"
      "  NOT AND OR XOR EQV IMP   logical / bitwise\n" },
    { "Game Programming",
      "  Game Programming Extensions\n\n"
      "  QBLike emulates the hardware tricks used by DOS games:\n\n"
      "  * Direct VGA memory in SCREEN 13: DEF SEG = &HA000: POKE y * 320 + x, c\n"
      "  * Vertical sync: WHILE (INP(&H3DA) AND 8) = 0: WEND  (or WAIT &H3DA, 8)\n"
      "  * Palette ports: OUT &H3C8, index: OUT &H3C9, r: OUT &H3C9, g: OUT &H3C9, b\n"
      "  * Keyboard scan codes: INP(&H60)\n"
      "  * Several keys at once: <<MULTIKEY>>(scancode)\n"
      "  * Mouse: <<MOUSE>> x, y, buttons\n"
      "  * Transparent sprites: PUT (x, y), sprite(0), MASK   (see <<PUT>>)\n"
      "  * Clipped sprites: PUT never fails off-screen; area set with <<CLIP>>\n"
      "  * Page flipping: SCREEN 7, 0, 1, 0 ... <<PCOPY>> 1, 0  (SCREEN 13: 64 pages)\n"
      "  * Off-screen image buffers: PUT buf(0), (x, y), sprite(0), MASK, then\n"
      "    PUT (0, 0), buf(0), PSET   (see <<PUT>>)\n"
      "  * Array addresses: <<VARSEG>>/<<VARPTR>> with PEEK, POKE, BLOAD and\n"
      "    PUT (x, y), SEG s, o\n"
      "  * Loading images: <<LOADBMP>>, <<LOADGIF>> and <<LOADPAL>>\n"
      "  * Flood fill by color, any border: <<FILL>> (x, y), color\n"
      "  * Background music: <<MUSIC>> \"SONG.MOD\", LOOP   (MOD or MIDI)\n"
      "  * Sound effects: id = <<SOUND>> \"BOOM.WAV\": SOUND PLAY id\n"
      "  * AdLib FM ports: OUT &H388, reg: OUT &H389, value   (see <<OUT>>)\n"
      "  * DirectQB games: lib/directqb/DIRECTQB.BI implements the whole\n"
      "    DirectQB 1.61 library in QBLike BASIC: keep '$INCLUDE: 'DIRECTQB.BI'\n\n"
      "  See also: <<INP>> <<OUT>> <<DEF SEG>> <<GET>>\n" },
    { "Make EXE",
      "  Making Executables\n\n"
      "  File > Make EXE File creates a stand-alone program: a copy of the\n"
      "  QBLike run-time module (qbrun, like BRUN45.EXE of QuickBASIC) with your\n"
      "  program inside. It runs on computers without QBLike, starting in a\n"
      "  window or full screen as chosen, and closes when the program ends.\n\n"
      "  * The program is saved and checked first; it is stored obfuscated.\n"
      "  * Data files it uses (images, sounds, music...) are not included: copy\n"
      "    them next to the executable.\n"
      "  * qbrun (qbrun.exe on Windows) must be next to QBLike. With the runtime\n"
      "    of the other system (bin/windows or bin/linux) an executable for it\n"
      "    can be made too. On Linux the executable needs SDL2, like QBLike.\n"
      "  * qbrun alone does nothing.\n"
      "  * The arguments of the executable are in <<COMMAND$>>. -w or --window\n"
      "    and --fullscreen choose the start mode at run time.\n\n"
      "  From the command line:\n"
      "    qblike --make-exe GAME.BAS [GAME.EXE] [--fullscreen]\n"
      "                              [--target windows|linux]\n\n"
      "  See also: <<SCREEN>> <<Menus>>\n" },
    { "Menus",
      "  Menus\n\n"
      "  Press Alt to activate the menu bar, then use the arrows and Enter, or\n"
      "  press Alt plus the highlighted letter. You can also click with the mouse.\n\n"
      "  See also: <<Editing Keys>> <<Debugging>>\n" },
    { "Errors",
      "  Run-time Error Codes\n\n"
      "    1 NEXT without FOR                 37 Argument-count mismatch\n"
      "    2 Syntax error                     38 Array not defined\n"
      "    3 RETURN without GOSUB             40 Variable required\n"
      "    4 Out of DATA                      50 FIELD overflow\n"
      "    5 Illegal function call            51 Internal error\n"
      "    6 Overflow                         52 Bad file name or number\n"
      "    7 Out of memory                    53 File not found\n"
      "    8 Label not defined                54 Bad file mode\n"
      "    9 Subscript out of range           55 File already open\n"
      "   10 Duplicate definition             56 FIELD statement active\n"
      "   11 Division by zero                 57 Device I/O error\n"
      "   12 Illegal in direct mode           58 File already exists\n"
      "   13 Type mismatch                    59 Bad record length\n"
      "   14 Out of string space              61 Disk full\n"
      "   16 String formula too complex       62 Input past end of file\n"
      "   17 Cannot continue                  63 Bad record number\n"
      "   18 Function not defined             64 Bad file name\n"
      "   19 No RESUME                        67 Too many files\n"
      "   20 RESUME without error             68 Device unavailable\n"
      "   24 Device timeout                   69 Communication-buffer overflow\n"
      "   25 Device fault                     70 Permission denied\n"
      "   26 FOR without NEXT                 71 Disk not ready\n"
      "   27 Out of paper                     72 Disk-media error\n"
      "   28 Out of stack space               73 Advanced feature unavailable\n"
      "   29 WHILE without WEND               74 Rename across disks\n"
      "   30 WEND without WHILE               75 Path/File access error\n"
      "   33 Duplicate label                  76 Path not found\n"
      "   35 Subprogram not defined\n"
      "\n  ERR returns the code inside an ON ERROR handler; ERROR n raises one.\n\n"
      "  See also: <<ON ERROR>> <<ERR>>\n" },
    { NULL, NULL }
};

static char *expand_links(const char *s) {
    size_t n = strlen(s);
    char *o = (char *)qb_alloc(n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '<' && s[i + 1] == '<') { o[k++] = 0x11; i++; continue; }
        if (s[i] == '>' && s[i + 1] == '>') { o[k++] = 0x10; i++; continue; }
        o[k++] = s[i];
    }
    o[k] = 0;
    return o;
}

static char helpbuf[16384];

static const HelpEnt *find_ent(const char *name) {
    for (int i = 0; H[i].name; i++) if (str_ieq(H[i].name, name)) return &H[i];
    return NULL;
}

const char *help_lookup(const char *topic) {
    static char *last = NULL;
    free(last);
    last = NULL;
    for (int i = 0; extra_topics[i][0]; i++)
        if (str_ieq(extra_topics[i][0], topic)) { last = expand_links(extra_topics[i][1]); return last; }
    if (str_ieq(topic, "Index")) {
        char *o = helpbuf;
        o += sprintf(o, "  Keyword Index\n\n  ");
        int col = 0;
        for (int i = 0; H[i].name; i++) {
            int w = (int)strlen(H[i].name) + 4;
            if (col + w > 74) { o += sprintf(o, "\n  "); col = 0; }
            o += sprintf(o, "<<%s>>  ", H[i].name);
            col += w;
        }
        sprintf(o, "\n\n  See also: <<Contents>>\n");
        last = expand_links(helpbuf);
        return last;
    }
    const HelpEnt *e = find_ent(topic);
    if (!e) {
        for (int i = 0; aliases[i][0]; i++) if (str_ieq(aliases[i][0], topic)) { e = find_ent(aliases[i][1]); if (!e) return help_lookup(aliases[i][1]); break; }
    }
    if (!e && (topic[0] == 'F' || topic[0] == 'f') && (topic[1] == 'N' || topic[1] == 'n')) e = find_ent("DEF FN");
    if (!e) return NULL;
    char *o = helpbuf;
    o += sprintf(o, "  %s\n\n", e->name);
    /* indent syntax */
    o += sprintf(o, "  ");
    for (const char *s = e->syntax; *s; s++) { *o++ = *s; if (*s == '\n') { *o++ = ' '; *o++ = ' '; } }
    o += sprintf(o, "\n\n  ");
    for (const char *s = e->text; *s; s++) { *o++ = *s; if (*s == '\n') { *o++ = ' '; *o++ = ' '; } }
    o += sprintf(o, "\n\n  See also: ");
    if (e->see && e->see[0]) {
        char tmp[256];
        snprintf(tmp, sizeof tmp, "%s", e->see);
        /* multi-word topics: LINE INPUT, VIEW PRINT, DEF SEG etc are listed with spaces; split on known names */
        char *p = tmp;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            /* try two-word topic first */
            char *sp = strchr(p, ' ');
            if (sp) {
                char two[80];
                char *sp2 = strchr(sp + 1, ' ');
                int l2 = sp2 ? (int)(sp2 - p) : (int)strlen(p);
                if (l2 < 79) {
                    memcpy(two, p, (size_t)l2); two[l2] = 0;
                    if (find_ent(two)) { o += sprintf(o, "<<%s>> ", two); p += l2; continue; }
                }
                *sp = 0;
                o += sprintf(o, "<<%s>> ", p);
                p = sp + 1;
            } else { o += sprintf(o, "<<%s>> ", p); break; }
        }
    }
    o += sprintf(o, "<<Index>>\n");
    last = expand_links(helpbuf);
    return last;
}

const char *help_index_text(void) { return help_lookup("Index"); }


/* qblike --help-dump: raw help data for tools/help2md.py (which builds REFERENCE.md) */
void help_dump(void) {
    for (int i = 0; H[i].name; i++)
        printf("@@ENT\t%s\n@@SYN\n%s\n@@TXT\n%s\n@@SEE\t%s\n", H[i].name, H[i].syntax, H[i].text, H[i].see ? H[i].see : "");
    for (int i = 0; extra_topics[i][0]; i++) printf("@@TOPIC\t%s\n%s\n@@END\n", extra_topics[i][0], extra_topics[i][1]);
    for (int i = 0; aliases[i][0]; i++) printf("@@ALIAS\t%s\t%s\n", aliases[i][0], aliases[i][1]);
}
