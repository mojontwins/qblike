/* using.c - PRINT USING formatting */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* provided by interp.c (non-static wrappers) */
double qb_eval_num(Expr *e);
void qb_eval_str(Expr *e, char **p, int *len);

typedef struct OutBuf { char *b; int n, cap; } OutBuf;
static void ob_put(OutBuf *o, const char *s, int n) {
    if (o->n + n + 1 > o->cap) { o->cap = (o->n + n + 1) * 2; o->b = (char *)qb_realloc(o->b, (size_t)o->cap); }
    memcpy(o->b + o->n, s, (size_t)n);
    o->n += n;
}
static void ob_ch(OutBuf *o, char c) { ob_put(o, &c, 1); }

static int is_num_field_start(const char *f, int i, int n) {
    char c = f[i];
    if (c == '#') return 1;
    if (c == '.' && i + 1 < n && f[i + 1] == '#') return 1;
    if (c == '+' && i + 1 < n && (f[i + 1] == '#' || f[i + 1] == '.' || f[i + 1] == '$' || f[i + 1] == '*')) return 1;
    if (c == '$' && i + 1 < n && f[i + 1] == '$') return 1;
    if (c == '*' && i + 1 < n && f[i + 1] == '*') return 1;
    return 0;
}
static int is_str_field_start(const char *f, int i, int n) {
    char c = f[i];
    if (c == '!' || c == '&') return 1;
    if (c == '\\') {
        int j = i + 1;
        while (j < n && f[j] == ' ') j++;
        if (j < n && f[j] == '\\') return 1;
    }
    return 0;
}

/* format one number according to field spec */
static void fmt_num_field(OutBuf *o, double v, int lead_plus, int trail_plus, int trail_minus,
                          int stars, int dollar, int ndig, int ndec, int hasdot, int comma, int exp) {
    char buf[512];
    int neg = v < 0;
    double a = fabs(v);
    if (exp) {
        /* scientific: ndig digits before point, ndec after */
        int before = ndig > 0 ? ndig : 1;
        if (!lead_plus && !trail_plus && !trail_minus && before > 1) before--; /* one position reserved for sign */
        int e = 0;
        if (a != 0) {
            e = (int)floor(log10(a)) - (before - 1);
            double m = a / pow(10, e);
            double r = floor(m * pow(10, ndec) + 0.5) / pow(10, ndec);
            if (r >= pow(10, before)) { e++; }
        }
        double m = a != 0 ? a / pow(10, e) : 0;
        char mant[256];
        if (hasdot) snprintf(mant, sizeof mant, "%*.*f", before + 1 + ndec, ndec, m);
        else snprintf(mant, sizeof mant, "%*.0f", before, m);
        char sign[2] = { 0, 0 };
        if (lead_plus) sign[0] = neg ? '-' : '+';
        else if (!trail_plus && !trail_minus && neg) sign[0] = '-';
        else if (!trail_plus && !trail_minus) sign[0] = ' ';
        int exd = exp >= 5 ? 3 : 2;
        snprintf(buf, sizeof buf, "%s%sE%c%0*d", sign, mant, e < 0 ? '-' : '+', exd, abs(e));
        ob_put(o, buf, (int)strlen(buf));
        if (trail_plus) ob_ch(o, neg ? '-' : '+');
        else if (trail_minus) ob_ch(o, neg ? '-' : ' ');
        return;
    }
    char digits[400];
    snprintf(digits, sizeof digits, "%.*f", ndec, a);
    /* split integer and fraction */
    char *dot = strchr(digits, '.');
    char ip[400], fp[100];
    if (dot) {
        int il = (int)(dot - digits);
        memcpy(ip, digits, (size_t)il); ip[il] = 0;
        snprintf(fp, sizeof fp, "%s", dot + 1);
    } else { snprintf(ip, sizeof ip, "%s", digits); fp[0] = 0; }
    if (strcmp(ip, "0") == 0 && hasdot && ndig == 0) ip[0] = 0;  /* ".50" style */
    if (strcmp(ip, "0") == 0 && hasdot && ndig > 0 && ndec > 0) {
        /* QBasic prints "0.50" when there is room for a digit before the point */
    }
    /* thousands separators */
    char ipc[600];
    if (comma && strlen(ip) > 3) {
        int l = (int)strlen(ip), k = 0;
        for (int i = 0; i < l; i++) {
            ipc[k++] = ip[i];
            int rem = l - 1 - i;
            if (rem > 0 && rem % 3 == 0) ipc[k++] = ',';
        }
        ipc[k] = 0;
    } else snprintf(ipc, sizeof ipc, "%s", ip);
    char body[800];
    if (hasdot) snprintf(body, sizeof body, "%s.%s", ipc, fp);
    else snprintf(body, sizeof body, "%s", ipc);
    /* sign and dollar */
    char pre[8] = "";
    int pl = 0;
    if (lead_plus) pre[pl++] = neg ? '-' : '+';
    else if (neg && !trail_minus && !trail_plus) pre[pl++] = '-';
    if (dollar) pre[pl++] = '$';
    pre[pl] = 0;
    int width = ndig + (hasdot ? 1 + ndec : 0) + (lead_plus ? 1 : 0) + (dollar ? 1 : 0) + (stars ? 0 : 0);
    char full[900];
    snprintf(full, sizeof full, "%s%s", pre, body);
    int fl = (int)strlen(full);
    if (fl > width) {
        ob_ch(o, '%');
        ob_put(o, full, fl);
    } else {
        char fill = stars ? '*' : ' ';
        for (int i = fl; i < width; i++) ob_ch(o, fill);
        ob_put(o, full, fl);
    }
    if (trail_plus) ob_ch(o, neg ? '-' : '+');
    else if (trail_minus) ob_ch(o, neg ? '-' : ' ');
}

