#!/usr/bin/env python3
"""Builds REFERENCE.md from QBLike's built-in help.

    qblike --help-dump > help.txt
    python3 tools/help2md.py help.txt > REFERENCE.md

(On Linux `qblike --help-dump` needs a display, like the editor; under Xvfb it works too.)
"""
import re
import sys


def parse(dump):
    entries, topics, aliases = [], {}, []
    for m in re.finditer(r'@@ENT\t(.*?)\n@@SYN\n(.*?)\n@@TXT\n(.*?)\n@@SEE\t(.*?)\n', dump, re.S):
        entries.append({'name': m.group(1), 'syntax': m.group(2), 'text': m.group(3),
                        'see': m.group(4).split() if m.group(4).strip() else []})
    for m in re.finditer(r'@@TOPIC\t(.*?)\n(.*?)\n@@END\n', dump, re.S):
        topics[m.group(1)] = m.group(2)
    for m in re.finditer(r'@@ALIAS\t(.*?)\t(.*?)\n', dump):
        aliases.append((m.group(1), m.group(2)))
    return entries, topics, aliases


def anchor(name, kind='kw'):
    a = name.lower().replace('$', '-s').replace(' ', '-').replace('?', 'q')
    a = re.sub(r'[^a-z0-9-]', '', a)
    return f'{kind}-{a}'


class Linker:
    def __init__(self, entries, topics, aliases):
        self.kw = {e['name'].upper(): e['name'] for e in entries}
        self.topics = {t.upper(): t for t in topics}
        self.alias = {a.upper(): b for a, b in aliases}

    def target(self, name):
        u = name.upper()
        if u == 'CONTENTS':
            return '#contents'
        if u in self.kw:
            return '#' + anchor(self.kw[u])
        if u in self.topics:
            return '#' + anchor(self.topics[u], 'topic')
        if u == 'INDEX':
            return '#index'
        if u == 'CONTENTS':
            return '#contents'
        if u in self.alias:
            return self.target(self.alias[u])
        # "SOUND PLAY" style: first word
        first = name.split()[0].upper() if name.split() else u
        if first in self.kw:
            return '#' + anchor(self.kw[first])
        return None

    def target_exact(self, name):
        u = name.upper()
        return u in self.kw or u in self.topics or u in self.alias

    def link(self, name):
        t = self.target(name)
        return f'[{esc(name)}]({t})' if t else f'`{name}`'


def esc(text):
    """escape Markdown in prose"""
    return re.sub(r'([\\`*_\[\]<>|#$])', r'\\\1', text)


def prose(text, lk):
    """prose line: <<links>> become links, the rest is escaped"""
    out, pos = [], 0
    for m in re.finditer(r'<<(.*?)>>', text):
        out.append(esc(text[pos:m.start()]))
        out.append(lk.link(m.group(1)))
        pos = m.end()
    out.append(esc(text[pos:]))
    return ''.join(out)


def plain(text):
    return re.sub(r'<<(.*?)>>', r'\1', text)


def body(text, lk):
    """help text -> Markdown: indented blocks become code, '- ' lines bullets, the rest paragraphs"""
    out = []
    para, bullet, code = [], None, []
    in_list = False

    def end_para():
        nonlocal para
        if para:
            out.append(prose(' '.join(para), lk))
            out.append('')
            para = []

    def end_bullet(close_list):
        nonlocal bullet, in_list
        if bullet is not None:
            out.append('- ' + prose(' '.join(bullet), lk))
            bullet = None
        if close_list and in_list:
            out.append('')
            in_list = False

    def end_code():
        nonlocal code
        while code and not code[-1].strip():
            code.pop()
        if code:
            out.append('```')
            out.extend(plain(c) for c in code)
            out.append('```')
            out.append('')
        code = []

    for ln in text.split('\n'):
        if not ln.strip():
            if code:
                code.append('')
            else:
                end_para()
                end_bullet(True)
            continue
        if ln.startswith('- '):
            end_para(); end_code(); end_bullet(False)
            bullet = [ln[2:].strip()]
            in_list = True
            continue
        if bullet is not None and ln.startswith('  ') and not ln.startswith('   '):
            bullet.append(ln.strip())
            continue
        if ln.startswith(' '):
            end_para(); end_bullet(True)
            code.append(ln[2:] if ln.startswith('  ') else ln[1:])
            continue
        end_code(); end_bullet(True)
        para.append(ln.strip())
    end_para(); end_bullet(True); end_code()
    return '\n'.join(out).rstrip() + '\n'


