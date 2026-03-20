#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hwmon.h"

/* ─── CPU ───────────────────────────────────────────────────────────────── */

void print_cpu(const HwMap *map)
{
    if (map->cpu.count == 0) return;
    const HwEntry *cpu = &map->cpu.entries[0];

    print_section_header("CPU", cpu->name);

    double temp      = sensor_float(cpu, "/temperature/2");
    double power     = sensor_float(cpu, "/power/0");
    double load_tot  = sensor_float(cpu, "/load/0");
    double load_cmax = sensor_float(cpu, "/load/1");
    double clk_avg   = sensor_float(cpu, "/clock/1");
    double clk_eff   = sensor_float(cpu, "/clock/2");

    const char *tc = threshold_color(temp,      70, 85, 0);
    const char *pc = threshold_color(power,     15, 25, 0);
    const char *lc = threshold_color(load_tot,  50, 80, 0);
    const char *cc = threshold_color(load_cmax, 60, 85, 0);

    /* line 1: temp + power */
    printf("  Temp  %s%.1f°C%s   Power  %s%.1fW%s\n",
           tc, temp, C_RESET, pc, power, C_RESET);

    /* line 2: total load bar */
    char suffix[256];
    snprintf(suffix, sizeof(suffix), "  %s%.1f%%%s  CoreMax  %s%.1f%%%s",
             lc, load_tot, C_RESET, cc, load_cmax, C_RESET);
    int bw = g_cols - 8 - 2 - visual_len(suffix);
    if (bw < 8) bw = 8;
    char bar[4096];
    make_bar(bar, sizeof(bar), load_tot, bw, lc);
    printf("  Total %s%s\n", bar, suffix);

    /* line 3: clocks */
    printf("  Clock avg %.0f MHz  eff %.0f MHz\n", clk_avg, clk_eff);

    /* line 4: per-core loads */
    printf("  Cores");
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/load/%d", i + 2);
        double cl  = sensor_float(cpu, path);
        const char *coc = threshold_color(cl, 60, 85, 0);
        printf("  #%d %s%.0f%%%s", i + 1, coc, cl, C_RESET);
    }
    printf("\n");
}

/* ─── GPU ───────────────────────────────────────────────────────────────── */

void print_gpu(const HwMap *map)
{
    if (map->gpu.count == 0) return;
    const HwEntry *gpu = &map->gpu.entries[0];

    print_section_header("GPU", gpu->name);

    double temp   = sensor_float(gpu, "/temperature/4");
    double load   = sensor_float(gpu, "/load/0");
    double power  = sensor_float(gpu, "/power/0");
    double clock  = sensor_float(gpu, "/clock/0");
    double vused  = sensor_float(gpu, "/smalldata/0");
    double vtotal = sensor_float(gpu, "/smalldata/2");
    double vpct   = (vtotal > 0) ? (vused / vtotal * 100.0) : 0.0;

    const char *tc = threshold_color(temp, 70, 85, 0);
    const char *lc = threshold_color(load, 50, 80, 0);
    const char *vc = threshold_color(vpct, 70, 90, 0);

    /* line 1: temp + power + clock */
    printf("  Temp  %s%.1f°C%s   Power  %.1fW   Clock %.0f MHz\n",
           tc, temp, C_RESET, power, clock);

    /* line 2: load bar */
    char suffix[256];
    snprintf(suffix, sizeof(suffix), "  %s%.1f%%%s", lc, load, C_RESET);
    int bw = g_cols - 8 - 2 - visual_len(suffix);
    if (bw < 8) bw = 8;
    char bar[4096];
    make_bar(bar, sizeof(bar), load, bw, lc);
    printf("  Load  %s%s\n", bar, suffix);

    /* line 3: VRAM bar */
    char vsuffix[256];
    snprintf(vsuffix, sizeof(vsuffix), "  %s%.1f%%%s  %.0f/%.0f MB",
             vc, vpct, C_RESET, vused, vtotal);
    bw = g_cols - 8 - 2 - visual_len(vsuffix);
    if (bw < 8) bw = 8;
    make_bar(bar, sizeof(bar), vpct, bw, vc);
    printf("  VRAM  %s%s\n", bar, vsuffix);
}

/* ─── Memory ────────────────────────────────────────────────────────────── */

void print_memory(const HwMap *map)
{
    if (map->ram.count == 0 && map->vram.count == 0) return;
    print_section_header("Memory", NULL);

    if (map->ram.count > 0) {
        const HwEntry *ram  = &map->ram.entries[0];
        double pct   = sensor_float(ram, "/load/0");
        double used  = sensor_float(ram, "/data/0");
        double free_ = sensor_float(ram, "/data/1");
        double total = used + free_;

        const char *rc = threshold_color(pct, 70, 90, 0);
        char suffix[256];
        snprintf(suffix, sizeof(suffix), "  %s%.1f%%%s  %.1f/%.1f GB",
                 rc, pct, C_RESET, used, total);
        int bw = g_cols - 8 - 2 - visual_len(suffix);
        if (bw < 8) bw = 8;
        char bar[4096];
        make_bar(bar, sizeof(bar), pct, bw, rc);
        printf("  RAM   %s%s\n", bar, suffix);
    }

    if (map->vram.count > 0) {
        const HwEntry *vram = &map->vram.entries[0];
        double pct   = sensor_float(vram, "/load/1");
        double used  = sensor_float(vram, "/data/2");
        double free_ = sensor_float(vram, "/data/3");
        double total = used + free_;

        const char *vc = threshold_color(pct, 70, 90, 0);
        char suffix[256];
        snprintf(suffix, sizeof(suffix), "  %s%.1f%%%s  %.1f/%.1f GB",
                 vc, pct, C_RESET, used, total);
        int bw = g_cols - 8 - 2 - visual_len(suffix);
        if (bw < 8) bw = 8;
        char bar[4096];
        make_bar(bar, sizeof(bar), pct, bw, vc);
        printf("  Virt  %s%s\n", bar, suffix);
    }
}