/* returns index after field; formats item if e != NULL */
static int do_field(OutBuf *o, const char *f, int i, int n, Expr *e, int *consumed) {
    *consumed = 0;
    if (is_str_field_start(f, i, n)) {
        int width;
        int j = i;
        if (f[i] == '!') { width = 1; j = i + 1; }
        else if (f[i] == '&') { width = -1; j = i + 1; }
        else { j = i + 1; while (f[j] == ' ') j++; width = j - i + 1; j++; }
        if (e) {
            if (e->type != T_STR) rt_error(13);
            char *s; int l;
            qb_eval_str(e, &s, &l);
            if (width < 0) ob_put(o, s, l);
            else {
                int k = l < width ? l : width;
                ob_put(o, s, k);
                for (int q = k; q < width; q++) ob_ch(o, ' ');
            }
            free(s);
            *consumed = 1;
        }
        return j;
    }
    /* numeric */
    int lead_plus = 0, trail_plus = 0, trail_minus = 0, stars = 0, dollar = 0;
    int ndig = 0, ndec = 0, hasdot = 0, comma = 0, exp = 0;
    int j = i;
    if (f[j] == '+') { lead_plus = 1; j++; }
    if (j + 1 < n && f[j] == '*' && f[j + 1] == '*') {
        stars = 1; ndig += 2; j += 2;
        if (j < n && f[j] == '$') { dollar = 1; j++; }
    } else if (j + 1 < n && f[j] == '$' && f[j + 1] == '$') {
        dollar = 1; ndig += 1; j += 2;
    }
    while (j < n && (f[j] == '#' || f[j] == ',')) { if (f[j] == ',') comma = 1; ndig++; j++; }
    if (j < n && f[j] == '.') {
        hasdot = 1; j++;
        while (j < n && f[j] == '#') { ndec++; j++; }
    }
    if (j + 3 < n && f[j] == '^' && f[j + 1] == '^' && f[j + 2] == '^' && f[j + 3] == '^') {
        exp = 4; j += 4;
        if (j < n && f[j] == '^') { exp = 5; j++; }
    }
    if (!lead_plus && j < n && f[j] == '+') { trail_plus = 1; j++; }
    else if (!lead_plus && j < n && f[j] == '-') { trail_minus = 1; j++; }
    if (e) {
        if (e->type == T_STR) rt_error(13);
        double v = qb_eval_num(e);
        fmt_num_field(o, v, lead_plus, trail_plus, trail_minus, stars, dollar, ndig, ndec, hasdot, comma, exp);
        *consumed = 1;
    }
    return j;
}

void print_using(int filenum, const char *f, int n, Expr **items, int nitems, int *seps) {
    OutBuf o = { 0 };
    /* verify there is at least one field */
    int anyfield = 0;
    for (int i = 0; i < n; i++) {
        if (f[i] == '_') { i++; continue; }
        if (is_num_field_start(f, i, n) || is_str_field_start(f, i, n)) { anyfield = 1; break; }
    }
    if (!anyfield) rt_error(5);
    int i = 0;
    int item = 0;
    int last_sep = 0;
    while (item < nitems) {
        Expr *e = items[item];
        if (!e) { item++; continue; }
        /* emit literals up to next field */
        if (i >= n) i = 0;
        for (;;) {
            if (i >= n) { i = 0; }
            if (f[i] == '_' && i + 1 < n) { ob_ch(&o, f[i + 1]); i += 2; continue; }
            if (is_num_field_start(f, i, n) || is_str_field_start(f, i, n)) break;
            ob_ch(&o, f[i]);
            i++;
        }
        int consumed;
        i = do_field(&o, f, i, n, e, &consumed);
        last_sep = seps ? seps[item] : 0;
        item++;
    }
    /* trailing literals until next field */
    while (i < n) {
        if (f[i] == '_' && i + 1 < n) { ob_ch(&o, f[i + 1]); i += 2; continue; }
        if (is_num_field_start(f, i, n) || is_str_field_start(f, i, n)) break;
        ob_ch(&o, f[i]);
        i++;
    }
    if (filenum > 0) file_print(filenum, o.b ? o.b : "", o.n);
    else if (filenum == 0) scr_print(o.b ? o.b : "", o.n);
    if (nitems == 0 || last_sep == 0) {
        if (filenum > 0) file_print(filenum, "\r\n", 2);
        else if (filenum == 0) scr_newline();
    }
    free(o.b);
}