def see_names(see, lk):
    """split a see-also list, keeping two-word names (DEF SEG, ON ERROR...) together"""
    words, res, i = see, [], 0
    while i < len(words):
        if i + 1 < len(words) and lk.target_exact(words[i] + ' ' + words[i + 1]):
            res.append(words[i] + ' ' + words[i + 1]); i += 2
        else:
            res.append(words[i]); i += 1
    return res


def topic_md(name, text, lk):
    lines = text.split('\n')
    # first non-empty line is the title
    while lines and not lines[0].strip():
        lines.pop(0)
    title = lines.pop(0).strip() if lines else name
    see = None
    rest = []
    for ln in lines:
        if ln.strip().startswith('See also:'):
            see = re.findall(r'<<(.*?)>>', ln)
        else:
            rest.append(ln)
    while rest and not rest[-1].strip():
        rest.pop()
    md = [f'<a id="{anchor(name, "topic")}"></a>', '', f'### {title}', '']
    # split into prose paragraphs (lines with a single leading indent that read as text) and
    # aligned tables (kept as code)
    block = '\n'.join(rest).strip('\n')
    paras = re.split(r'\n\s*\n', block)
    for p in paras:
        pl = p.split('\n')
        if not p.strip():
            continue
        ded = [l[2:] if l.startswith('  ') else l for l in pl]
        is_table = any('  ' in l.strip() for l in ded) or any(l.startswith(' ') for l in ded) \
            or any(l.lstrip().startswith('*') for l in ded)
        if any(l.lstrip().startswith('* ') for l in ded) and not any(re.search(r'\S {3,}\S', l) for l in ded if not l.lstrip().startswith('* ')):
            for l in ded:
                md.append('- ' + prose(l.lstrip()[2:], lk) if l.lstrip().startswith('* ') else '  ' + prose(l.strip(), lk))
            md.append('')
        elif is_table:
            md.append('```')
            md.extend(plain(l) for l in ded)
            md.append('```')
            md.append('')
        else:
            md.append(prose(' '.join(l.strip() for l in ded), lk))
            md.append('')
    if see:
        md.append('See also: ' + ', '.join(lk.link(s) for s in see))
        md.append('')
    return '\n'.join(md)


def contents_by_task(text, lk):
    rows, cur = [], None
    on = False
    for ln in text.split('\n'):
        if 'Keywords by task' in ln:
            on = True
            continue
        if not on or not ln.strip():
            continue
        m = re.match(r'\s+([A-Za-z/ ]+):\s+(.*)', ln)
        if m and '<<' in m.group(2):
            cur = [m.group(1).strip(), re.findall(r'<<(.*?)>>', m.group(2))]
            rows.append(cur)
        elif cur is not None:
            cur[1].extend(re.findall(r'<<(.*?)>>', ln))
    md = ['| Task | Keywords |', '|---|---|']
    for task, kws in rows:
        md.append(f'| {task} | ' + ' · '.join(lk.link(k) for k in kws) + ' |')
    return '\n'.join(md) + '\n'


