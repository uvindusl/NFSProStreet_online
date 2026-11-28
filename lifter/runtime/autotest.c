/* State-driven scripted runs: PROSTREET_SCRIPT, stepped once per Present.
 *
 * Frame-timed input (PROSTREET_KEYS) breaks whenever loading takes a little
 * longer; this waits for the frontend itself instead.  Steps, separated by
 * commas, run in order:
 *
 *   wait=FeMainMenu.fng[:T]  until the package that has input control has this
 *                            name (case-insensitive); give up after T frames
 *                            (default 3000) and stop the script
 *   flow=6[:T]               until the game-flow state (0x00abb510) is 6
 *                            (3 in the frontend, 6 racing)
 *   key=28[:H]               hold DirectInput scan code 28 for H frames
 *                            (key=200+205:H holds up to three together)
 *                            (default 6), then wait as long again
 *   tap=28:FeMainMenu.fng[:T] tap scan code 28 every 60 frames until that
 *                            package has control (a dialog that ignores
 *                            accept while it is still busy); T as for wait;
 *                            tap=28:!PKG taps until PKG no longer has control
 *   text=PLAYER              type it (WM_CHAR, PROSTREET_TEXT's path)
 *   frames=N                 wait N frames
 *   dialog[:T]               until the open dialog (DialogInstance
 *                            0x00ab2870) takes accept: its +0x28 byte is 1
 *   dump                     render the next frame in software
 *                            (needs PROSTREET_FRAMEDUMP set, e.g. to 999999,
 *                            so the software renderer tracks device state)
 *
 * Each step is logged as "script: ...".  The frontend's package list comes
 * from cFEng ([0x00ab27d8] -> FEngine): FEMinList at +0xd8 (count +4, head
 * +8, node next +4); FEPackage name pointer +0x0c, has-control +0x20.
 * PROSTREET_SCRIPT_LOG=1 also prints the controlling package whenever it
 * changes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cpu.h"

extern void sw_request_dump(long frame);
extern void win32_post_char(uint32_t ch);

int g_script_key = -1;                  /* read by DirectInput GetDeviceState (com.c) */
int g_script_key2 = -1, g_script_key3 = -1;   /* more keys held with it (key=200+205) */

enum { S_WAIT, S_FLOW, S_KEY, S_TEXT, S_FRAMES, S_DUMP, S_DIALOG, S_TAP };
typedef struct { int op; long a, b, b2, b3; char s[64]; } Step;
static Step g_step[64];
static int g_nsteps = -1, g_cur;
static long g_since;                    /* frame the current step started */

static const char *top_package(void) {
    static char name[64];
    uint32_t cfeng = mem_is_committed(0x00ab27d8u) ? MR32(0x00ab27d8u) : 0;
    uint32_t fe = cfeng && mem_is_committed(cfeng) ? MR32(cfeng) : 0;
    if (!fe || !mem_is_committed(fe + 0xe0)) return "";
    uint32_t cnt = MR32(fe + 0xd8 + 4), node = MR32(fe + 0xd8 + 8);
    for (uint32_t i = 0; i < cnt && i < 64 && node && mem_is_committed(node + 0x24); i++) {
        if (MR32(node + 0x20)) {
            uint32_t p = MR32(node + 0x0c);
            if (!p || !mem_is_committed(p)) return "";
            snprintf(name, sizeof name, "%s", (const char *)g_mem + p);
            return name;
        }
        node = MR32(node + 4);
    }
    return "";
}

static void parse(void) {
    g_nsteps = 0;
    const char *e = getenv("PROSTREET_SCRIPT");
    while (e && *e && g_nsteps < 64) {
        const char *end = strchr(e, ',');
        size_t len = end ? (size_t)(end - e) : strlen(e);
        char tok[96];
        if (len >= sizeof tok) len = sizeof tok - 1;
        memcpy(tok, e, len); tok[len] = 0;
        e = end ? end + 1 : "";
        char *val = strchr(tok, '=');
        if (val) *val++ = 0;
        char *opt = val ? strchr(val, ':') : NULL;
        if (opt) *opt++ = 0;
        Step *s = &g_step[g_nsteps];
        memset(s, 0, sizeof *s); s->b2 = s->b3 = -1;
        if (!strcmp(tok, "wait") && val)        { s->op = S_WAIT; snprintf(s->s, sizeof s->s, "%s", val); s->a = opt ? atol(opt) : 3000; }
        else if (!strcmp(tok, "flow") && val)   { s->op = S_FLOW; s->b = atol(val); s->a = opt ? atol(opt) : 6000; }
        else if (!strcmp(tok, "key") && val)    {
            s->op = S_KEY; s->b2 = s->b3 = -1;
            char *p2 = strchr(val, '+'); if (p2) { *p2++ = 0; char *p3 = strchr(p2, '+'); if (p3) { *p3++ = 0; s->b3 = atol(p3); } s->b2 = atol(p2); }
            s->b = atol(val); s->a = opt ? atol(opt) : 6;
        }
        else if (!strcmp(tok, "text") && val)   { s->op = S_TEXT; snprintf(s->s, sizeof s->s, "%s", val); }
        else if (!strcmp(tok, "frames") && val) { s->op = S_FRAMES; s->a = atol(val); }
        else if (!strcmp(tok, "dump"))          { s->op = S_DUMP; }
        else if (!strcmp(tok, "tap") && val && opt) {
            s->op = S_TAP; s->b = atol(val);
            char *t = strchr(opt, ':'); if (t) *t++ = 0;
            snprintf(s->s, sizeof s->s, "%s", opt); s->a = t ? atol(t) : 3000;
        }
        else if (!strcmp(tok, "dialog"))        { s->op = S_DIALOG; s->a = val ? atol(val) : 3000; }
        else { fprintf(stderr, "script: unknown step '%s', ignored\n", tok); continue; }
        if (s->op == S_TAP && (s->b < 0 || s->b > 263)) { fprintf(stderr, "script: bad tap step, ignored\n"); continue; }
        if (s->op == S_KEY && (s->b < 0 || s->b > 263 || s->a <= 0)) { fprintf(stderr, "script: bad key step, ignored\n"); continue; }
        g_nsteps++;
    }
    if (g_nsteps) fprintf(stderr, "script: %d steps\n", g_nsteps);
}

