/* sound.c - PC-speaker style BEEP / SOUND / PLAY */
#include "qb.h"
#include "doslike/dos.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define RATE 22050
#define NCH 8

static struct sound_t *chsound[NCH];
static int nextch = 0;
static double q_end = 0;        /* time when queued audio finishes */
static double q_start = 0;
static int q_notes = 0;
static int snd_ready = 0;

/* PLAY state persists between PLAY statements */
static int p_oct = 4, p_len = 4, p_tempo = 120, p_mode = 0 /* 0 normal,1 legato,2 staccato */, p_bg = 0;

void snd_init(void) {
    if (snd_ready) return;
    setsoundmode(soundmode_16bit_mono_22050);
    snd_ready = 1;
}

void snd_reset(void) {
    snd_stop();
    p_oct = 4; p_len = 4; p_tempo = 120; p_mode = 0; p_bg = 0;
}

void snd_stop(void) {
    for (int i = 0; i < NCH; i++) stopsound(i);
    q_end = 0;
    q_notes = 0;
}

static void wait_until(double t) {
    while (sys_time() < t) {
        kb_poll();
        scr_present(0);
        if (kb_break_pressed()) { if (rs) rs->breakreq = 1; snd_stop(); return; }
        if (sys_shutting_down()) return;
        double rem = t - sys_time();
        sys_sleep(rem > 0.01 ? 5 : 1);
    }
}

static void submit(short *samples, int n, int foreground, int notes) {
    snd_init();
    double now = sys_time();
    if (q_end > now) wait_until(q_end);
    now = sys_time();
    if (n <= 0) return;
    int ch = nextch;
    nextch = (nextch + 1) % NCH;
    stopsound(ch);
    if (chsound[ch]) { free(chsound[ch]); chsound[ch] = NULL; }
    chsound[ch] = createsound(1, RATE, n, samples);
    playsound(ch, chsound[ch], 0, 160);
    q_start = now;
    q_end = now + (double)n / RATE;
    q_notes = notes;
    if (foreground) wait_until(q_end);
}

/* append a square wave tone to buffer */
typedef struct Buf { short *s; int n, cap; double phase; } Buf;
static void buf_tone(Buf *b, double freq, double secs, double on_frac) {
    int total = (int)(secs * RATE + 0.5);
    int on = (int)(total * on_frac);
    if (b->n + total > b->cap) { b->cap = (b->n + total) * 2 + 1024; b->s = (short *)qb_realloc(b->s, sizeof(short) * (size_t)b->cap); }
    for (int i = 0; i < total; i++) {
        short v = 0;
        if (i < on && freq > 0 && freq < 20000) {
            b->phase += freq / RATE;
            if (b->phase >= 1) b->phase -= floor(b->phase);
            v = b->phase < 0.5 ? 5000 : -5000;
        }
        b->s[b->n++] = v;
    }
}

void snd_beep(void) {
    Buf b = { 0 };
    buf_tone(&b, 800, 0.25, 1.0);
    submit(b.s, b.n, 0, 1);
    free(b.s);
}

void snd_sound(double freq, double ticks) {
    if (ticks == 0) { snd_stop(); return; }
    Buf b = { 0 };
    double secs = ticks / 18.2065;
    buf_tone(&b, freq >= 32767 ? 0 : freq, secs, 1.0);
    submit(b.s, b.n, 0, 1);
    free(b.s);
}

static int pnum(const char *s, int len, int *i, int *ok) {
    while (*i < len && s[*i] == ' ') (*i)++;
    int v = 0, any = 0;
    if (*i < len && s[*i] == '=') {
        while (*i < len && s[*i] != ';') (*i)++;
        if (*i < len) (*i)++;
        *ok = 0;
        return 0;
    }
    while (*i < len && isdigit((unsigned char)s[*i])) { v = v * 10 + (s[*i] - '0'); (*i)++; any = 1; }
    *ok = any;
    return v;
}