/* ─── NVMe ──────────────────────────────────────────────────────────────── */

void print_nvme(const HwMap *map)
{
    for (int n = 0; n < map->nvme.count; n++) {
        const HwEntry *nv = &map->nvme.entries[n];

        print_section_header("NVMe", nv->name);

        double temp  = sensor_float(nv, "/temperature/0");
        double used  = sensor_float(nv, "/load/30");
        double rfree = sensor_float(nv, "/data/31");
        double life  = sensor_float(nv, "/level/20");
        double ract  = sensor_float(nv, "/load/51");
        double wact  = sensor_float(nv, "/load/52");
        const char *rs = sensor_value(nv, "/throughput/54");
        const char *ws = sensor_value(nv, "/throughput/55");

        const char *tc = threshold_color(temp, 55, 70, 0);
        const char *uc = threshold_color(used, 70, 90, 0);

        /* line 1: temp + life + free */
        printf("  Temp  %s%.1f°C%s   Life %s%.0f%%%s   Free %.1f GB\n",
               tc, temp, C_RESET, C_GREEN, life, C_RESET, rfree);

        /* line 2: used bar */
        char suffix[128];
        snprintf(suffix, sizeof(suffix), "  %s%.1f%%%s", uc, used, C_RESET);
        int bw = g_cols - 8 - 2 - visual_len(suffix);
        if (bw < 8) bw = 8;
        char bar[4096];
        make_bar(bar, sizeof(bar), used, bw, uc);
        printf("  Used  %s%s\n", bar, suffix);

        /* lines 3-4: throughput */
        printf("  %-5s %12s  (%.1f%% active)\n", "Read",  rs ? rs : "", ract);
        printf("  %-5s %12s  (%.1f%% active)\n", "Write", ws ? ws : "", wact);
    }
}

/* ─── Battery ───────────────────────────────────────────────────────────── */

void print_battery(const HwMap *map)
{
    if (map->battery.count == 0) return;
    const HwEntry *bat = &map->battery.entries[0];

    print_section_header("Battery", bat->name);

    double level    = sensor_float(bat, "/level/0");
    double degrad   = sensor_float(bat, "/level/1");
    double voltage  = sensor_float(bat, "/voltage/0");
    double current  = sensor_float(bat, "/current/0");
    double power    = sensor_float(bat, "/power/0");
    double cap_rem  = sensor_float(bat, "/energy/2");
    double cap_full = sensor_float(bat, "/energy/1");

    const char *bc = threshold_color(level, 30, 15, 1); /* reverse */
    double bar_level = level > 100.0 ? 100.0 : level;

    /* line 1: level bar */
    char suffix[256];
    snprintf(suffix, sizeof(suffix), "  %s%.0f%%%s  %.0f/%.0f mWh",
             bc, level, C_RESET, cap_rem, cap_full);
    int bw = g_cols - 2 - 2 - visual_len(suffix);
    if (bw < 8) bw = 8;
    char bar[4096];
    make_bar(bar, sizeof(bar), bar_level, bw, bc);
    printf("  %s%s\n", bar, suffix);

    /* line 2: charging status + voltage + degradation */
    char        charge_str[64];
    const char *charge_col;
    if (current > 0.0) {
        snprintf(charge_str, sizeof(charge_str), "Charging +%.1fW", power);
        charge_col = C_GREEN;
    } else if (current < 0.0) {
        snprintf(charge_str, sizeof(charge_str), "Discharging -%.1fW", power);
        charge_col = C_YELLOW;
    } else {
        snprintf(charge_str, sizeof(charge_str), "AC (idle)");
        charge_col = C_DIM;
    }
    printf("  %s%s%s   %.4fV   " C_YELLOW "Degradation %.1f%%" C_RESET "\n",
           charge_col, charge_str, C_RESET, voltage, degrad);
}

/* ─── Network ───────────────────────────────────────────────────────────── */

void print_network(const HwMap *map)
{
    if (map->nic.count == 0) return;
    print_section_header("Network", NULL);

    for (int n = 0; n < map->nic.count; n++) {
        const HwEntry *nic = &map->nic.entries[n];

        double cum_up   = sensor_float(nic, "/data/2");
        double cum_down = sensor_float(nic, "/data/3");

        /* skip NICs that have never transferred any data */
        if (cum_up == 0.0 && cum_down == 0.0) continue;

        const char *up_s   = sensor_value(nic, "/throughput/7");
        const char *down_s = sensor_value(nic, "/throughput/8");

        /* parse numeric prefix to detect active traffic */
        char *endp;
        double up_val   = (*up_s)   ? strtod(up_s,   &endp) : 0.0;
        double down_val = (*down_s) ? strtod(down_s, &endp) : 0.0;
        (void)endp;

        const char *arrow_col = (up_val + down_val > 0.0) ? C_GREEN : C_DIM;

        printf("  %-14s  %s↑%s %12s   %s↓%s %12s"
               "  " C_DIM "(total ↑%.2fG ↓%.2fG)" C_RESET "\n",
               nic->name,
               arrow_col, C_RESET, up_s,
               arrow_col, C_RESET, down_s,
               cum_up, cum_down);
    }
}
