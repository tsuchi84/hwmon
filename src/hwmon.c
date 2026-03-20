#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <curl/curl.h>

#include "cJSON.h"
#include "hwmon.h"

int g_cols = 80;

/* ─── HTTP fetch ─────────────────────────────────────────────────────────── */

typedef struct { char *data; size_t size; } MemBuf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    MemBuf *buf = ud;
    size_t  n   = size * nmemb;
    char   *tmp = realloc(buf->data, buf->size + n + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->size, ptr, n);
    buf->size += n;
    buf->data[buf->size] = '\0';
    return n;
}

static char *fetch_url(const char *url)
{
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    MemBuf buf = { NULL, 0 };
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,      1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

/* ─── sensor helpers ─────────────────────────────────────────────────────── */

const char *sensor_value(const HwEntry *e, const char *path)
{
    for (int i = 0; i < e->sensor_count; i++)
        if (strcmp(e->sensors[i].path, path) == 0)
            return e->sensors[i].value;
    return "";
}

double sensor_float(const HwEntry *e, const char *path)
{
    const char *v = sensor_value(e, path);
    if (!v || !*v) return 0.0;
    char *end;
    double d = strtod(v, &end);
    return (end == v) ? 0.0 : d;
}

/* ─── display utilities ──────────────────────────────────────────────────── */

const char *threshold_color(double val, double warn, double crit, int reverse)
{
    if (!reverse) {
        if (val >= crit) return C_RED;
        if (val >= warn) return C_YELLOW;
        return C_GREEN;
    } else {
        /* lower is worse; e.g. battery: warn=30, crit=15 */
        if (val >= warn) return C_GREEN;
        if (val >= crit) return C_YELLOW;
        return C_RED;
    }
}

/* strip ANSI escapes, count UTF-8 leading bytes (= display columns) */
int visual_len(const char *s)
{
    int len = 0, in_esc = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\033')            { in_esc = 1; continue; }
        if (in_esc && *s == 'm')   { in_esc = 0; continue; }
        if (in_esc)                  continue;
        if ((c & 0xC0) != 0x80) len++;   /* not a UTF-8 continuation byte */
    }
    return len;
}

