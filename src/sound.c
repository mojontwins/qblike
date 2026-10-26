/* sound.c - PC-speaker style BEEP / SOUND / PLAY, and the MUSIC / SOUND PLAY extensions */
#include "qb.h"
#include "doslike/dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define RATE 22050
#define NCH 4                      /* dos-like channels 0-3: BEEP/SOUND/PLAY; 4-15: SOUND PLAY 1-12 */
#define UCH0 NCH
#define NUCH 12

static struct sound_t *chsound[NCH];
static int nextch = 0;
static int snd_ready = 0;

/* The audio thread picks up sound and music pointers once per frame, so anything we stop is
   freed a little later, never at once. */
#define NGRAVE 512
static struct { void *p; double t; } grave[NGRAVE];
static int ngrave = 0;
static void grave_collect(int all) {
    double now = sys_time();
    int k = 0;
    for (int i = 0; i < ngrave; i++) {
        if (all || now - grave[i].t > 0.3) free(grave[i].p);
        else grave[k++] = grave[i];
    }
    ngrave = k;
}
static void defer_free(void *p) {
    if (!p) return;
    if (ngrave == NGRAVE) { sys_sleep(300); grave_collect(1); }
    grave[ngrave].p = p; grave[ngrave].t = sys_time(); ngrave++;
}

/* SOUND file$ / SOUND PLAY / MUSIC */
#define NSND 256
static struct sound_t *usnd[NSND];
static char *usnd_name[NSND];
static int nusnd = 0;
static double uch_start[NUCH];
static struct music_t *cur_music = NULL;
static int music_vol = 255;

/* Music queue. Every note (or pause) of BEEP/SOUND/PLAY is queued with the time it ends; the next
   one starts when the previous one finishes. In background mode (PLAY "MB...", SOUND) statements
   return at once, as in QBasic, unless more than 32 notes are pending; ON PLAY(n) watches the count. */
#define QN 512
static double qn_end[QN];
static int qn_n = 0;
static double q_tail = 0;          /* time when the queued audio ends */
typedef struct Pend { double start; struct sound_t *snd; } Pend;   /* sounds waiting for their turn */
#define NPEND 64
static Pend pend[NPEND];
static int npend = 0;

/* PLAY state persists between PLAY statements */
static int p_oct = 4, p_len = 4, p_tempo = 120, p_mode = 0 /* 0 normal,1 legato,2 staccato */, p_bg = 0;

void snd_init(void) {
    if (snd_ready) return;
    setsoundmode(soundmode_16bit_stereo_44100);
    snd_ready = 1;
}

void snd_reset(void) {
    snd_end();
    p_oct = 4; p_len = 4; p_tempo = 120; p_mode = 0; p_bg = 0;
}

void snd_stop(void) {
    for (int i = 0; i < NCH; i++) stopsound(i);
    for (int i = 0; i < npend; i++) free(pend[i].snd);
    npend = 0;
    qn_n = 0;
    q_tail = 0;
}

static void start_sound(struct sound_t *snd) {
    int ch = nextch;
    nextch = (nextch + 1) % NCH;
    stopsound(ch);
    if (chsound[ch]) { defer_free(chsound[ch]); chsound[ch] = NULL; }
    chsound[ch] = snd;
    playsound(ch, snd, 0, 160);
}

void snd_poll(void) {
    double now = sys_time();
    if (ngrave) grave_collect(0);
    for (int i = 0; i < npend; ) {
        if (pend[i].start <= now + 0.002) { start_sound(pend[i].snd); pend[i] = pend[--npend]; }
        else i++;
    }
}

int snd_queue_count(void) {
    double now = sys_time();
    int k = 0;
    for (int i = 0; i < qn_n; i++) if (qn_end[i] > now) qn_end[k++] = qn_end[i];
    qn_n = k;
    return qn_n;
}

static void wait_until(double t) {
    while (sys_time() < t) {
        kb_poll();
        scr_present(0);
        snd_poll();
        if (kb_break_pressed()) { if (rs) rs->breakreq = 1; snd_stop(); return; }
        if (sys_shutting_down()) return;
        double rem = t - sys_time();
        sys_sleep(rem > 0.01 ? 5 : 1);
    }
}

