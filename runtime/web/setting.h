/* setting.h -- settings from the environment: NFSU2_* env vars passed by the page.
 * Included by the C runtime (gpu_web.c uses ENVC) and the Mac app.
 * No guest memory here. */

#ifndef NFSU2_SETTING_H
#define NFSU2_SETTING_H

#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* for strcasecmp */

/* Read an NFSU2_* env var once per call site and keep it.  For hot paths:
 * getenv + the cached lookup cost (~3% of the guest thread in a profile),
 * and the environment does not change during a run. */
#define ENVC(nm_) ({ static const char *_ev_; static int _ei_; \
                     if (!_ei_){ _ei_ = 1; _ev_ = getenv(nm_); } _ev_; })

/* An on/off setting (the page writes 1 or 0; the Mac app passes the line's
 * own on/off): unset or empty, dflt; 0..., off, no or false (any case), 0;
 * anything else, 1.  Read each call: a caller that reads it often keeps it. */
static inline int setting_bool(const char *env, int dflt){
    const char *e = getenv(env);
    if (!e || !*e) return dflt;
    return *e != '0' && strcasecmp(e, "off") && strcasecmp(e, "no") && strcasecmp(e, "false");
}
#endif