def main():
    dump = open(sys.argv[1], encoding='latin-1').read() if len(sys.argv) > 1 else sys.stdin.read()
    entries, topics, aliases = parse(dump)
    lk = Linker(entries, topics, aliases)
    entries.sort(key=lambda e: e['name'].upper())
    ext = [e for e in entries if e['text'].startswith('QBLike extension')]

    o = []
    o.append('# QBLike language reference\n')
    o.append('Complete reference of the language accepted by QBLike: every statement, function and operator of '
             'QBasic 1.1, with the behavior of QBLike where it differs, plus the QBLike extensions. It is the same '
             'text as the built-in help of the editor (F1), generated from it with `tools/help2md.py`.\n')
    o.append('For a guided description of the extensions and the emulated hardware, see '
             '[EXTENSIONES_en.md](EXTENSIONES_en.md) ([EXTENSIONES_es.md](EXTENSIONES_es.md) in Spanish).\n')
    o.append('## Contents\n')
    o.append('- [QBLike compared with QBasic](#qblike-compared-with-qbasic)')
    o.append('- [Keywords by task](#keywords-by-task)')
    o.append('- [Index](#index)')
    o.append('- [Language topics](#language-topics): '
             + ', '.join(f'[{t}](#{anchor(t, "topic")})' for t in ('Data Types', 'Operators', 'Errors', 'Game Programming')))
    o.append('- [Keyword reference](#keyword-reference)')
    o.append('- [The editor](#the-editor): '
             + ', '.join(f'[{t}](#{anchor(t, "topic")})' for t in ('Editing Keys', 'Debugging', 'Menus', 'Using Help')))
    o.append('')

    o.append('## QBLike compared with QBasic\n')
    o.append('Programs written for QBasic 1.1 run unchanged. The differences are:\n')
    o.append('- **Extensions** (new statements and functions): '
             + ', '.join(lk.link(e['name']) for e in ext)
             + '; also ' + lk.link('PUT') + ' `MASK`, ' + lk.link('WAIT') + ' `VSYNC`, ' + lk.link('SOUND')
             + ' `file$` / `SOUND PLAY` / `SOUND STOP`, and `SCREEN 3` (PCjr/Tandy, see ' + lk.link('SCREEN') + ').')
    o.append('- **Emulated hardware**: video memory (' + lk.link('DEF SEG') + ', ' + lk.link('PEEK') + ', '
             + lk.link('POKE') + '), VGA/CGA ports and the AdLib FM synthesizer (' + lk.link('INP') + ', '
             + lk.link('OUT') + '), vertical sync (' + lk.link('WAIT') + '), BIOS clock and keyboard flags.')
    o.append('- **No memory limits**: strings up to 1 GB, arrays up to 64 million elements with LONG subscripts, '
             'about 100000 levels of SUB/FUNCTION recursion (see ' + lk.link('FRE') + ').')
    o.append('- **Behavior changes**: ' + lk.link('PUT') + ' clips images that go off the screen instead of '
             'failing; ' + lk.link('LEN') + ' and ' + lk.link('INSTR') + ' return LONG.')
    o.append('- **Not available**: ' + lk.link('CHAIN') + ', `CALL ABSOLUTE`/`INTERRUPT`, ' + lk.link('RUN')
             + ' with a file or line, printing (' + lk.link('LPRINT') + ' output is discarded), serial ports, '
             'light pen and joystick (' + lk.link('COM') + ', ' + lk.link('PEN') + ', ' + lk.link('STICK')
             + '), and ' + lk.link('KEY') + ' events. They are accepted where possible so old programs still run.')
    o.append('')

    o.append('## Keywords by task\n')
    o.append(contents_by_task(topics.get('Contents', ''), lk))

    o.append('## Index\n')
    o.append('<a id="index"></a>\n')
    names = sorted([(e['name'], None) for e in entries] + [(a, b) for a, b in aliases
                   if a.upper() not in lk.kw and a != '?'], key=lambda x: x[0].upper().lstrip('$'))
    groups = {}
    for n, target in names:
        c = n[0].upper()
        groups.setdefault(c if c.isalpha() else '#', []).append((n, target))
    for c in sorted(groups):
        items = []
        for n, target in groups[c]:
            if target:
                items.append(f'{esc(n)} → {lk.link(target)}')
            else:
                items.append(lk.link(n))
        o.append(f'**{c}** — ' + ' · '.join(items) + '\n')

    o.append('## Language topics\n')
    for t in ('Data Types', 'Operators', 'Errors', 'Game Programming'):
        if t in topics:
            o.append(topic_md(t, topics[t], lk))

    o.append('## Keyword reference\n')
    for e in entries:
        o.append(f'<a id="{anchor(e["name"])}"></a>\n')
        title = e['name']
        if e['text'].startswith('QBLike extension'):
            title += ' *(QBLike extension)*'
        o.append(f'### {title}\n')
        o.append('```')
        o.append(plain(e['syntax']))
        o.append('```\n')
        txt = e['text']
        if txt.startswith('QBLike extension. '):
            txt = txt[len('QBLike extension. '):]
        o.append(body(txt, lk))
        if e['see']:
            o.append('See also: ' + ', '.join(lk.link(n) for n in see_names(e['see'], lk)) + '\n')

    o.append('## The editor\n')
    for t in ('Editing Keys', 'Debugging', 'Menus', 'Using Help'):
        if t in topics:
            o.append(topic_md(t, topics[t], lk))

    sys.stdout.write('\n'.join(o).rstrip() + '\n')


if __name__ == '__main__':
    main()