static double note_freq(int n) { return 440.0 * pow(2.0, (n - 46) / 12.0); }

void snd_play(const char *s, int len) {
    Buf b = { 0 };
    int notes = 0;
    int i = 0;
    while (i < len) {
        char c = (char)toupper((unsigned char)s[i++]);
        int ok;
        if (c == ' ' || c == ';') continue;
        if (c >= 'A' && c <= 'G') {
            static const int base[7] = { 10, 12, 1, 3, 5, 6, 8 }; /* A B C D E F G within octave (C=1) */
            int n = base[c - 'A'];
            while (i < len && (s[i] == '#' || s[i] == '+' || s[i] == '-')) { n += s[i] == '-' ? -1 : 1; i++; }
            int l = pnum(s, len, &i, &ok);
            if (!ok || l < 1 || l > 64) { if (ok && (l < 1 || l > 64)) rt_error(5); l = p_len; }
            double dur = (240.0 / p_tempo) / l;
            double mult = 1.5;
            while (i < len && s[i] == '.') { dur += (240.0 / p_tempo) / l * (mult - 1.0); mult = mult * 0.5 + 0.5; i++; }
            int note = p_oct * 12 + n;
            double frac = p_mode == 1 ? 1.0 : p_mode == 2 ? 0.75 : 0.875;
            buf_tone(&b, note_freq(note), dur, frac);
            notes++;
            continue;
        }
        switch (c) {
            case 'O': { int v = pnum(s, len, &i, &ok); if (!ok || v > 6) rt_error(5); p_oct = v; break; }
            case '>': if (p_oct < 6) p_oct++; break;
            case '<': if (p_oct > 0) p_oct--; break;
            case 'L': { int v = pnum(s, len, &i, &ok); if (!ok || v < 1 || v > 64) rt_error(5); p_len = v; break; }
            case 'T': { int v = pnum(s, len, &i, &ok); if (!ok || v < 32 || v > 255) rt_error(5); p_tempo = v; break; }
            case 'N': {
                int v = pnum(s, len, &i, &ok);
                if (!ok || v < 0 || v > 84) rt_error(5);
                double dur = (240.0 / p_tempo) / p_len;
                double mult = 1.5;
                while (i < len && s[i] == '.') { dur += (240.0 / p_tempo) / p_len * (mult - 1.0); mult = mult * 0.5 + 0.5; i++; }
                double frac = p_mode == 1 ? 1.0 : p_mode == 2 ? 0.75 : 0.875;
                buf_tone(&b, v == 0 ? 0 : note_freq(v), dur, v == 0 ? 0 : frac);
                notes++;
                break;
            }
            case 'P': {
                int v = pnum(s, len, &i, &ok);
                if (!ok || v < 1 || v > 64) rt_error(5);
                double dur = (240.0 / p_tempo) / v;
                double mult = 1.5;
                while (i < len && s[i] == '.') { dur += (240.0 / p_tempo) / v * (mult - 1.0); mult = mult * 0.5 + 0.5; i++; }
                buf_tone(&b, 0, dur, 0);
                break;
            }
            case 'M': {
                if (i >= len) rt_error(5);
                char m = (char)toupper((unsigned char)s[i++]);
                if (m == 'N') p_mode = 0;
                else if (m == 'L') p_mode = 1;
                else if (m == 'S') p_mode = 2;
                else if (m == 'F') p_bg = 0;
                else if (m == 'B') p_bg = 1;
                else rt_error(5);
                break;
            }
            case 'X': while (i < len && s[i] != ';') i++; break;
            default: rt_error(5);
        }
    }
    if (b.n > 0) submit(b.s, b.n, !p_bg, notes);
    free(b.s);
}

int snd_playfn(void) {
    double now = sys_time();
    if (now >= q_end || q_end <= q_start) return 0;
    double frac = (q_end - now) / (q_end - q_start);
    int n = (int)ceil(frac * q_notes);
    return n;
}