/* queue audio: samples (may be all silence) made of 'notes' notes with the given durations */
static void submit(short *samples, int n, int foreground, int notes, const double *durs) {
    snd_init();
    if (n <= 0 && notes <= 0) return;
    /* QBasic's background buffer holds 32 notes: wait for room */
    while (snd_queue_count() + notes > 32 && snd_queue_count() > 0) {
        double first = 1e30;
        for (int i = 0; i < qn_n; i++) if (qn_end[i] < first) first = qn_end[i];
        wait_until(first);
        if (sys_shutting_down() || (rs && rs->breakreq)) return;
    }
    double now = sys_time();
    /* continue right after the queued audio; if it just ended, keep the exact rhythm */
    double start = q_tail > now - 0.02 ? q_tail : now;
    double t = start;
    for (int i = 0; i < notes && qn_n < QN; i++) { t += durs[i]; qn_end[qn_n++] = t; }
    q_tail = start + (double)n / RATE;
    if (t > q_tail) q_tail = t;
    int silent = 1;
    for (int i = 0; i < n; i++) if (samples[i]) { silent = 0; break; }
    if (!silent && n > 0) {
        struct sound_t *snd = createsound(1, RATE, n, samples);
        if (start <= now + 0.002) start_sound(snd);
        else if (npend < NPEND) { pend[npend].start = start; pend[npend].snd = snd; npend++; }
        else free(snd);
    }
    if (foreground) wait_until(q_tail);
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
    double d = 0.25;
    submit(b.s, b.n, 1, 1, &d);
    free(b.s);
}

void snd_sound(double freq, double ticks) {
    if (ticks == 0) { snd_stop(); return; }
    Buf b = { 0 };
    double secs = ticks / 18.2065;
    buf_tone(&b, freq >= 32767 ? 0 : freq, secs, 1.0);
    submit(b.s, b.n, !p_bg, 1, &secs);
    free(b.s);
}

static int pnum(const char *s, int len, int *i, int *ok) {
    while (*i < len && s[*i] == ' ') (*i)++;
    int v = 0, any = 0;
    if (*i < len && s[*i] == '=') {
        /* =VARPTR$(var) */
        (*i)++;
        if (*i + 3 > len) rt_error(5);
        double d = qb_varptr_num(s + *i);
        *i += 3;
        if (*i < len && s[*i] == ';') (*i)++;
        *ok = 1;
        return (int)floor(d + 0.5);
    }
    while (*i < len && isdigit((unsigned char)s[*i])) { v = v * 10 + (s[*i] - '0'); (*i)++; any = 1; }
    *ok = any;
    return v;
}

static double note_freq(int n) { return 440.0 * pow(2.0, (n - 46) / 12.0); }

typedef struct Durs { double *d; int n, cap; } Durs;
static void add_dur(Durs *q, double d) {
    if (q->n == q->cap) { q->cap = q->cap ? q->cap * 2 : 32; q->d = realloc(q->d, sizeof(double) * (size_t)q->cap); }
    q->d[q->n++] = d;
}

static double dotted(int l, int *i, const char *s, int len) {
    double dur = (240.0 / p_tempo) / l;
    double mult = 1.5;
    while (*i < len && s[*i] == '.') { dur += (240.0 / p_tempo) / l * (mult - 1.0); mult = mult * 0.5 + 0.5; (*i)++; }
    return dur;
}

static void play_parse(const char *s, int len, Buf *b, Durs *q, int depth) {
    int i = 0;
    if (depth > 16) rt_error(5);
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
            double dur = dotted(l, &i, s, len);
            int note = p_oct * 12 + n;
            double frac = p_mode == 1 ? 1.0 : p_mode == 2 ? 0.75 : 0.875;
            buf_tone(b, note_freq(note), dur, frac);
            add_dur(q, dur);
            continue;
        }
        switch (c) {
            case 'O': { int v = pnum(s, len, &i, &ok); if (!ok || v < 0 || v > 6) rt_error(5); p_oct = v; break; }
            case '>': if (p_oct < 6) p_oct++; break;
            case '<': if (p_oct > 0) p_oct--; break;
            case 'L': { int v = pnum(s, len, &i, &ok); if (!ok || v < 1 || v > 64) rt_error(5); p_len = v; break; }
            case 'T': { int v = pnum(s, len, &i, &ok); if (!ok || v < 32 || v > 255) rt_error(5); p_tempo = v; break; }
            case 'N': {
                int v = pnum(s, len, &i, &ok);
                if (!ok || v < 0 || v > 84) rt_error(5);
                double dur = dotted(p_len, &i, s, len);
                double frac = p_mode == 1 ? 1.0 : p_mode == 2 ? 0.75 : 0.875;
                buf_tone(b, v == 0 ? 0 : note_freq(v), dur, v == 0 ? 0 : frac);
                add_dur(q, dur);
                break;
            }
            case 'P': {
                int v = pnum(s, len, &i, &ok);
                if (!ok || v < 1 || v > 64) rt_error(5);
                double dur = dotted(v, &i, s, len);
                buf_tone(b, 0, dur, 0);
                add_dur(q, dur);
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
            case 'X': {
                if (i + 3 > len) rt_error(5);
                const char *ss; int sl;
                qb_varptr_str(s + i, &ss, &sl);
                i += 3;
                if (i < len && s[i] == ';') i++;
                char *cp = malloc((size_t)sl + 1);
                memcpy(cp, ss, (size_t)sl);
                play_parse(cp, sl, b, q, depth + 1);
                free(cp);
                break;
            }
            default: rt_error(5);
        }
    }
}