void autotest_tick(long frame) {
    if (g_nsteps < 0) parse();
    static int log = -1;
    if (log < 0) { const char *l = getenv("PROSTREET_SCRIPT_LOG"); log = l && *l == '1'; }
    if (log) {
        static char last[64];
        const char *t = top_package();
        if (strcmp(t, last)) { fprintf(stderr, "script: frame %ld: control -> %s\n", frame, t[0] ? t : "(none)"); snprintf(last, sizeof last, "%s", t); }
        static uint32_t ld, lb = 0xffffffffu, lp;
        uint32_t d = mem_is_committed(0x00ab2870u) ? MR32(0x00ab2870u) : 0;
        uint32_t b = d && mem_is_committed(d + 0x28) ? MR8(d + 0x28) : 0xffu;
        uint32_t pend = mem_is_committed(0x00ab2874u) ? MR32(0x00ab2874u) : 0;
        if (d != ld || b != lb || pend != lp) { fprintf(stderr, "script: frame %ld: dialog %#x +0x28=%u pending=%#x\n", frame, d, b, pend); ld = d; lb = b; lp = pend; }
    }
    if (g_cur >= g_nsteps) return;
    Step *s = &g_step[g_cur];
    if (!g_since) g_since = frame;
    long el = frame - g_since;
    int done = 0;
    switch (s->op) {
    case S_WAIT:
        if (!strcasecmp(top_package(), s->s)) { fprintf(stderr, "script: frame %ld: %s has control\n", frame, s->s); done = 1; }
        else if (el > s->a) { fprintf(stderr, "script: frame %ld: gave up waiting for %s (control: %s); script stopped\n", frame, s->s, top_package()); g_cur = g_nsteps; return; }
        break;
    case S_FLOW: {
        uint32_t st = mem_is_committed(0x00abb510u) ? MR32(0x00abb510u) : 0;
        if ((long)st == s->b) { fprintf(stderr, "script: frame %ld: game flow %ld\n", frame, s->b); done = 1; }
        else if (el > s->a) { fprintf(stderr, "script: frame %ld: gave up waiting for game flow %ld (now %u); script stopped\n", frame, s->b, st); g_cur = g_nsteps; return; }
        break;
    }
    case S_KEY:
        if (el == 0) { g_script_key = (int)s->b; g_script_key2 = (int)s->b2; g_script_key3 = (int)s->b3;
                       fprintf(stderr, "script: frame %ld: key %ld%s\n", frame, s->b, s->b2 >= 0 ? " (+more)" : ""); }
        if (el == s->a) g_script_key = g_script_key2 = g_script_key3 = -1;
        if (el >= 2 * s->a) done = 1;
        break;
    case S_TEXT:
        if ((size_t)el / 4 < strlen(s->s)) { if (el % 4 == 0) win32_post_char((uint8_t)s->s[el / 4]); }
        else { fprintf(stderr, "script: frame %ld: typed %s\n", frame, s->s); done = 1; }
        break;
    case S_DIALOG: {
        uint32_t d = mem_is_committed(0x00ab2870u) ? MR32(0x00ab2870u) : 0;
        if (d && mem_is_committed(d + 0x28) && MR8(d + 0x28) == 1) { fprintf(stderr, "script: frame %ld: dialog %#x ready\n", frame, d); done = 1; }
        else if (el > s->a) { fprintf(stderr, "script: frame %ld: gave up waiting for a dialog; script stopped\n", frame); g_cur = g_nsteps; return; }
        break;
    }
    case S_TAP:
        if (s->s[0] == '!' ? strcasecmp(top_package(), s->s + 1) != 0 : !strcasecmp(top_package(), s->s)) { g_script_key = -1; fprintf(stderr, "script: frame %ld: %s has control\n", frame, s->s); done = 1; }
        else if (el > s->a) { g_script_key = -1; fprintf(stderr, "script: frame %ld: gave up tapping for %s (control: %s); script stopped\n", frame, s->s, top_package()); g_cur = g_nsteps; return; }
        else g_script_key = (el % 60) < 6 ? (int)s->b : -1;
        break;
    case S_FRAMES:
        if (el >= s->a) done = 1;
        break;
    case S_DUMP:
        sw_request_dump(frame + 1);
        fprintf(stderr, "script: frame %ld: dump of frame %ld requested\n", frame, frame + 1);
        done = 1;
        break;
    }
    if (done) { g_cur++; g_since = 0; if (g_cur == g_nsteps) fprintf(stderr, "script: frame %ld: done\n", frame); }
}
