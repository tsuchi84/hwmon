#pragma once

#include <stddef.h>

/* ─── data model ─────────────────────────────────────────────────────────── */

#define MAX_SENSORS     256
#define MAX_HW_ENTRIES   32

typedef struct {
    char path[128];   /* relative sensor path, e.g. "/temperature/2" */
    char value[64];   /* raw value string from LHM,  e.g. "65.3 °C"  */
} Sensor;

typedef struct {
    char   name[256];
    char   hw_id[128];
    Sensor sensors[MAX_SENSORS];
    int    sensor_count;
} HwEntry;

typedef struct {
    HwEntry entries[MAX_HW_ENTRIES];
    int     count;
} HwList;

typedef struct {
    HwList cpu, gpu, nvme, battery, nic, ram, vram;
} HwMap;

/* ─── ANSI colors ────────────────────────────────────────────────────────── */

#define C_RESET   "\033[0m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_DIM     "\033[2m"
#define C_BOLD    "\033[1m"

/* ─── globals ────────────────────────────────────────────────────────────── */

extern int g_cols;

/* ─── utilities (hwmon.c) ────────────────────────────────────────────────── */

const char *sensor_value(const HwEntry *e, const char *path);
double      sensor_float(const HwEntry *e, const char *path);
const char *threshold_color(double val, double warn, double crit, int reverse);
int         visual_len(const char *s);
void        make_bar(char *buf, int buf_size, double pct, int bar_width,
                     const char *color);
void        print_separator(void);
void        print_section_header(const char *label, const char *name);

/* ─── display sections ───────────────────────────────────────────────────── */

void print_cpu(const HwMap *map);
void print_gpu(const HwMap *map);
void print_memory(const HwMap *map);
void print_nvme(const HwMap *map);
void print_battery(const HwMap *map);
void print_network(const HwMap *map);
void print_claude(void);
