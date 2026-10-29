#!/usr/bin/env python3
"""mak2bas.py - joins a QuickBASIC 4.5 multi-module program into one QBLike program.

QuickBASIC programs made of several modules list them in a .MAK file (the first one is the main
module). QBLike, like QBasic, runs one program file, so this joins them:

    python3 mak2bas.py GAME.MAK GAMEQB.BAS [MODULE.BAS ...]

- The main module keeps its module-level code and its $INCLUDE lines.
- The module-level lines of the other modules that matter in one program (TYPE blocks, DATA
  with their labels, DIM SHARED...) are kept too; their $INCLUDEs, DECLAREs and $DYNAMIC/$STATIC
  are dropped, since the main module already has them.
- Every SUB and FUNCTION is copied with the DEFtype its module had at that point (QuickBASIC
  applies the DEFtypes of each module to its own procedures), e.g. DEFINT A-Z from DIRECTQB.BI.
- Modules named after the output file are skipped: use it for modules that only wrap assembly
  routines and that you replace with a QBLike version (an include file).
"""
import os, re, sys

if len(sys.argv) < 3:
    print(__doc__); sys.exit(1)
mak, out = sys.argv[1], sys.argv[2]
skip = set(m.upper() for m in sys.argv[3:])
d = os.path.dirname(os.path.abspath(mak))
mods = [l.strip() for l in open(mak, encoding='latin1') if l.strip()]

inc_re = re.compile(r"^\s*('|REM\s+)\s*\$INCLUDE\s*:?\s*'([^']+)'", re.I)
def_re = re.compile(r"^\s*DEF(INT|SNG|LNG|DBL|STR)\s+A\s*-\s*Z\s*$", re.I)
proc_re = re.compile(r"^\s*(STATIC\s+)?(SUB|FUNCTION)\s+[A-Za-z]", re.I)
end_re = re.compile(r"^\s*END\s+(SUB|FUNCTION)\b", re.I)
decl_re = re.compile(r"^\s*DECLARE\s", re.I)
meta_re = re.compile(r"^\s*('|REM\s+)\s*\$(DYNAMIC|STATIC)", re.I)
deftype_inc = {}   # DEFtype set by an include file (read once)

def include_deftype(name):
    key = name.upper()
    if key not in deftype_inc:
        t = None
        try:
            for l in open(os.path.join(d, name), encoding='latin1'):
                m = def_re.match(l)
                if m: t = m.group(1).upper()
        except OSError:
            pass
        deftype_inc[key] = t
    return deftype_inc[key]

main_code, extra, procs = [], [], []
for mi, m in enumerate(mods):
    if m.upper() in skip: continue
    path = os.path.join(d, m)
    if not os.path.exists(path) and os.path.exists(path + '.BAS'): path += '.BAS'
    lines = open(path, encoding='latin1').read().replace('\r', '').split('\n')
    state, inproc = 'SNG', False
    for l in lines:
        if inproc:
            procs.append(l)
            if end_re.match(l): inproc = False; procs.append('')
            continue
        mi_ = inc_re.match(l)
        if mi_:
            t = include_deftype(mi_.group(2).strip())
            if t: state = t
            if mi == 0: main_code.append(l)
            continue
        md = def_re.match(l)
        if md:
            state = md.group(1).upper()
            if mi == 0: main_code.append(l)
            continue
        if proc_re.match(l):
            procs += ['DEF%s A-Z' % state, l]
            inproc = not end_re.match(l)
            continue
        if mi == 0: main_code.append(l)
        elif not decl_re.match(l) and not meta_re.match(l) and l.strip(): extra.append(l)

# TYPE blocks of other modules go right after the main module's includes; the rest (DATA...)
# after its code
first = max([i + 1 for i, l in enumerate(main_code) if inc_re.match(l)] or [0])
types, rest, intype = [], [], False
for l in extra:
    if re.match(r"^\s*TYPE\s+\w+\s*$", l, re.I): intype = True
    (types if intype else rest).append(l)
    if re.match(r"^\s*END\s+TYPE", l, re.I): intype = False
res = main_code[:first] + types + main_code[first:] + [''] + rest + [''] + procs
open(out, 'w', encoding='latin1', newline='\r\n').write('\n'.join(res) + '\n')
print('%s: %d lines, %d procedures' % (out, len(res), sum(1 for l in procs if proc_re.match(l))))
