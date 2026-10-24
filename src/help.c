/* help.c - built-in help text. Links are written as <<Topic>> and shown as ◄Topic►. */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct HelpEnt { const char *name; const char *syntax; const char *text; const char *see; } HelpEnt;

static const HelpEnt H[] = {
{ "ABS", "ABS(numeric-expression)", "Returns the absolute value of a number.", "SGN" },
{ "ASC", "ASC(stringexpression$)", "Returns the ASCII code of the first character of a string.", "CHR$" },
{ "ATN", "ATN(numeric-expression)", "Returns the arctangent (in radians) of a number.", "COS SIN TAN" },
{ "BEEP", "BEEP", "Sounds the speaker (800 Hz for 1/4 second).", "SOUND PLAY" },
{ "BLOAD", "BLOAD filespec$[, offset%]", "Loads a memory image saved with BSAVE. Supported for video memory\n(DEF SEG = &HA000 in SCREEN 13, &HB800 in SCREEN 0).", "BSAVE DEF SEG" },
{ "BSAVE", "BSAVE filespec$, offset%, length&", "Saves a block of memory (video memory) to a file.", "BLOAD DEF SEG" },
{ "CALL", "[CALL] name [([argumentlist])]", "Transfers control to a SUB procedure. Without CALL the parentheses\naround the argument list are omitted. Arguments are passed by\nreference unless enclosed in parentheses.", "SUB DECLARE" },
{ "CASE", "CASE expressionlist", "See SELECT CASE.", "SELECT" },
{ "CDBL", "CDBL(numeric-expression)", "Converts a numeric expression to double precision.", "CINT CLNG CSNG" },
{ "CHDIR", "CHDIR pathspec$", "Changes the current directory.", "MKDIR RMDIR FILES" },
{ "CHR$", "CHR$(code%)", "Returns the character with the given ASCII code (0-255).", "ASC" },
{ "CINT", "CINT(numeric-expression)", "Rounds to the nearest integer (halves round to even).\nRange -32768 to 32767.", "CLNG FIX INT" },
{ "CIRCLE", "CIRCLE [STEP] (x!,y!),radius![,[color%] [,[start!] [,[end!] [,aspect!]]]]", "Draws a circle, ellipse or arc. Start and end angles are in radians;\nnegative angles also draw a radius line.", "LINE PSET PAINT" },
{ "CLEAR", "CLEAR [,,stack&]", "Closes all files and resets all variables.", "ERASE" },
{ "CLIP", "CLIP x1%, y1%, x2%, y2%\nCLIP", "QBLike extension. Sets the clipping rectangle used by PUT (and by\nLOADBMP/LOADGIF ..., SCREEN), in absolute screen coordinates: the origin\nis not moved, unlike VIEW. CLIP alone, or SCREEN with a mode, resets it to\nthe whole screen.\n\nPUT never fails for being off-screen: only the part of the image inside\nthe rectangle is drawn, so sprites can leave the screen on any side.\n\n  CLIP 0, 20, 319, 199     ' keep the top 20 lines (score bar) intact\n  PUT (x%, y%), sprite%(0), MASK", "PUT VIEW" },
{ "CLNG", "CLNG(numeric-expression)", "Rounds to the nearest long integer.", "CINT" },
{ "CLOSE", "CLOSE [[#]filenumber%[,[#]filenumber%]...]", "Closes one or more open files. CLOSE alone closes all files.", "OPEN RESET" },
{ "CLS", "CLS [{0 | 1 | 2}]", "Clears the screen. 0 = whole screen, 1 = graphics viewport,\n2 = text viewport.", "VIEW VIEW PRINT" },
{ "COLOR", "SCREEN 0:       COLOR [foreground%] [,[background%] [,border%]]\nSCREEN 7-10:    COLOR [foreground%] [,background%]\nSCREEN 12-13:   COLOR [foreground%]",
  "Sets screen display colors. In text mode foreground 0-31 (16-31 blink)\nand background 0-7.\n\n 0 Black    4 Red       8 Gray         12 Light Red\n 1 Blue     5 Magenta   9 Light Blue   13 Light Magenta\n 2 Green    6 Brown    10 Light Green  14 Yellow\n 3 Cyan     7 White    11 Light Cyan   15 Bright White", "PALETTE SCREEN" },
{ "COMMON", "COMMON [SHARED] variablelist", "Treated as DIM SHARED (programs are not chained).", "DIM SHARED" },
{ "CONST", "CONST constantname = expression [,constantname = expression]...", "Declares symbolic constants.", "DIM" },
{ "COS", "COS(angle)", "Returns the cosine of an angle given in radians.", "SIN TAN ATN" },
{ "CSNG", "CSNG(numeric-expression)", "Converts a numeric expression to single precision.", "CDBL" },
{ "CSRLIN", "CSRLIN", "Returns the current row of the cursor.", "POS LOCATE" },
{ "CVI", "CVI(2-byte-string)  CVL(4-byte-string)  CVS(4-byte-string)  CVD(8-byte-string)", "Convert strings created by MKI$, MKL$, MKS$ and MKD$ back to numbers.", "MKI$" },
{ "CVL", "CVL(4-byte-string)", "See CVI.", "CVI MKL$" },
{ "CVS", "CVS(4-byte-string)", "See CVI.", "CVI MKS$" },
{ "CVD", "CVD(8-byte-string)", "See CVI.", "CVI MKD$" },
{ "DATA", "DATA constant[,constant]...", "Stores numeric and string constants read by READ.", "READ RESTORE" },
{ "DATE$", "DATE$", "Returns the current date as mm-dd-yyyy.", "TIME$ TIMER" },
{ "DECLARE", "DECLARE {SUB | FUNCTION} name [([parameterlist])]", "Declares a procedure. QBLike adds DECLARE lines automatically when\nsaving; they are not required to run.", "SUB FUNCTION" },
{ "DEF FN", "DEF FNname[(parameterlist)] = expression\n\nDEF FNname[(parameterlist)]\n    [statementblock]\n    FNname = expression\nEND DEF", "Defines a function. FUNCTION procedures are preferred.", "FUNCTION" },
{ "DEF SEG", "DEF SEG [=address]", "Sets the segment used by PEEK, POKE, BLOAD and BSAVE.\nSupported segments: &HA000 (SCREEN 13 pixels, 1 byte each, offset = y*320+x),\n&HB800 (text screen: character/attribute pairs) and 0 (BIOS: PEEK(&H417)\nkeyboard flags, PEEK(&H46C) timer ticks).", "PEEK POKE" },
{ "DEFINT", "DEFINT letterrange [,letterrange]...\nDEFLNG / DEFSNG / DEFDBL / DEFSTR", "Sets the default data type for variables whose names begin with the\nspecified letters. Example: DEFINT A-Z", "DIM" },
{ "DIM", "DIM [SHARED] variable[(subscripts)] [AS type] [,variable[(subscripts)] [AS type]]...\n\nsubscripts: [lower% TO] upper% [,[lower% TO] upper%]...\ntype: INTEGER, LONG, SINGLE, DOUBLE, STRING, STRING * n, or a TYPE name",
  "Declares a variable or array.", "REDIM ERASE TYPE SHARED OPTION BASE" },
{ "DO", "DO [{WHILE | UNTIL} condition]\n    [statementblock]\nLOOP\n\nDO\n    [statementblock]\nLOOP [{WHILE | UNTIL} condition]", "Repeats a block of statements while a condition is true or until it\nbecomes true. EXIT DO leaves the loop.", "WHILE FOR EXIT" },
{ "DRAW", "DRAW commandstring$", "Draws an object.\n\n U/D/L/R [n]  up/down/left/right    E/F/G/H [n]  diagonals\n M [+|-]x,y   move to / by          B  prefix: move without drawing\n N  prefix: return after drawing    A n  angle (0-3)   TA n  turn angle\n C n  color   S n  scale (default 4) P paint,border", "LINE PSET" },
{ "END", "END [{DEF | FUNCTION | IF | SELECT | SUB | TYPE}]", "Ends a program, procedure, block or user-defined type.", "STOP SYSTEM" },
{ "ENVIRON$", "ENVIRON$(env-variable$)", "Returns an environment variable.", "ENVIRON" },
{ "EOF", "EOF(filenumber%)", "Returns true (-1) when the end of a file has been reached.", "LOF LOC OPEN" },
{ "ERASE", "ERASE arrayname [,arrayname]...", "Reinitializes static arrays and deallocates dynamic arrays.", "DIM REDIM" },
{ "ERR", "ERR\nERL", "ERR returns the code of the last error, ERL the nearest line number\nbefore the statement that caused it.", "ON ERROR ERROR Errors" },
{ "ERROR", "ERROR expression%", "Simulates the occurrence of an error.", "ON ERROR ERR" },
{ "EXIT", "EXIT {DEF | DO | FOR | FUNCTION | SUB}", "Exits a loop or procedure.", "DO FOR SUB FUNCTION" },
{ "EXP", "EXP(numeric-expression)", "Returns e raised to the given power.", "LOG" },
{ "FILES", "FILES [filespec$]", "Displays the files in the current directory.", "CHDIR KILL" },
{ "FIX", "FIX(numeric-expression)", "Truncates toward zero.", "INT CINT" },
{ "FOR", "FOR counter = start TO end [STEP increment]\n    [statementblock]\nNEXT [counter [,counter]...]", "Repeats a block a specified number of times.", "DO EXIT" },
{ "FREEFILE", "FREEFILE", "Returns the next free file number.", "OPEN" },
{ "FUNCTION", "FUNCTION name[(parameterlist)] [STATIC]\n    [statementblock]\n    name = expression\nEND FUNCTION", "Defines a FUNCTION procedure. Type FUNCTION name in the editor (or use\nEdit > New FUNCTION) and it gets its own window; press F2 to switch.", "SUB DECLARE EXIT" },
{ "GET", "GET [#]filenumber%[,[record&][,variable]]        (files)\nGET [STEP](x1!,y1!)-[STEP](x2!,y2!), arrayname[(index%)]   (graphics)", "Reads from a random/binary file, or stores a screen image in an array\nfor PUT.", "PUT OPEN" },
{ "GOSUB", "GOSUB line\n...\nRETURN [line]", "Branches to and returns from a subroutine.", "RETURN ON GOSUB" },
{ "GOTO", "GOTO line", "Branches to a line label or number.", "GOSUB" },
{ "HEX$", "HEX$(expression&)", "Returns the hexadecimal representation of a number.", "OCT$" },
{ "IF", "IF condition THEN thenpart [ELSE elsepart]\n\nIF condition1 THEN\n    [statementblock-1]\n[ELSEIF condition2 THEN\n    [statementblock-2]]...\n[ELSE\n    [statementblock-n]]\nEND IF", "Executes statements depending on conditions.", "SELECT" },
{ "INKEY$", "INKEY$", "Reads a character from the keyboard without waiting. Extended keys\nreturn CHR$(0) + scan code (e.g. up arrow = CHR$(0) + \"H\").", "INPUT$ MULTIKEY INP" },
{ "INP", "INP(port%)", "Emulated ports:\n  &H60   last keyboard scan code (bit 7 set on release)\n  &H3DA  VGA status: bit 3 = vertical retrace (70 Hz)\n  &H3C9  palette data read (after OUT &H3C7, index)\n\nVSYNC wait:\n  WHILE (INP(&H3DA) AND 8) <> 0: WEND\n  WHILE (INP(&H3DA) AND 8) = 0: WEND", "OUT WAIT MULTIKEY" },
{ "INPUT", "INPUT [;] [\"prompt\"{; | ,}] variablelist\nINPUT #filenumber%, variablelist", "Reads input from the keyboard or a file. With ; after the prompt a\nquestion mark is shown; with , it is not.", "LINE INPUT INPUT$" },
{ "INPUT$", "INPUT$(n[,[#]filenumber%])", "Returns n characters read from the keyboard or a file.", "INKEY$" },
{ "INSTR", "INSTR([start%,]stringexpression1$,stringexpression2$)", "Returns the position of the first occurrence of a string in another.", "MID$" },
{ "INT", "INT(numeric-expression)", "Returns the largest integer less than or equal to a number.", "FIX CINT" },
{ "KILL", "KILL filespec$", "Deletes a file.", "FILES NAME" },
{ "LBOUND", "LBOUND(array[,dimension%])\nUBOUND(array[,dimension%])", "Return the lower and upper bounds of an array dimension.", "DIM" },
{ "LCASE$", "LCASE$(stringexpression$)\nUCASE$(stringexpression$)", "Convert a string to lowercase / uppercase.", "" },
{ "LEFT$", "LEFT$(stringexpression$,n%)\nRIGHT$(stringexpression$,n%)", "Return the leftmost / rightmost n characters of a string.", "MID$ LEN" },
{ "LEN", "LEN(stringexpression$)\nLEN(variable)", "Returns the number of characters in a string or bytes in a variable.", "" },
{ "LET", "[LET] variable=expression", "Assigns a value to a variable.", "" },
{ "LINE", "LINE [[STEP](x1!,y1!)]-[STEP](x2!,y2!) [,[color%] [,[B | BF] [,style%]]]", "Draws a line or a box (B) or a filled box (BF).", "CIRCLE PSET LINE INPUT" },
{ "LINE INPUT", "LINE INPUT [;] [\"prompt\";] variable$\nLINE INPUT #filenumber%, variable$", "Reads a whole line from the keyboard or a file.", "INPUT" },
{ "LOADBMP", "LOADBMP filespec$, arrayname[(index%)]\nLOADBMP filespec$, SCREEN, x!, y! [, PALETTE]",
  "QBLike extension. Loads an indexed BMP (2, 16 or 256 colors; uncompressed,\nRLE4 or RLE8).\n\nArray form: stores the image in GET/PUT format for the current screen\nmode, so PUT (x, y), arrayname(index) draws it. The array must be large\nenough (Illegal function call otherwise, like GET).\n\nSCREEN form: draws the image at (x, y). The image must fit the current\nmode's colors (256 colors needs SCREEN 13, 16 colors a 16-color mode).\nPALETTE also sets the screen palette from the file.\n\n  SCREEN 13\n  LOADBMP \"fondo.bmp\", SCREEN, 0, 0, PALETTE\n  DIM nave%(130): LOADBMP \"nave.bmp\", nave%(0)\n  PUT (100, 100), nave%(0), MASK", "LOADGIF LOADPAL PUT GET" },
{ "LOADGIF", "LOADGIF filespec$, arrayname[(index%)]\nLOADGIF filespec$, SCREEN, x!, y! [, PALETTE]",
  "QBLike extension. Like LOADBMP but for GIF files.\n\nAnimated GIFs: every frame is composed on the GIF's logical screen and\nstored in the array one after another, each frame taking\n  (4 + bytes of pixels + elementsize - 1) \\ elementsize  elements.\nIn SCREEN 13 with an INTEGER array: (4 + w * h + 1) \\ 2 per frame.\nThe SCREEN form draws the first frame.", "LOADBMP LOADPAL PUT" },
{ "LOADPAL", "LOADPAL filespec$, arrayname&[(index%)]",
  "QBLike extension. Loads only the palette of a BMP or GIF file into a LONG\narray, one element per color, in PALETTE USING format\n(65536 * blue + 256 * green + red, each 0-63).\n\n  DIM pal&(255)\n  LOADPAL \"fondo.gif\", pal&(0)\n  PALETTE USING pal&(0)", "PALETTE LOADBMP LOADGIF" },
{ "LOC", "LOC(filenumber%)", "Returns the current position within a file.", "SEEK EOF" },
{ "LOCATE", "LOCATE [row%] [,[column%] [,[cursor%] [,start% [,stop%]]]]", "Moves the cursor. cursor% = 1 shows it, 0 hides it.", "CSRLIN POS" },
{ "LOF", "LOF(filenumber%)", "Returns the length of a file in bytes.", "EOF" },
{ "LOG", "LOG(numeric-expression)", "Returns the natural logarithm.", "EXP" },
{ "LTRIM$", "LTRIM$(stringexpression$)\nRTRIM$(stringexpression$)", "Remove leading / trailing spaces.", "" },
{ "MID$", "MID$(stringexpression$,start%[,length%])        (function)\nMID$(stringvariable$,start%[,length%])=expression$   (statement)", "Returns or replaces part of a string.", "LEFT$ INSTR" },
{ "MKI$", "MKI$(integer%)  MKL$(long&)  MKS$(single!)  MKD$(double#)", "Convert numbers to binary strings (for files).", "CVI" },
{ "MKDIR", "MKDIR pathspec$", "Creates a directory.", "CHDIR RMDIR" },
{ "MOD", "numeric-expression1 MOD numeric-expression2", "Integer remainder of a division (operands are rounded first).", "Operators" },
{ "MULTIKEY", "MULTIKEY(scancode%)", "QBLike extension. Returns -1 while the key with the given scan code is\nheld down, 0 otherwise. Several keys can be checked at once, ideal for\ngames.\n\nCommon scan codes: Esc 1, Enter 28, Space 57, Ctrl 29, Alt 56,\nLeft Shift 42, Up 72, Down 80, Left 75, Right 77, A 30, D 32, S 31, W 17.", "INKEY$ INP" },
{ "NAME", "NAME oldspec$ AS newspec$", "Renames a file.", "KILL" },
{ "OCT$", "OCT$(numeric-expression&)", "Returns the octal representation of a number.", "HEX$" },
{ "ON ERROR", "ON ERROR GOTO line\nON ERROR GOTO 0\n...\nRESUME [NEXT | line]", "Enables error handling.", "RESUME ERR ERROR" },
{ "ON GOSUB", "ON expression% GOSUB line-list\nON expression% GOTO line-list", "Branches to one of several labels depending on a value.", "GOSUB GOTO" },
{ "ON TIMER", "ON TIMER(n%) GOSUB line\nTIMER ON | OFF | STOP", "Calls a subroutine every n seconds while TIMER ON is active.", "TIMER" },
{ "OPEN", "OPEN file$ [FOR mode] [ACCESS access] [lock] AS [#]filenumber% [LEN=reclen%]\n\nmode: APPEND, BINARY, INPUT, OUTPUT, RANDOM\n\nOPEN mode$, [#]filenumber%, file$[, reclen%]", "Opens a file. DOS-style names (C:\\DIR\\FILE.TXT) are accepted;\non Linux the name is matched case-insensitively.", "CLOSE GET PUT INPUT PRINT" },
{ "OPTION BASE", "OPTION BASE {0 | 1}", "Sets the default lower bound for arrays.", "DIM" },
{ "OUT", "OUT port%, data%", "Emulated ports:\n  &H3C8  select palette index to write\n  &H3C9  write R, G, B (0-63) successively\n  &H3C7  select palette index to read\n\n  OUT &H3C8, 0: OUT &H3C9, 10: OUT &H3C9, 20: OUT &H3C9, 50", "INP PALETTE" },
{ "PAINT", "PAINT [STEP](x!,y!)[,[{color% | tile$}] [,[bordercolor%] [,background$]]]", "Fills an area bounded by the border color.", "CIRCLE LINE DRAW" },
{ "PALETTE", "PALETTE [attribute%,color&]\nPALETTE USING arrayname#[(index%)]", "Changes colors in the palette. In SCREEN 12 and 13 color& is\n65536 * blue + 256 * green + red (each 0-63).", "COLOR OUT SCREEN" },
{ "PCOPY", "PCOPY sourcepage%, destinationpage%", "Copies one video page to another (SCREEN 0, 7, 8, 9).", "SCREEN" },
{ "PEEK", "PEEK(address)", "Returns a byte from memory at the DEF SEG segment. See DEF SEG.", "POKE DEF SEG" },
{ "PLAY", "PLAY commandstring$", "Plays music.\n\n A-G [#|+|-] [n]  notes        O n  octave (0-6)   < >  octave down/up\n N n  note 0-84   L n  length    T n  tempo (32-255)  P n  pause\n MN ML MS  normal/legato/staccato   MF MB  foreground/background\n .  dotted note", "SOUND BEEP" },
{ "PMAP", "PMAP(startcoordinate#, n%)", "Maps between logical (WINDOW) and physical coordinates.\nn: 0 = x to physical, 1 = y to physical, 2 = x to logical, 3 = y to logical.", "WINDOW VIEW" },
{ "POINT", "POINT(x%,y%)\nPOINT(number%)", "Returns a pixel color, or the graphics cursor position\n(0 = physical x, 1 = physical y, 2 = logical x, 3 = logical y).", "PSET" },
{ "POKE", "POKE address, byte%", "Writes a byte to memory at the DEF SEG segment.\nExample (SCREEN 13): DEF SEG = &HA000: POKE y * 320 + x, color", "PEEK DEF SEG" },
{ "POS", "POS(0)", "Returns the current column of the cursor.", "CSRLIN LOCATE" },
{ "PRINT", "PRINT [#filenumber%,] [expressionlist] [{; | ,}]", "Writes data to the screen or a file. ; keeps the cursor after the last\nitem, , moves to the next 14-column zone. Positive numbers are printed\nwith a leading space and all numbers with a trailing space.\n? is an abbreviation for PRINT.", "PRINT USING WRITE LPRINT" },
{ "PRINT USING", "PRINT [#filenumber%,] USING formatstring$; expressionlist [{; | ,}]", "Prints formatted output.\n\n #  digit      .  decimal point    ,  thousands      +  sign\n -  trailing minus   $$  floating $    **  * fill     ^^^^  exponent\n !  first char  \\  \\  n chars       &  whole string   _  literal", "PRINT" },
{ "PSET", "PSET [STEP](x!,y!) [,color%]\nPRESET [STEP](x!,y!) [,color%]", "Draws a point. PRESET uses the background color by default.", "POINT LINE" },
{ "PUT", "PUT [#]filenumber%[,[record&][,variable]]        (files)\nPUT [STEP](x!,y!), arrayname[(index%)] [,actionverb]   (graphics)\n\nactionverb: PSET, PRESET, AND, OR, XOR (default) or MASK", "Writes to a file, or draws an image captured with GET.\nMASK (QBLike extension) skips pixels of color 0, so sprites can have\ntransparent backgrounds: PUT (x%, y%), sprite%(0), MASK\nIn QBLike PUT is clipped to the CLIP rectangle instead of failing when\nthe image is partly or totally off-screen.", "GET CLIP" },
{ "RANDOMIZE", "RANDOMIZE [seed%]\nRANDOMIZE TIMER", "Initializes the random-number generator.", "RND" },
{ "READ", "READ variablelist", "Reads values from DATA statements.", "DATA RESTORE" },
{ "REDIM", "REDIM [PRESERVE] [SHARED] variable(subscripts) [AS type] [,...]", "Changes the size of a dynamic array.", "DIM ERASE" },
{ "REM", "REM remark\n' remark", "Allows comments in a program.", "" },
{ "RESTORE", "RESTORE [line]", "Allows DATA to be reread from the start or from a label.", "DATA READ" },
{ "RESUME", "RESUME [0]\nRESUME NEXT\nRESUME line", "Continues after an error handler.", "ON ERROR" },
{ "RETURN", "RETURN [line]", "Returns from a GOSUB subroutine.", "GOSUB" },
{ "RIGHT$", "RIGHT$(stringexpression$,n%)", "See LEFT$.", "LEFT$" },
{ "RMDIR", "RMDIR pathspec$", "Removes a directory.", "MKDIR" },
{ "RND", "RND[(n#)]", "Returns a single-precision random number between 0 and 1.\nThe sequence is the same as in QBasic for the same seed.", "RANDOMIZE" },
{ "RUN", "RUN [line]", "Restarts the program.", "END" },
{ "SCREEN", "SCREEN mode% [,[colorswitch%] [,[activepage%] [,visualpage%]]]\n\nSCREEN(row%, column% [,colorflag%])   (function)",
  "Sets the screen mode.\n\n  0  text 80x25 (40x25, 80x43, 80x50 with WIDTH), 16 colors, 8 pages\n  1  320x200, 4 colors          2  640x200, 2 colors\n  7  320x200, 16 colors, 8 pages\n  8  640x200, 16 colors, 4 pages\n  9  640x350, 16 colors, 2 pages\n 11  640x480, 2 colors          12  640x480, 16 colors\n 13  320x200, 256 colors\n\nThe function SCREEN(row, col) returns the character code at a position.", "COLOR PALETTE PCOPY WIDTH" },
{ "SEEK", "SEEK [#]filenumber%, position&\nSEEK(filenumber%)", "Sets / returns the file position.", "LOC" },
{ "SELECT", "SELECT CASE testexpression\nCASE expressionlist1\n    [statementblock-1]\n[CASE expressionlist2\n    [statementblock-2]]...\n[CASE ELSE\n    [statementblock-n]]\nEND SELECT\n\nexpressionlist: expression, expression TO expression, IS relop expression", "Executes one of several blocks depending on a value.", "IF" },
{ "SGN", "SGN(numeric-expression)", "Returns -1, 0 or 1 depending on the sign.", "ABS" },
{ "SHARED", "SHARED variable[()] [AS type] [,variable[()] [AS type]]...", "Gives a procedure access to module-level variables.", "DIM STATIC" },
{ "SHELL", "SHELL [commandstring$]", "Runs an operating system command.", "" },
{ "SIN", "SIN(angle)", "Returns the sine of an angle given in radians.", "COS TAN ATN" },
{ "SLEEP", "SLEEP [seconds&]", "Suspends execution until a key is pressed or the time elapses.", "INKEY$" },
{ "SOUND", "SOUND frequency, duration", "Generates a tone. Duration is in clock ticks (18.2 per second).", "PLAY BEEP" },
{ "SPACE$", "SPACE$(n%)", "Returns a string of n spaces.", "STRING$ SPC" },
{ "SPC", "SPC(n%)", "Skips n spaces in a PRINT statement.", "TAB PRINT" },
{ "SQR", "SQR(numeric-expression)", "Returns the square root.", "" },
{ "STATIC", "STATIC variablelist", "Makes procedure variables keep their values between calls.", "SHARED SUB" },
{ "STOP", "STOP", "Halts the program and returns to the editor (like a breakpoint).", "END" },
{ "STR$", "STR$(numeric-expression)", "Returns a string representation of a number (with a leading space\nfor positive numbers).", "VAL" },
{ "STRING$", "STRING$(length%,{ascii-code% | stringexpression$})", "Returns a string of repeated characters.", "SPACE$" },
{ "SUB", "SUB name[(parameterlist)] [STATIC]\n    [statementblock]\nEND SUB", "Defines a SUB procedure. Type SUB name in the editor (or use\nEdit > New SUB) and it gets its own window; press F2 to switch.", "CALL FUNCTION SHARED STATIC" },
{ "SWAP", "SWAP variable1, variable2", "Exchanges the values of two variables of the same type.", "" },
{ "SYSTEM", "SYSTEM", "Ends the program (and closes QBLike when run with /X).", "END" },
{ "TAB", "TAB(column%)", "Moves to a column in a PRINT statement.", "SPC PRINT" },
{ "TAN", "TAN(angle)", "Returns the tangent of an angle given in radians.", "SIN COS ATN" },
{ "TIME$", "TIME$", "Returns the current time as hh:mm:ss.", "DATE$ TIMER" },
{ "TIMER", "TIMER\nTIMER ON | OFF | STOP", "Returns the number of seconds since midnight; also controls ON TIMER.", "ON TIMER TIME$" },
{ "TRON", "TRON\nTROFF", "Accepted for compatibility. Use Debug > Trace On in the IDE.", "" },
{ "TYPE", "TYPE usertype\n    elementname AS typename\n    ...\nEND TYPE", "Defines a record data type. Elements can be INTEGER, LONG, SINGLE,\nDOUBLE, STRING * n or another TYPE.", "DIM" },
{ "UBOUND", "UBOUND(array[,dimension%])", "See LBOUND.", "LBOUND" },
{ "UCASE$", "UCASE$(stringexpression$)", "See LCASE$.", "LCASE$" },
{ "VAL", "VAL(stringexpression$)", "Converts a string to a number. Blanks are ignored; &H and &O prefixes\nare accepted.", "STR$" },
{ "VIEW", "VIEW [[SCREEN] (x1!,y1!)-(x2!,y2!) [,[color%] [,border%]]]", "Defines a graphics viewport.", "WINDOW VIEW PRINT CLS" },
{ "VIEW PRINT", "VIEW PRINT [toprow% TO bottomrow%]", "Sets the text scrolling area.", "VIEW CLS" },
{ "WAIT", "WAIT &H3DA, 8", "Waits for the start of the vertical retrace (about 70 times per second).\nOther ports are ignored.", "INP" },
{ "WHILE", "WHILE condition\n    [statementblock]\nWEND", "Repeats while a condition is true.", "DO" },
{ "WIDTH", "WIDTH [columns%] [,rows%]", "Changes the text screen size (40/80 columns; 25, 43 or 50 rows in\nSCREEN 0; 30 or 60 rows in SCREEN 12).", "SCREEN" },
{ "WINDOW", "WINDOW [[SCREEN] (x1!,y1!)-(x2!,y2!)]", "Defines logical coordinates for graphics. Without SCREEN, y grows\nupwards.", "VIEW PMAP" },
{ "WRITE", "WRITE [#filenumber%,] expressionlist", "Writes data separated by commas, strings in quotes.", "PRINT INPUT" },
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
      "                  <<LOADBMP>> <<LOADGIF>> <<LOADPAL>> <<CLIP>>\n"
      "  Keyboard:       <<INKEY$>> <<MULTIKEY>> <<INPUT$>> <<SLEEP>>\n"
      "  Sound:          <<BEEP>> <<SOUND>> <<PLAY>>\n"
      "  Strings:        <<LEFT$>> <<MID$>> <<INSTR>> <<LEN>> <<STR$>> <<VAL>> <<CHR$>> <<ASC>>\n"
      "  Math:           <<ABS>> <<INT>> <<FIX>> <<SQR>> <<SIN>> <<RND>> <<RANDOMIZE>>\n"
      "  Files:          <<OPEN>> <<CLOSE>> <<GET>> <<PUT>> <<EOF>> <<KILL>> <<FILES>>\n"
      "  Hardware:       <<DEF SEG>> <<PEEK>> <<POKE>> <<INP>> <<OUT>> <<WAIT>>\n" },
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
      "  Ctrl+C  Ctrl+X  Ctrl+V           copy, cut, paste (system clipboard)\n"
      "  Ctrl+Ins  Shift+Del  Shift+Ins   copy, cut, paste (classic keys)\n"
      "  Ctrl+Z  Ctrl+Y                   undo, redo\n"
      "  Ctrl+Backspace  Ctrl+Del         delete word\n"
      "  Tab / Shift+Tab                  indent / unindent (also selected lines)\n"
      "  Insert                           toggle overwrite mode\n"
      "  Ctrl+S  Ctrl+O  Ctrl+N           save, open, new\n"
      "  Ctrl+F  Ctrl+H  F3               find, change, find next\n"
      "  Ctrl+G                           go to line\n"
      "  F2                               list of SUBs / FUNCTIONs\n"
      "  Shift+F2 / Ctrl+F2               next / previous procedure\n"
      "  F6                               switch to/from the Immediate window\n"
      "  Alt (tap) or Alt+letter          menus\n\n"
      "  When you leave a line, keywords are capitalised, spacing is tidied and\n"
      "  the line is checked for syntax errors, like in QBasic.\n\n"
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
      "  Ctrl+Break (or Ctrl+Pause, Ctrl+Scroll Lock)  stop a running program\n\n"
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
      "    $     STRING    up to 32767 characters; STRING * n fixed length\n\n"
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
      "  * Transparent sprites: PUT (x, y), sprite(0), MASK   (see <<PUT>>)\n"
      "  * Clipped sprites: PUT never fails off-screen; area set with <<CLIP>>\n"
      "  * Page flipping: SCREEN 7, 0, 1, 0 ... <<PCOPY>> 1, 0\n"
      "  * Loading images: <<LOADBMP>>, <<LOADGIF>> and <<LOADPAL>>\n\n"
      "  See also: <<INP>> <<OUT>> <<DEF SEG>> <<GET>>\n" },
    { "Menus",
      "  Menus\n\n"
      "  Press Alt to activate the menu bar, then use the arrows and Enter, or\n"
      "  press Alt plus the highlighted letter. You can also click with the mouse.\n\n"
      "  See also: <<Editing Keys>> <<Debugging>>\n" },
    { "Errors",
      "  Run-time Error Codes\n\n"
      "   1 NEXT without FOR          2 Syntax error            3 RETURN without GOSUB\n"
      "   4 Out of DATA               5 Illegal function call   6 Overflow\n"
      "   7 Out of memory             8 Label not defined       9 Subscript out of range\n"
      "  10 Duplicate definition     11 Division by zero       13 Type mismatch\n"
      "  14 Out of string space      20 RESUME without error   35 Subprogram not defined\n"
      "  37 Argument-count mismatch  52 Bad file name or number\n"
      "  53 File not found           54 Bad file mode          55 File already open\n"
      "  58 File already exists      62 Input past end of file 64 Bad file name\n"
      "  73 Advanced feature unavailable                        75 Path/File access error\n"
      "  76 Path not found\n\n"
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