void snd_play(const char *s, int len) {
    Buf b = { 0 };
    Durs q = { 0 };
    play_parse(s, len, &b, &q, 0);
    if (b.n > 0 || q.n > 0) submit(b.s, b.n, !p_bg, q.n, q.d);
    free(b.s);
    free(q.d);
}

int snd_playfn(void) {
    return snd_queue_count();
}


/* ------------------------------------------------------------------ */
/* QBLike extensions: MUSIC and SOUND file$ / SOUND PLAY / SOUND STOP  */
/* ------------------------------------------------------------------ */
void file_native_name(const char *in, int len, char *out, int outsz, int for_read);

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* end of program: stop music and samples, forget loaded sounds */
void snd_end(void) {
    snd_stop();
    for (int i = 0; i < NUCH; i++) stopsound(UCH0 + i);
    stopmusic();
    defer_free(cur_music); cur_music = NULL;
    for (int i = 0; i < nusnd; i++) { defer_free(usnd[i]); free(usnd_name[i]); usnd[i] = NULL; usnd_name[i] = NULL; }
    nusnd = 0;
    music_vol = 255;
}

int snd_load(const char *name, int nl) {
    snd_init();
    char path[1024];
    file_native_name(name, nl, path, sizeof path, 1);
    for (int i = 0; i < nusnd; i++) {   /* already loaded */
#ifdef _WIN32
        if (!_stricmp(usnd_name[i], path)) return i + 1;
#else
        if (!strcmp(usnd_name[i], path)) return i + 1;
#endif
    }
    if (!file_exists(path)) rt_error(53);
    if (nusnd == NSND) rt_error_msg(7, "Too many sounds loaded");
    struct sound_t *snd = loadwav(path);
    if (!snd) rt_error_msg(5, "Bad or unsupported sound file (WAV, 4000-48000 Hz, mono or stereo)");
    usnd[nusnd] = snd;
    usnd_name[nusnd] = qb_strdup(path);
    return ++nusnd;
}

void snd_sample_play(int id, int ch, int vol, int pan, int loop) {
    if (id < 1 || id > nusnd || !usnd[id - 1]) rt_error(5);
    if (ch < 0 || ch > NUCH || vol < 0 || vol > 255 || pan < -100 || pan > 100) rt_error(5);
    double now = sys_time();
    if (ch == 0) {   /* first free channel, else the one playing for the longest time */
        int best = 0;
        for (int i = 0; i < NUCH; i++) {
            if (!soundplaying(UCH0 + i)) { best = i; goto found; }
            if (uch_start[i] < uch_start[best]) best = i;
        }
    found:
        ch = best + 1;
    }
    int l = pan > 0 ? vol * (100 - pan) / 100 : vol;
    int r = pan < 0 ? vol * (100 + pan) / 100 : vol;
    uch_start[ch - 1] = now;
    playsound(UCH0 + ch - 1, usnd[id - 1], loop, vol);
    soundvolume(UCH0 + ch - 1, l, r);
}

void snd_sample_stop(int ch) {
    if (ch < 0 || ch > NUCH) rt_error(5);
    for (int i = 0; i < NUCH; i++) if (ch == 0 || ch == i + 1) stopsound(UCH0 + i);
}

void snd_music_play(const char *name, int nl, int loop) {
    snd_init();
    char path[1024];
    file_native_name(name, nl, path, sizeof path, 1);
    FILE *f = fopen(path, "rb");
    if (!f) rt_error(53);
    unsigned char h[4] = { 0 };
    size_t n = fread(h, 1, 4, f);
    fclose(f);
    struct music_t *m = NULL;
    if (n == 4 && !memcmp(h, "MThd", 4)) m = qbl_loadmid_opl(path);
    else m = loadmod(path);
    if (!m) rt_error_msg(5, "Bad or unsupported music file (MOD or MID)");
    stopmusic();
    defer_free(cur_music);
    cur_music = m;
    playmusic(m, loop ? 1 : 0, music_vol);
}

void snd_music_stop(void) {
    stopmusic();
    defer_free(cur_music);
    cur_music = NULL;
}

void snd_music_volume(int v) {
    if (v < 0 || v > 255) rt_error(5);
    music_vol = v;
    musicvolume(v);
}

int snd_music_playing(void) {
    return cur_music && musicplaying();
}
