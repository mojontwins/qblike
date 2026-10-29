# Creates the test files used by T_IO.BAS: a DirectQB datafile (as written by
# DQBENC.EXE, RLE + password), a FLI animation and a WAV sound.
import struct, math

def encode(data, pw):
    out = []
    pp = [0]
    def put(b):
        p = pw[pp[0] % len(pw)] if pw else 0
        out.append((b + p) & 255); pp[0] += 1
    i = 0; n = len(data)
    while i < n:
        b = data[i]
        j = i + 1
        lim = 255 if b == 255 else 254
        while j < n and data[j] == b and j - i < lim: j += 1
        run = j - i
        if b == 255 or run > 1:
            put(255); put(run); put(b)
        else:
            put(b)
        i = j
    return bytes(out)

pw = b"TEST"
packets = []
font = open("HITECH.FNT", "rb").read()[:2305]
packets.append(b"FNT\xfe" + font)
w, h = 16, 8
img = bytes(((x + y) % 16) + 1 if x > 1 else 0 for y in range(h) for x in range(w))
packets.append(b"IMG\xfe" + struct.pack("<HH", w, h) + img)
snd = bytes(128 + int(100 * math.sin(i / 5)) for i in range(2000))
packets.append(b"SND\xfe" + struct.pack("<I", len(snd)) + snd)
pal = bytes((i // 3 * 2 + i % 3) % 64 for i in range(768))
packets.append(b"PAL\xfe" + pal)
bma = bytes((k * 7) % 256 for k in range(512))
packets.append(b"BMA\xfe" + bytes([2]) + bma)
packets.append(b"CUR\xfe" + bytes(range(64)))
usr = b"HELLO\xff\xff\xff\x00\x01"
packets.append(b"USR\xfe" + struct.pack("<I", len(usr)) + usr)

hdr = b"DQBPaCk" + bytes([len(packets) - 1])
body = b""; offs = []
pos = 8 + 1024
for p in packets:
    e = encode(p, pw)
    offs.append(pos); body += e; pos += len(e)
offs += [0] * (256 - len(offs))
open("T_DATA.DQB", "wb").write(hdr + struct.pack("<256I", *offs) + body)

# FLI: 320x200, 3 frames + ring frame, speed 5
def chunk(t, d): return struct.pack("<IH", 6 + len(d), t) + d
def frame(chunks):
    d = b"".join(chunks)
    return struct.pack("<IHH8x", 16 + len(d), 0xF1FA, len(chunks)) + d
def color64(pal):
    return chunk(11, struct.pack("<H", 1) + bytes([0, 0]) + pal)
def brun(pix):
    d = b""
    for y in range(200):
        row = pix[y * 320:(y + 1) * 320]
        d += bytes([0])
        x = 0
        while x < 320:
            n = 1
            while x + n < 320 and row[x + n] == row[x] and n < 127: n += 1
            d += bytes([n, row[x]]); x += n
    return chunk(15, d)
def lc(line, x, col, n):
    d = struct.pack("<HH", line, 1) + bytes([1, x, (256 - n) & 255, col])
    return chunk(12, d)
gpal = bytes(min(63, (i // 3)) if i % 3 == 0 else 0 for i in range(768))
pix0 = bytes((x // 10) % 256 for y in range(200) for x in range(320))
frames = [frame([color64(gpal), brun(pix0)]), frame([lc(100, 10, 200, 20)]),
          frame([lc(110, 10, 201, 20)]), frame([lc(100, 10, 1, 20)])]
data = b"".join(frames)
h = struct.pack("<IHHHHHHH", 128 + len(data), 0xAF11, 3, 320, 200, 8, 0, 5)
h += bytes(128 - len(h))
open("T_ANIM.FLI", "wb").write(h + data)

# WAV 8-bit mono 11025 Hz
wav = bytes(128 + int(60 * math.sin(i / 3)) for i in range(5000))
open("T_SOUND.WAV", "wb").write(b"RIFF" + struct.pack("<I", 36 + len(wav)) + b"WAVEfmt " +
    struct.pack("<IHHIIHH", 16, 1, 1, 11025, 11025, 1, 8) + b"data" + struct.pack("<I", len(wav)) + wav)
print("ok")