void make_bar(char *buf, int buf_size, double pct, int bar_width,
              const char *color)
{
    if (bar_width < 1) bar_width = 1;
    int fill = (int)floor(pct / 100.0 * bar_width);
    if (fill > bar_width) fill = bar_width;
    if (fill < 0)         fill = 0;

    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[%s", color);
    for (int i = 0; i < fill && pos < buf_size - 8; i++)
        pos += snprintf(buf + pos, buf_size - pos, "█");
    pos += snprintf(buf + pos, buf_size - pos, "%s", C_RESET);
    for (int i = fill; i < bar_width && pos < buf_size - 8; i++)
        pos += snprintf(buf + pos, buf_size - pos, "░");
    snprintf(buf + pos, buf_size - pos, "]");
}

void print_separator(void)
{
    printf(C_DIM);
    for (int i = 0; i < g_cols; i++) printf("═");
    printf(C_RESET "\n");
}

void print_section_header(const char *label, const char *name)
{
    if (name && *name)
        printf(C_BOLD "▌ %s  %s" C_RESET "\n", label, name);
    else
        printf(C_BOLD "▌ %s" C_RESET "\n", label);
}

/* ─── tree walk ──────────────────────────────────────────────────────────── */

static const char *hw_type(const char *image_url, const char *hw_id)
{
    if (!image_url || !*image_url) return NULL;
    const char *slash = strrchr(image_url, '/');
    const char *fname = slash ? slash + 1 : image_url;

    if (strcmp(fname, "cpu.png")     == 0) return "cpu";
    if (strcmp(fname, "ati.png")     == 0) return "gpu";
    if (strcmp(fname, "nvidia.png")  == 0) return "gpu";
    if (strcmp(fname, "hdd.png")     == 0) return "nvme";
    if (strcmp(fname, "battery.png") == 0) return "battery";
    if (strcmp(fname, "nic.png")     == 0) return "nic";
    if (strcmp(fname, "ram.png")     == 0) {
        if (hw_id && strncmp(hw_id, "/vram", 5) == 0) return "vram";
        return "ram";
    }
    return NULL;
}

static HwList *list_for(HwMap *map, const char *type)
{
    if (!type)                       return NULL;
    if (strcmp(type, "cpu")     == 0) return &map->cpu;
    if (strcmp(type, "gpu")     == 0) return &map->gpu;
    if (strcmp(type, "nvme")    == 0) return &map->nvme;
    if (strcmp(type, "battery") == 0) return &map->battery;
    if (strcmp(type, "nic")     == 0) return &map->nic;
    if (strcmp(type, "ram")     == 0) return &map->ram;
    if (strcmp(type, "vram")    == 0) return &map->vram;
    return NULL;
}

static void walk(cJSON *node, HwMap *map, HwEntry *parent_hw)
{
    if (!node) return;

    const char *hw_id_s    = "";
    const char *sensor_id_s = "";
    const char *text_s     = "";
    const char *image_s    = "";
    const char *value_s    = "";
    cJSON *j;

#define GET_STR(field, var) \
    if ((j = cJSON_GetObjectItemCaseSensitive(node, field)) && cJSON_IsString(j)) \
        var = j->valuestring;

    GET_STR("HardwareId", hw_id_s)
    GET_STR("SensorId",   sensor_id_s)
    GET_STR("Text",       text_s)
    GET_STR("ImageURL",   image_s)
    GET_STR("Value",      value_s)
#undef GET_STR

    HwEntry *cur_hw = parent_hw;

    if (*hw_id_s) {
        /* hardware node */
        const char *type = hw_type(image_s, hw_id_s);
        HwList     *list = list_for(map, type);
        if (list && list->count < MAX_HW_ENTRIES) {
            HwEntry *e = &list->entries[list->count++];
            memset(e, 0, sizeof(*e));
            strncpy(e->name,  text_s,  sizeof(e->name)  - 1);
            strncpy(e->hw_id, hw_id_s, sizeof(e->hw_id) - 1);
            cur_hw = e;
        }
        /* unrecognized type: cur_hw inherits parent_hw per spec */

    } else if (*sensor_id_s && parent_hw) {
        /* sensor node */
        if (parent_hw->sensor_count < MAX_SENSORS) {
            Sensor     *s    = &parent_hw->sensors[parent_hw->sensor_count++];
            const char *rel  = sensor_id_s;
            size_t      plen = strlen(parent_hw->hw_id);
            if (plen > 0 && strncmp(sensor_id_s, parent_hw->hw_id, plen) == 0)
                rel = sensor_id_s + plen;
            strncpy(s->path,  rel,     sizeof(s->path)  - 1);
            strncpy(s->value, value_s, sizeof(s->value) - 1);
        }
    }
    /* else: group/root node — inherit cur_hw */

    cJSON *children = cJSON_GetObjectItemCaseSensitive(node, "Children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children)
            walk(child, map, cur_hw);
    }
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    /* terminal width */
    const char *cols_env = getenv("COLUMNS");
    if (cols_env) {
        int c = atoi(cols_env);
        if (c > 0) g_cols = c;
    } else {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            g_cols = ws.ws_col;
    }

    /* URL */
    const char *url = getenv("HWMON_URL");
    if (!url) url = "http://localhost:8085/data.json";

    /* fetch */
    char *json_str = fetch_url(url);
    if (!json_str) {
        fprintf(stderr, C_RED "Error: failed to fetch %s" C_RESET "\n", url);
        return 1;
    }

    /* parse */
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        fprintf(stderr, C_RED "Error: failed to parse JSON" C_RESET "\n");
        return 1;
    }

    /* machine name from root's first child */
    const char *machine_name = "Unknown";
    cJSON *root_ch = cJSON_GetObjectItemCaseSensitive(root, "Children");
    cJSON *first   = root_ch ? cJSON_GetArrayItem(root_ch, 0) : NULL;
    if (first) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(first, "Text");
        if (t && cJSON_IsString(t)) machine_name = t->valuestring;
    }

    /* build hardware map — static to avoid ~10 MB stack allocation */
    static HwMap map;
    memset(&map, 0, sizeof(map));
    if (first) {
        cJSON *ch = cJSON_GetObjectItemCaseSensitive(first, "Children");
        if (ch && cJSON_IsArray(ch)) {
            cJSON *child;
            cJSON_ArrayForEach(child, ch)
                walk(child, &map, NULL);
        }
    }

    /* header */
    time_t     t       = time(NULL);
    struct tm *tm_info = localtime(&t);
    char       ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    print_separator();
    printf("  hwmon  %s  %s\n", machine_name, ts);
    print_separator();

    /* sections */
    print_cpu(&map);
    print_gpu(&map);
    print_memory(&map);
    print_nvme(&map);
    print_battery(&map);
    print_network(&map);
    print_claude();

    /* footer */
    print_separator();

    cJSON_Delete(root);
    return 0;
}
