#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <ftw.h>

#include "cJSON.h"
#include "hwmon.h"

/* ─── session ID set ─────────────────────────────────────────────────────── */

#define MAX_SESSIONS 256

static char g_sessions[MAX_SESSIONS][128];
static int  g_session_count;

static int session_seen(const char *sid)
{
    for (int i = 0; i < g_session_count; i++)
        if (strcmp(g_sessions[i], sid) == 0) return 1;
    return 0;
}

static void session_add(const char *sid)
{
    if (g_session_count >= MAX_SESSIONS) return;
    strncpy(g_sessions[g_session_count++], sid, sizeof(g_sessions[0]) - 1);
}

/* ─── aggregated stats ───────────────────────────────────────────────────── */

static struct {
    long long input;
    long long output;
    long long cache_read;
    long long cache_create;
    int       messages;
} g_stats;

static char g_today[16]; /* YYYY-MM-DD */

/* ─── per-file processing ────────────────────────────────────────────────── */

static void process_jsonl(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char  *line = NULL;
    size_t cap  = 0;

    while (getline(&line, &cap, fp) != -1) {
        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* filter: type == "assistant" */
        cJSON *type_j = cJSON_GetObjectItemCaseSensitive(obj, "type");
        if (!type_j || !cJSON_IsString(type_j) ||
            strcmp(type_j->valuestring, "assistant") != 0)
            goto next;

        /* filter: timestamp starts with today */
        cJSON *ts_j = cJSON_GetObjectItemCaseSensitive(obj, "timestamp");
        if (!ts_j || !cJSON_IsString(ts_j) ||
            strncmp(ts_j->valuestring, g_today, 10) != 0)
            goto next;

        /* filter: message.usage exists */
        cJSON *msg_j = cJSON_GetObjectItemCaseSensitive(obj, "message");
        if (!msg_j) goto next;
        cJSON *usage_j = cJSON_GetObjectItemCaseSensitive(msg_j, "usage");
        if (!usage_j) goto next;

        /* accumulate tokens */
        cJSON *j;
#define ADD_TOK(field, member) \
        if ((j = cJSON_GetObjectItemCaseSensitive(usage_j, field)) && cJSON_IsNumber(j)) \
            g_stats.member += (long long)j->valuedouble;

        ADD_TOK("input_tokens",                 input)
        ADD_TOK("output_tokens",                output)
        ADD_TOK("cache_read_input_tokens",       cache_read)
        ADD_TOK("cache_creation_input_tokens",   cache_create)
#undef ADD_TOK

        /* unique session IDs */
        cJSON *sid_j = cJSON_GetObjectItemCaseSensitive(obj, "sessionId");
        if (sid_j && cJSON_IsString(sid_j) && !session_seen(sid_j->valuestring))
            session_add(sid_j->valuestring);

        g_stats.messages++;

next:
        cJSON_Delete(obj);
    }

    free(line);
    fclose(fp);
}

/* ─── nftw callback ──────────────────────────────────────────────────────── */

static int nftw_cb(const char *fpath, const struct stat *sb,
                   int typeflag, struct FTW *ftwbuf)
{
    (void)sb; (void)ftwbuf;
    if (typeflag != FTW_F) return 0;
    size_t len = strlen(fpath);
    if (len >= 6 && strcmp(fpath + len - 6, ".jsonl") == 0)
        process_jsonl(fpath);
    return 0;
}

/* ─── token formatting ───────────────────────────────────────────────────── */

static void fmt_tokens(char *buf, int sz, long long n)
{
    if (n < 1000000)
        snprintf(buf, sz, "%.1fk", n / 1000.0);
    else
        snprintf(buf, sz, "%.2fM", n / 1000000.0);
}

/* ─── public entry point ─────────────────────────────────────────────────── */

void print_claude(void)
{
    /* reset */
    memset(&g_stats, 0, sizeof(g_stats));
    g_session_count = 0;

    /* today's date */
    time_t     t  = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(g_today, sizeof(g_today), "%Y-%m-%d", tm);

    /* scan ~/.claude/projects/ */
    const char *home = getenv("HOME");
    if (!home) home = "/root";
    char scan_path[512];
    snprintf(scan_path, sizeof(scan_path), "%s/.claude/projects", home);
    nftw(scan_path, nftw_cb, 16, FTW_PHYS);

    print_section_header("Claude", "claude-sonnet-4-6  (today / local stats)");

    char in_s[32], out_s[32], cr_s[32], cc_s[32];
    fmt_tokens(in_s,  sizeof(in_s),  g_stats.input);
    fmt_tokens(out_s, sizeof(out_s), g_stats.output);
    fmt_tokens(cr_s,  sizeof(cr_s),  g_stats.cache_read);
    fmt_tokens(cc_s,  sizeof(cc_s),  g_stats.cache_create);

    double cost = (g_stats.input        *  3.00 +
                   g_stats.output       * 15.00 +
                   g_stats.cache_read   *  0.30 +
                   g_stats.cache_create *  3.75) / 1000000.0;

    printf("  Sessions %d   Messages %d\n",
           g_session_count, g_stats.messages);
    printf("  Tokens   in %s  out %s  cache-read %s  cache-create %s\n",
           in_s, out_s, cr_s, cc_s);
    printf("  Est. cost  $%.4f USD\n", cost);
}
