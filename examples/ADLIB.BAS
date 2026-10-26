' ADLIB.BAS - FM music through the emulated AdLib ports (&H388/&H389)
' Detects the card like DOS programs did, sets up an instrument and plays a tune.
DECLARE SUB FMwrite (reg%, value%)
DECLARE FUNCTION FMdetect% ()
DECLARE SUB NoteOn (ch%, note%)
DECLARE SUB NoteOff (ch%)

CLS
IF NOT FMdetect% THEN PRINT "AdLib not found.": END
PRINT "AdLib found. Playing... (any key stops)"

FOR r% = 1 TO &HF5: FMwrite r%, 0: NEXT
FMwrite 1, &H20                          ' allow waveform select
FOR ch% = 0 TO 2                         ' same instrument on channels 0-2
    m% = ch%                             ' modulator operator offset (carrier = m% + 3)
    FMwrite &H20 + m%, &H1: FMwrite &H23 + m%, &H1    ' multiplier
    FMwrite &H40 + m%, &H1A: FMwrite &H43 + m%, &H0   ' levels
    FMwrite &H60 + m%, &HF2: FMwrite &H63 + m%, &HF3  ' attack / decay
    FMwrite &H80 + m%, &H54: FMwrite &H83 + m%, &H35  ' sustain / release
    FMwrite &HC0 + ch%, &H6               ' feedback, FM
NEXT

RESTORE Tune
DO
    READ n%, b%, d!
    IF d! = 0 THEN EXIT DO
    IF n% THEN NoteOn 0, n%
    IF b% THEN NoteOn 1, b%
    t! = TIMER: DO: k$ = INKEY$: LOOP WHILE TIMER < t! + d! AND k$ = ""
    NoteOff 0
    IF k$ <> "" THEN EXIT DO
LOOP
NoteOff 1
IF k$ = "" THEN
    NoteOn 0, 48: NoteOn 1, 36: NoteOn 2, 52
    t! = TIMER: DO: LOOP WHILE TIMER < t! + 1.5
END IF
NoteOff 0: NoteOff 1: NoteOff 2
END

' note (semitones from C0, 0 = none), bass note (0 = keep), seconds
Tune:
DATA 48,36,.25, 52,0,.25, 55,43,.25, 60,0,.25, 59,43,.25, 55,0,.25, 57,41,.5
DATA 53,41,.25, 57,0,.25, 60,48,.25, 65,0,.25, 64,43,.25, 60,0,.25, 62,43,.5
DATA 0,0,0

FUNCTION FMdetect%
    FMwrite 4, &H60: FMwrite 4, &H80      ' reset both timers
    s1% = INP(&H388)
    FMwrite 2, &HFF: FMwrite 4, &H21      ' start timer 1
    FOR i% = 1 TO 200: x% = INP(&H388): NEXT
    s2% = INP(&H388)
    FMwrite 4, &H60: FMwrite 4, &H80
    FMdetect% = ((s1% AND &HE0) = 0) AND ((s2% AND &HE0) = &HC0)
END FUNCTION

SUB FMwrite (reg%, value%)
    OUT &H388, reg%
    FOR i% = 1 TO 6: x% = INP(&H388): NEXT
    OUT &H389, value%
    FOR i% = 1 TO 35: x% = INP(&H388): NEXT
END SUB

SUB NoteOff (ch%)
    FMwrite &HB0 + ch%, 0
END SUB

SUB NoteOn (ch%, note%)
    freq! = 440 * 2 ^ ((note% - 57) / 12)     ' note 57 = A4
    block% = 0
    DO
        fval& = freq! * 2 ^ (20 - block%) / 49716
        IF fval& < 1024 THEN EXIT DO
        block% = block% + 1
    LOOP
    FMwrite &HB0 + ch%, 0
    FMwrite &HA0 + ch%, fval& AND &HFF
    FMwrite &HB0 + ch%, &H20 OR (block% * 4) OR (fval& \ 256)
END SUB
