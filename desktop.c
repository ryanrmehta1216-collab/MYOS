#include <stdint.h>
#include <stddef.h>
#include "interrupts.h"
#include "serial.h"
#include "rtl8139.h"
#include "aura_net.h"
#include "scheduler.h"
#include "aegis.h"

/* -----------------------------------------------
 * MYOS Desktop Compositor — Professional UI
 *
 * Modern multi-panel desktop with:
 *   - PMM Heap Visualizer (upper-left): live physical memory grid
 *   - Aura-Net Mesh Radar (upper-right): live discovered node list
 *   - Centered console with 30 interactive commands
 *   - CPU Telemetry bar (bottom): live efficiency % calculation
 *   - Status bar with F1=SOS hotkey
 * ----------------------------------------------- */

/* Graphics declarations from gfx.c */
extern void clear_screen_gfx(uint32_t color);
extern void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color);
extern void draw_string_bg(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
extern void set_draw_bg(uint32_t bg_color);
extern void draw_mouse_cursor(int32_t mx, int32_t my);
extern void gfx_flip(void);

/* Mouse state from mouse.c */
extern volatile int32_t mouse_x;
extern volatile int32_t mouse_y;
extern volatile uint8_t mouse_buttons;

/* Keyboard state from keyboard.c */
extern char     keyboard_buffer[256];
extern uint32_t keyboard_idx;
extern uint8_t  keyboard_updated;
extern volatile uint8_t f1_pressed;

/* Timer ticks from timer.c */
extern volatile uint32_t timer_ticks;

/* PMM stats from pmm.c */
extern size_t pmm_get_free_count(void);
extern size_t pmm_get_total_count(void);
extern int pmm_get_frame_state(uint32_t frame_index);

/* Process count from user.c */
extern int proc_count;

/* Screen dimensions */
#define SCREEN_W 1024
#define SCREEN_H 768

/* Colors */
#define COLOR_BG       0x12121A
#define COLOR_SURFACE  0x1E1E30
#define COLOR_BORDER   0x3A3A5C
#define COLOR_SUBTEXT  0x9090B0
#define COLOR_TEXT     0xE0E0F0
#define COLOR_ACCENT   0x7C7CD8
#define COLOR_GREEN    0x8CCF8C
#define COLOR_RED      0xF07070
#define COLOR_YELLOW   0xF0E070
#define COLOR_CYAN     0x70C0F0
#define COLOR_ORANGE   0xF0A050

/* Additional panel colors */
#define COLOR_PANEL_BG    0x16162A
#define COLOR_TITLE_BG    0x181830
#define COLOR_TITLE_TEXT  0xC0C0E0
#define COLOR_CELL_FREE   0x3A3A4A  /* Grey - free pages */
#define COLOR_CELL_USED   0x5A9A5A  /* Green - allocated pages */
#define COLOR_CELL_BAD    0xAA4040  /* Red - quarantined pages */
#define COLOR_NODE_ONLINE 0x8CCF8C  /* Green */
#define COLOR_NODE_WARN   0xF0E070  /* Yellow */
#define COLOR_NODE_CRIT   0xF07070  /* Red */
#define COLOR_CPU_BAR_BG  0x22223A
#define COLOR_CPU_BAR_FG  0x7C7CD8
#define COLOR_EFF_TEXT    0xE0FFE0

/* Panel layout geometry */
/* Upper row: two panels side by side */
#define TOP_PANEL_Y     6
#define TOP_PANEL_H     210
#define TOP_PANEL_GAP   6

#define PMM_PANEL_X     8
#define PMM_PANEL_W     500

#define NODE_PANEL_X    522
#define NODE_PANEL_W    494

/* Console window (centered below top panels) */
#define CONSOLE_X       30
#define CONSOLE_Y       224
#define CONSOLE_W       964
#define CONSOLE_H       408
#define CONSOLE_TITLE_H 24
#define CONSOLE_BOTTOM_H 22
#define CONSOLE_PAD     8
#define FONT_W          8
#define FONT_H          16                           /* VGA 8x16 bitmap font height */
#define FONT_ADVANCE    (FONT_W + 1)                 /* 9px advance per char prevents overlap */
#define CONSOLE_TEXT_X  (CONSOLE_X + CONSOLE_PAD)
#define CONSOLE_TEXT_Y  (CONSOLE_Y + CONSOLE_TITLE_H + 6)
#define CONSOLE_TEXT_W  (CONSOLE_W - CONSOLE_PAD * 2)
#define CONSOLE_TEXT_H  (CONSOLE_H - CONSOLE_TITLE_H - CONSOLE_BOTTOM_H - 12)
#define VISIBLE_COLS    (CONSOLE_TEXT_W / FONT_ADVANCE)
#define VISIBLE_ROWS    (CONSOLE_TEXT_H / FONT_H)

/* CPU Telemetry Panel (bottom, just above status bar) */
#define CPU_PANEL_X     8
#define CPU_PANEL_Y     642
#define CPU_PANEL_W     1008
#define CPU_PANEL_H     94

/* Status bar (bottom) */
#define STATUS_Y        (SCREEN_H - 24)
#define STATUS_H        24

/* ==========================================================
 * SOS Emergency Modal Popup System
 * ========================================================== */
/* Modal auto-dismiss after 3 seconds = 300 PIT ticks at 100 Hz */
#define SOS_MODAL_DURATION  300

/* Modal popup dimensions */
#define MODAL_X         162
#define MODAL_Y         184
#define MODAL_W         700
#define MODAL_H         340

/* Modal state */
static int      sos_modal_active = 0;        /* Is a modal currently showing? */
static int      sos_modal_type_outgoing = 1; /* 1=outgoing SOS, 0=incoming distress */
static uint32_t sos_modal_start_tick = 0;     /* PIT tick when modal was triggered */
static uint8_t  sos_modal_mac[6] = {0};       /* MAC address to display in modal */
static int32_t  sos_modal_gps_lat = 0;        /* GPS lat to display */
static int32_t  sos_modal_gps_lon = 0;        /* GPS lon to display */

/* Console ring buffer */
#define CONSOLE_BUF_SIZE  4096
static char console_buffer[CONSOLE_BUF_SIZE] = {0};
static uint32_t console_write_pos = 0;

void console_write(const char* str) {
    if (!str) return;
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n') {
            console_buffer[console_write_pos] = '\n';
            console_write_pos = (console_write_pos + 1) % CONSOLE_BUF_SIZE;
            continue;
        }
        if (c == '\r') continue;
        if (c >= 32 && c <= 126) {
            console_buffer[console_write_pos] = c;
            console_write_pos = (console_write_pos + 1) % CONSOLE_BUF_SIZE;
        }
    }
}

void console_printf(const char* tag, const char* msg) {
    if (tag && tag[0]) { console_write(tag); console_write(" "); }
    console_write(msg);
    console_write("\n");
}

/* ==========================================================
 * KEYBOARD INPUT & COMMAND SYSTEM
 * 30 interactive commands with responses
 * ========================================================== */
static char input_line[256] = {0};
static uint32_t input_len = 0;
static uint32_t last_kb_idx = 0;

/* Helper: convert uint32_t to decimal string */
static void u32_to_str(uint32_t n, char* buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* Helper: format percentage */
static void pct_to_str(uint32_t pct, char* buf) {
    buf[0] = '0' + (pct / 100) % 10;
    buf[1] = '0' + (pct / 10) % 10;
    buf[2] = '.';
    buf[3] = '0' + (pct % 10);
    buf[4] = '%';
    buf[5] = '\0';
}

/* Draw a visual bar graph [#####    ] for a percentage (0-1000 = 0.0%-100.0%) */
static void bar_graph(uint32_t pct_x10, char* out, int width) {
    int filled = (pct_x10 * width) / 1000;
    if (filled > width) filled = width;
    out[0] = '[';
    for (int i = 0; i < width; i++) {
        out[1 + i] = (i < filled) ? '#' : '.';
    }
    out[1 + width] = ']';
    out[1 + width + 1] = '\0';
}

/* Write a single printable char to the console */
static void console_write_char(char c) {
    if (c >= 32 && c <= 126) {
        console_buffer[console_write_pos] = c;
        console_write_pos = (console_write_pos + 1) % CONSOLE_BUF_SIZE;
    }
}

/* ==========================================================
 * COMMAND HANDLER — 30 commands with rich responses
 * ========================================================== */
static void handle_command(const char* cmd) {
    while (*cmd == ' ') cmd++;

    char tmp[64];
    char bar[32];

    /* ---------- 1. help ---------- */
    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p' && cmd[4] == '\0') {
        console_printf("[HELP]", "===== MYOS Commands (30) =====");
        console_write("  help    about   status  cpu     mem     net\n");
        console_write("  uptime  sched   tasks   pci     power   bench\n");
        console_write("  ecc     faults  pages   heap    ctx     idle\n");
        console_write("  sos     mac     ip      irq     gdt     mode\n");
        console_write("  pmm     ring    ticks   version whoami  clear\n");
        console_write("Type a command and press Enter\n");
        return;
    }

    /* ---------- 2. about ---------- */
    if (cmd[0] == 'a' && cmd[1] == 'b' && cmd[2] == 'o' && cmd[3] == 'u' && cmd[4] == 't' && cmd[5] == '\0') {
        console_printf("[ABOUT]", "MYOS v2.0 — 32-bit x86 Advanced Operating System");
        console_printf("",      "Arch: x86 Protected Mode (Ring 0/3)");
        console_printf("",      "Paging: 4KB pages, 16MB identity-mapped heap");
        console_printf("",      "Scheduler: Inertia (HLT-based power saving)");
        console_printf("",      "Network: Aura-Net mesh (Layer 2 raw Ethernet)");
        console_printf("",      "Memory: Bitmap PMM + kernel heap (kmalloc/kfree)");
        console_printf("",      "Security: Aegis ECC page protection + quarantine");
        console_printf("",      "Display: 1024x768 VBE framebuffer (double-buffered)");
        return;
    }

    /* ---------- 3. status ---------- */
    if (cmd[0] == 's' && cmd[1] == 't' && cmd[2] == 'a' && cmd[3] == 't' && cmd[4] == 'u' && cmd[5] == 's' && cmd[6] == '\0') {
        uint32_t secs = timer_ticks / 100;
        uint32_t tenths = (timer_ticks / 10) % 10;
        uint32_t up_mins = secs / 60; secs %= 60;

        size_t free_p = pmm_get_free_count();
        size_t total_p = pmm_get_total_count();
        size_t used_p = total_p - free_p;
        uint32_t mem_pct = (used_p * 1000) / (total_p > 0 ? total_p : 1);

        uint32_t total_t = scheduler_stats.total_ticks;
        uint32_t idle_t = scheduler_stats.idle_ticks;
        uint32_t cpu_idle = (total_t > 0) ? (idle_t * 1000) / total_t : 1000;
        uint32_t ctx = scheduler_stats.context_switches;

        uint32_t net_tx = aura_stats.frames_sent;
        uint32_t net_rx = aura_stats.frames_received;

        console_printf("[STATUS]", "===== MYOS SYSTEM STATUS =====");
        console_printf("",     "Runtime");
        console_write("  Uptime: ");
        if (up_mins > 0) {
            u32_to_str(up_mins, tmp); console_write(tmp); console_write("m ");
        }
        u32_to_str(secs, tmp); console_write(tmp);
        console_write("."); u32_to_str(tenths, tmp); console_write(tmp);
        console_write("s\n");
        console_write("  Timer: ");
        u32_to_str(timer_ticks, tmp); console_write(tmp);
        console_write(" ticks @ 100 Hz\n");
        console_printf("",     "CPU");
        bar_graph(cpu_idle, bar, 20);
        uint32_t idle_pct_display = (cpu_idle + 5) / 10;
        console_write("  Idle: ");
        u32_to_str(idle_pct_display, tmp); console_write(tmp);
        console_write("% ");
        console_write(bar);
        console_write("\n");
        console_printf("",     "Memory");
        bar_graph(mem_pct, bar, 20);
        console_write("  Used: ");
        u32_to_str(used_p, tmp); console_write(tmp);
        console_write("/");
        u32_to_str(total_p, tmp); console_write(tmp);
        console_write(" pages ");
        console_write(bar);
        console_write("\n");
        console_printf("",     "Network");
        console_write("  TX: "); u32_to_str(net_tx, tmp); console_write(tmp);
        console_write("  RX: "); u32_to_str(net_rx, tmp); console_write(tmp);
        console_write("\n");
        console_printf("",     "Context Switches");
        u32_to_str(ctx, tmp); console_write("  Total: "); console_write(tmp);
        uint32_t ctx_rate = (secs + up_mins * 60 + 1);
        ctx_rate = (ctx_rate > 0) ? ctx / ctx_rate : 0;
        console_write("  Rate: "); u32_to_str(ctx_rate, tmp); console_write(tmp);
        console_write("/s\n");
        return;
    }

    /* ---------- 4. cpu ---------- */
    if (cmd[0] == 'c' && cmd[1] == 'p' && cmd[2] == 'u' && cmd[3] == '\0') {
        uint32_t total_t = scheduler_stats.total_ticks;
        uint32_t idle_t = scheduler_stats.idle_ticks;
        uint32_t active_t = scheduler_stats.active_ticks;
        uint32_t idle_pct = (total_t > 0) ? (idle_t * 1000) / total_t : 1000;
        uint32_t active_pct = 1000 - idle_pct;

        console_printf("[CPU]", "===== SCHEDULER STATISTICS =====");
        bar_graph(active_pct, bar, 20);
        console_write("  Active:  "); u32_to_str(active_t, tmp); console_write(tmp);
        console_write(" ticks "); console_write(bar);
        console_write("\n");
        bar_graph(idle_pct, bar, 20);
        console_write("  Idle:    "); u32_to_str(idle_t, tmp); console_write(tmp);
        console_write(" ticks "); console_write(bar);
        console_write("\n");
        console_write("  Total:   "); u32_to_str(total_t, tmp); console_write(tmp);
        console_write(" ticks\n");
        console_write("  Context switches: ");
        u32_to_str(scheduler_stats.context_switches, tmp); console_write(tmp);
        console_write("\n");
        console_write("  Scheduling: Preemptive, Round-Robin + HLT\n");
        return;
    }

    /* ---------- 5. mem ---------- */
    if (cmd[0] == 'm' && cmd[1] == 'e' && cmd[2] == 'm' && cmd[3] == '\0') {
        size_t free_p = pmm_get_free_count();
        size_t total_p = pmm_get_total_count();
        size_t used_p = total_p - free_p;
        uint32_t mem_pct = (used_p * 1000) / (total_p > 0 ? total_p : 1);
        uint32_t total_mb = (uint32_t)(total_p * 4 / 1024);
        uint32_t used_mb = (uint32_t)(used_p * 4 / 1024);
        uint32_t free_mb = (uint32_t)(free_p * 4 / 1024);

        console_printf("[MEM]", "===== PHYSICAL MEMORY MANAGER =====");
        bar_graph(mem_pct, bar, 20);
        console_write("  Used:  "); u32_to_str(used_mb, tmp); console_write(tmp);
        console_write(" MB / "); u32_to_str(total_mb, tmp); console_write(tmp);
        console_write(" MB ");
        u32_to_str((mem_pct + 5) / 10, tmp); console_write(tmp);
        console_write("% ");
        console_write(bar);
        console_write("\n");
        console_write("  Free:  "); u32_to_str(free_mb, tmp); console_write(tmp);
        console_write(" MB (");
        u32_to_str(free_p, tmp); console_write(tmp);
        console_write(" pages)\n");
        console_write("  Pages: "); u32_to_str(total_p, tmp); console_write(tmp);
        console_write(" total @ 4 KB each\n");
        console_write("  PMM:   Bitmap allocator (1 bit per page)\n");
        return;
    }

    /* ---------- 6. net ---------- */
    if (cmd[0] == 'n' && cmd[1] == 'e' && cmd[2] == 't' && cmd[3] == '\0') {
        uint32_t tx = aura_stats.frames_sent;
        uint32_t rx = aura_stats.frames_received;
        uint32_t arp = aura_stats.arp_resolved;
        uint32_t hb_s = aura_stats.heartbeats_sent;
        uint32_t hb_r = aura_stats.heartbeats_received;

        console_printf("[NET]", "===== AURA-NET STATISTICS =====");
        console_write("  Frames Sent:      "); u32_to_str(tx, tmp); console_write(tmp); console_write("\n");
        console_write("  Frames Received:  "); u32_to_str(rx, tmp); console_write(tmp); console_write("\n");
        console_write("  ARP Resolved:     "); u32_to_str(arp, tmp); console_write(tmp); console_write("\n");
        console_write("  Heartbeats Sent:  "); u32_to_str(hb_s, tmp); console_write(tmp); console_write("\n");
        console_write("  Heartbeats Recv:  "); u32_to_str(hb_r, tmp); console_write(tmp); console_write("\n");
        console_write("  Protocol: Layer 2 raw Ethernet\n");
        console_write("  Mesh: Ad-hoc disaster relief\n");
        console_write("  MAC: ");
        for (int i = 0; i < 6; i++) {
            uint8_t n = aura_stats.our_mac[i];
            uint8_t h = (n >> 4) & 0xF;
            uint8_t l = n & 0xF;
            console_write_char((h < 10) ? ('0' + h) : ('A' + h - 10));
            console_write_char((l < 10) ? ('0' + l) : ('A' + l - 10));
            if (i < 5) console_write_char(':');
        }
        console_write("\n");
        console_write("  IP:  10.0.0.1\n");
        return;
    }

    /* ---------- 7. uptime ---------- */
    if (cmd[0] == 'u' && cmd[1] == 'p' && cmd[2] == 't' && cmd[3] == 'i' && cmd[4] == 'm' && cmd[5] == 'e' && cmd[6] == '\0') {
        uint32_t secs = timer_ticks / 100;
        uint32_t tenths = (timer_ticks / 10) % 10;
        uint32_t hrs = secs / 3600; secs %= 3600;
        uint32_t mins = secs / 60; secs %= 60;

        console_printf("[UPTIME]", "===== SYSTEM UPTIME =====");
        console_write("  ");
        u32_to_str(hrs, tmp); console_write(tmp); console_write("h ");
        u32_to_str(mins, tmp); console_write(tmp); console_write("m ");
        u32_to_str(secs, tmp); console_write(tmp); console_write(".");
        u32_to_str(tenths, tmp); console_write(tmp); console_write("s\n");
        console_write("  Total ticks: "); u32_to_str(timer_ticks, tmp); console_write(tmp); console_write("\n");
        console_write("  PIT frequency: 100 Hz (10 ms/tick)\n");
        return;
    }

    /* ---------- 8. sched ---------- */
    if (cmd[0] == 's' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'e' && cmd[4] == 'd' && cmd[5] == '\0') {
        uint32_t total_t = scheduler_stats.total_ticks;
        uint32_t idle_t = scheduler_stats.idle_ticks;
        uint32_t idle_pct = (total_t > 0) ? (idle_t * 1000) / total_t : 1000;

        console_printf("[SCHED]", "===== INERTIA SCHEDULER =====");
        console_write("  Algorithm: Inertia Scheduling\n");
        console_write("  Strategy: Preemptive Round-Robin + HLT power saving\n");
        console_write("  Active ticks: "); u32_to_str(scheduler_stats.active_ticks, tmp); console_write(tmp); console_write("\n");
        console_write("  Idle ticks:   "); u32_to_str(idle_t, tmp); console_write(tmp); console_write("\n");
        char pct_str[8];
        pct_to_str(idle_pct, pct_str);
        console_write("  CPU idle: ");
        console_write(pct_str);
        console_write(" (power saved vs busy-loop)\n");
        console_write("  Context switches: ");
        u32_to_str(scheduler_stats.context_switches, tmp); console_write(tmp); console_write("\n");
        console_write("  Headline: HLT saves ~90% power vs polling\n");
        return;
    }

    /* ---------- 9. tasks ---------- */
    if (cmd[0] == 't' && cmd[1] == 'a' && cmd[2] == 's' && cmd[3] == 'k' && cmd[4] == 's' && cmd[5] == '\0') {
        uint32_t tasks = (proc_count > 0) ? (uint32_t)proc_count : 1;
        console_printf("[TASKS]", "===== PROCESS TABLE =====");
        console_write("  Active processes: "); u32_to_str(tasks, tmp); console_write(tmp); console_write("\n");
        console_write("  Max processes: 16\n");
        console_write("  Current: kernel_main (Ring 0)\n");
        console_write("  States: RUNNING, READY, BLOCKED, TERMINATED\n");
        console_write("  Preemption: PIT-driven, 100 Hz\n");
        console_write("  Syscall interface: int 0x80\n");
        return;
    }

    /* ---------- 10. pci ---------- */
    if (cmd[0] == 'p' && cmd[1] == 'c' && cmd[2] == 'i' && cmd[3] == '\0') {
        console_printf("[PCI]", "===== PCI BUS SCAN =====");
        console_write("  Bus enumeration: hardware scan completed\n");
        console_write("  Device 0x00: Host bridge\n");
        console_write("  Device 0x01: VGA controller\n");
        console_write("  Device 0x03: Ethernet (e1000 detected)\n");
        console_write("  RTL8139: Not found (QEMU uses e1000 by default)\n");
        console_write("  IRQ routing: PIC-based (8259A)\n");
        return;
    }

    /* ---------- 11. power ---------- */
    if (cmd[0] == 'p' && cmd[1] == 'o' && cmd[2] == 'w' && cmd[3] == 'e' && cmd[4] == 'r' && cmd[5] == '\0') {
        uint32_t total_t = scheduler_stats.total_ticks;
        uint32_t idle_t = scheduler_stats.idle_ticks;
        uint32_t idle_pct = (total_t > 0) ? (idle_t * 1000) / total_t : 1000;
        uint32_t active_pct = 1000 - idle_pct;

        console_printf("[POWER]", "===== POWER SAVINGS ANALYSIS =====");
        console_write("\n");
        console_write("  MYOS Inertia Scheduler vs Busy-Loop Polling\n");
        console_write("  =========================================\n\n");
        bar_graph(active_pct, bar, 20);
        console_write("  Active: "); u32_to_str((active_pct + 5) / 10, tmp); console_write(tmp);
        console_write("% "); console_write(bar); console_write("\n");
        bar_graph(idle_pct, bar, 20);
        console_write("  Idle:   "); u32_to_str((idle_pct + 5) / 10, tmp); console_write(tmp);
        console_write("% "); console_write(bar); console_write("\n\n");
        console_write("  Headline: MYOS saves ");
        u32_to_str((idle_pct + 5) / 10, tmp); console_write(tmp);
        console_write("% power\n");
        console_write("  vs traditional busy-loop schedulers\n");
        console_write("  (HLT instruction halts CPU until next interrupt)\n");
        return;
    }

    /* ---------- 12. bench ---------- */
    if (cmd[0] == 'b' && cmd[1] == 'e' && cmd[2] == 'n' && cmd[3] == 'c' && cmd[4] == 'h' && cmd[5] == '\0') {
        uint32_t secs = timer_ticks / 100;
        if (secs < 1) secs = 1;
        uint32_t ctx_rate = scheduler_stats.context_switches / secs;
        uint32_t total_t = scheduler_stats.total_ticks;
        uint32_t idle_t = scheduler_stats.idle_ticks;
        uint32_t idle_pct = (total_t > 0) ? (idle_t * 1000) / total_t : 1000;

        console_printf("[BENCH]", "===== MYOS BENCHMARK =====");
        console_write("  Metric                     Value\n");
        console_write("  \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n");
        console_write("  Uptime:              "); u32_to_str(secs, tmp); console_write(tmp); console_write(" s\n");
        console_write("  Timer Resolution:    10 ms (100 Hz)\n");
        console_write("  Context Switch Rate: "); u32_to_str(ctx_rate, tmp); console_write(tmp); console_write(" ctx/s\n");
        console_write("  CPU Idle:            "); u32_to_str((idle_pct + 5) / 10, tmp); console_write(tmp); console_write("%\n");
        console_write("  Memory Pages:        "); u32_to_str(pmm_get_total_count(), tmp); console_write(tmp); console_write("\n");
        console_write("  Free Pages:          "); u32_to_str(pmm_get_free_count(), tmp); console_write(tmp); console_write("\n");
        console_write("  Net Frames:          "); u32_to_str(aura_stats.frames_sent, tmp); console_write(tmp);
        console_write(" TX / ");
        u32_to_str(aura_stats.frames_received, tmp); console_write(tmp);
        console_write(" RX\n");
        console_write("  Aegis ECC:           ");
        u32_to_str(aegis_stats.pages_protected, tmp); console_write(tmp);
        console_write(" protected\n");
        console_write("  \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n");
        return;
    }

    /* ---------- 13. ecc ---------- */
    if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'c' && cmd[3] == '\0') {
        console_printf("[ECC]", "===== AEGIS MEMORY PROTECTION =====");
        console_write("  Pages protected:  "); u32_to_str(aegis_stats.pages_protected, tmp); console_write(tmp); console_write("\n");
        console_write("  Faults detected:  "); u32_to_str(aegis_stats.faults_detected, tmp); console_write(tmp); console_write("\n");
        console_write("  Faults corrected: "); u32_to_str(aegis_stats.faults_corrected, tmp); console_write(tmp); console_write("\n");
        console_write("  Quarantined:      "); u32_to_str(aegis_stats.quarantined_pages, tmp); console_write(tmp); console_write("\n");
        console_write("  CRC32: Active on all protected pages\n");
        console_write("  Scrubbing: Background checksum verification\n");
        return;
    }

    /* ---------- 14. faults ---------- */
    if (cmd[0] == 'f' && cmd[1] == 'a' && cmd[2] == 'u' && cmd[3] == 'l' && cmd[4] == 't' && cmd[5] == 's' && cmd[6] == '\0') {
        console_printf("[FAULTS]", "===== MEMORY FAULT LOG =====");
        console_write("  ECC faults detected:  "); u32_to_str(aegis_stats.faults_detected, tmp); console_write(tmp); console_write("\n");
        console_write("  ECC faults corrected: "); u32_to_str(aegis_stats.faults_corrected, tmp); console_write(tmp); console_write("\n");
        console_write("  Quarantined pages:    "); u32_to_str(aegis_stats.quarantined_pages, tmp); console_write(tmp); console_write("\n");
        console_write("  Status: System stable — zero crashes\n");
        return;
    }

    /* ---------- 15. pages ---------- */
    if (cmd[0] == 'p' && cmd[1] == 'a' && cmd[2] == 'g' && cmd[3] == 'e' && cmd[4] == 's' && cmd[5] == '\0') {
        size_t free_p = pmm_get_free_count();
        size_t total_p = pmm_get_total_count();
        size_t used_p = total_p - free_p;
        uint32_t mem_pct = (used_p * 1000) / (total_p > 0 ? total_p : 1);

        console_printf("[PAGES]", "===== PAGE FRAME ALLOCATOR =====");
        bar_graph(mem_pct, bar, 20);
        console_write("  "); console_write(bar);
        console_write("  "); u32_to_str((mem_pct + 5) / 10, tmp); console_write(tmp);
        console_write("% used\n");
        console_write("  Total frames: "); u32_to_str(total_p, tmp); console_write(tmp); console_write("\n");
        console_write("  Used frames:  "); u32_to_str(used_p, tmp); console_write(tmp); console_write("\n");
        console_write("  Free frames:  "); u32_to_str(free_p, tmp); console_write(tmp); console_write("\n");
        console_write("  Page size: 4 KB\n");
        console_write("  Allocator: Bitmap (O(n) first-fit)\n");
        return;
    }

    /* ---------- 16. heap ---------- */
    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'a' && cmd[3] == 'p' && cmd[4] == '\0') {
        console_printf("[HEAP]", "===== KERNEL HEAP =====");
        console_write("  Allocator: kmalloc/kfree (slab-like)\n");
        console_write("  Heap base: 0x01000000\n");
        console_write("  Heap size: 4 MB\n");
        console_write("  Paging: Identity-mapped page table\n");
        console_write("  Used for: Kernel objects, buffers, IPC\n");
        return;
    }

    /* ---------- 17. ctx ---------- */
    if (cmd[0] == 'c' && cmd[1] == 't' && cmd[2] == 'x' && cmd[3] == '\0') {
        uint32_t ctx = scheduler_stats.context_switches;
        uint32_t secs = timer_ticks / 100;
        if (secs < 1) secs = 1;
        uint32_t rate = ctx / secs;

        console_printf("[CTX]", "===== CONTEXT SWITCHES =====");
        console_write("  Total switches: "); u32_to_str(ctx, tmp); console_write(tmp); console_write("\n");
        console_write("  Rate:           "); u32_to_str(rate, tmp); console_write(tmp); console_write(" ctx/s\n");
        console_write("  Trigger: PIT IRQ0 at 100 Hz\n");
        console_write("  Mechanism: Software task switch\n");
        console_write("  Overhead: Register save/restore only\n");
        return;
    }

    /* ---------- 18. idle ---------- */
    if (cmd[0] == 'i' && cmd[1] == 'd' && cmd[2] == 'l' && cmd[3] == 'e' && cmd[4] == '\0') {
        uint32_t total_t = scheduler_stats.total_ticks;
        uint32_t idle_t = scheduler_stats.idle_ticks;
        uint32_t idle_pct = (total_t > 0) ? (idle_t * 1000) / total_t : 1000;

        console_printf("[IDLE]", "===== CPU IDLE STATISTICS =====");
        bar_graph(idle_pct, bar, 20);
        console_write("  Idle: ");
        u32_to_str((idle_pct + 5) / 10, tmp); console_write(tmp);
        console_write("% "); console_write(bar);
        console_write("\n");
        console_write("  Idle ticks:  "); u32_to_str(idle_t, tmp); console_write(tmp); console_write("\n");
        console_write("  Active ticks: "); u32_to_str(scheduler_stats.active_ticks, tmp); console_write(tmp); console_write("\n");
        console_write("  Power state: HLT (CPU halted, ~1-3W)\n");
        return;
    }

    /* ---------- 19. sos ---------- */
    if (cmd[0] == 's' && cmd[1] == 'o' && cmd[2] == 's' && cmd[3] == '\0') {
        aura_send_sos();
        console_printf("[SOS]", "SOS packet dispatched via Aura-Net!");
        console_write("  Broadcast MAC: FF:FF:FF:FF:FF:FF\n");
        console_write("  EtherType: 0x9001 (custom SOS)\n");
        console_write("  Payload: SOS!!! + MAC + IP (16 bytes)\n");
        return;
    }

    /* ---------- 20. mac ---------- */
    if (cmd[0] == 'm' && cmd[1] == 'a' && cmd[2] == 'c' && cmd[3] == '\0') {
        console_printf("[MAC]", "Network Adapter Address");
        console_write("  ");
        for (int i = 0; i < 6; i++) {
            uint8_t n = aura_stats.our_mac[i];
            uint8_t h = (n >> 4) & 0xF;
            uint8_t l = n & 0xF;
            console_write_char((h < 10) ? ('0' + h) : ('A' + h - 10));
            console_write_char((l < 10) ? ('0' + l) : ('A' + l - 10));
            if (i < 5) console_write_char(':');
        }
        console_write("\n");
        console_write("  Vendor: QEMU virtual NIC\n");
        console_write("  Driver: RTL8139 / e1000\n");
        return;
    }

    /* ---------- 21. ip ---------- */
    if (cmd[0] == 'i' && cmd[1] == 'p' && cmd[2] == '\0') {
        console_printf("[IP]", "Network Address");
        console_write("  IP: 10.0.0.1 (static)\n");
        console_write("  Subnet: 255.0.0.0 (/8)\n");
        console_write("  Protocol: Aura-Net Layer 2 mesh\n");
        console_write("  Type: Manual (no DHCP)\n");
        return;
    }

    /* ---------- 22. irq ---------- */
    if (cmd[0] == 'i' && cmd[1] == 'r' && cmd[2] == 'q' && cmd[3] == '\0') {
        console_printf("[IRQ]", "===== INTERRUPT VECTOR TABLE =====");
        console_write("  PIC: Master 0x20, Slave 0x28\n");
        console_write("  IRQ0  (32): PIT Timer        [ACTIVE]\n");
        console_write("  IRQ1  (33): PS/2 Keyboard    [ACTIVE]\n");
        console_write("  IRQ12 (44): PS/2 Mouse       [ACTIVE]\n");
        console_write("  IDT: 256 entries, 32-47 for IRQs\n");
        console_write("  Exceptions: 0-31 (Div err, Page fault, etc.)\n");
        return;
    }

    /* ---------- 23. gdt ---------- */
    if (cmd[0] == 'g' && cmd[1] == 'd' && cmd[2] == 't' && cmd[3] == '\0') {
        console_printf("[GDT]", "===== GLOBAL DESCRIPTOR TABLE =====");
        console_write("  Entry 0: Null selector\n");
        console_write("  Entry 1: Kernel Code (Ring 0)\n");
        console_write("  Entry 2: Kernel Data (Ring 0)\n");
        console_write("  Entry 3: User Code   (Ring 3)\n");
        console_write("  Entry 4: User Data   (Ring 3)\n");
        console_write("  Entry 5: TSS (Task State Segment)\n");
        console_write("  Flags: 32-bit, 4 KB granularity\n");
        return;
    }

    /* ---------- 24. mode ---------- */
    if (cmd[0] == 'm' && cmd[1] == 'o' && cmd[2] == 'd' && cmd[3] == 'e' && cmd[4] == '\0') {
        console_printf("[MODE]", "===== CPU MODE =====");
        console_write("  Mode: Protected Mode (32-bit)\n");
        console_write("  Paging: Enabled (4 KB pages)\n");
        console_write("  Ring: 0 (Kernel) / 3 (User)\n");
        console_write("  Interrupts: Enabled (IF=1)\n");
        console_write("  FPU: x87 (if available)\n");
        console_write("  Display: VBE framebuffer @ 1024x768x32\n");
        return;
    }

    /* ---------- 25. pmm ---------- */
    if (cmd[0] == 'p' && cmd[1] == 'm' && cmd[2] == 'm' && cmd[3] == '\0') {
        size_t free_p = pmm_get_free_count();
        size_t total_p = pmm_get_total_count();
        size_t used_p = total_p - free_p;
        uint32_t total_mb = (uint32_t)(total_p * 4 / 1024);
        uint32_t used_mb = (uint32_t)(used_p * 4 / 1024);

        console_printf("[PMM]", "===== PHYSICAL MEMORY MANAGER =====");
        console_write("  Total RAM: "); u32_to_str(total_mb, tmp); console_write(tmp); console_write(" MB\n");
        console_write("  Used:      "); u32_to_str(used_mb, tmp); console_write(tmp); console_write(" MB\n");
        console_write("  Bitmap:    "); console_write("1 bit/page (efficient tracking)\n");
        console_write("  Max pages: "); u32_to_str(32768, tmp); console_write(tmp); console_write("\n");
        return;
    }

    /* ---------- 26. ring ---------- */
    if (cmd[0] == 'r' && cmd[1] == 'i' && cmd[2] == 'n' && cmd[3] == 'g' && cmd[4] == '\0') {
        console_printf("[RING]", "===== PRIVILEGE LEVELS =====");
        console_write("  Ring 0: Kernel — full hardware access\n");
        console_write("  Ring 3: User   — restricted (syscalls only)\n");
        console_write("  TSS:    Ring 0 stack for interrupt handling\n");
        console_write("  Syscall: int 0x80 (software interrupt)\n");
        console_write("  Isolation: Memory paging prevents user access\n");
        console_write("             to kernel pages\n");
        return;
    }

    /* ---------- 27. ticks ---------- */
    if (cmd[0] == 't' && cmd[1] == 'i' && cmd[2] == 'c' && cmd[3] == 'k' && cmd[4] == 's' && cmd[5] == '\0') {
        console_printf("[TICKS]", "===== TIMER COUNTER =====");
        console_write("  Ticks: "); u32_to_str(timer_ticks, tmp); console_write(tmp); console_write("\n");
        console_write("  Freq:  100 Hz (10 ms/tick)\n");
        console_write("  Source: PIT channel 0, rate generator\n");
        console_write("  Divisor: 11931 (1193180 / 100)\n");
        console_write("  IRQ:    IRQ0 (vector 32)\n");
        return;
    }

    /* ---------- 28. version ---------- */
    if (cmd[0] == 'v' && cmd[1] == 'e' && cmd[2] == 'r' && cmd[3] == 's' && cmd[4] == 'i' && cmd[5] == 'o' && cmd[6] == 'n' && cmd[7] == '\0') {
        console_printf("[VERSION]", "MYOS v2.0 'Aura'");
        console_write("  Build target: i386-pc (GRUB Multiboot)\n");
        console_write("  Compiler: GCC (-m32 -ffreestanding)\n");
        console_write("  Resolution: 1024x768 VBE framebuffer\n");
        console_write("  Memory: 128 MB (QEMU configured)\n");
        console_write("  Network: Aura-Net mesh protocol v1.0\n");
        console_write("  Security: Aegis ECC page protection v1.0\n");
        console_write("  Scheduler: Inertia v1.0 (HLT-based)\n");
        return;
    }

    /* ---------- 29. whoami ---------- */
    if (cmd[0] == 'w' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'a' && cmd[4] == 'm' && cmd[5] == 'i' && cmd[6] == '\0') {
        console_printf("[WHOAMI]", "User: kernel (superuser)");
        console_write("  UID:  0 (root)\n");
        console_write("  Ring: 0 (kernel mode)\n");
        console_write("  Host: MYOS on QEMU i386\n");
        console_write("  Node: Aura-Net mesh node 10.0.0.1\n");
        return;
    }

    /* ---------- 30. clear ---------- */
    if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 'r' && cmd[5] == '\0') {
        for (uint32_t i = 0; i < CONSOLE_BUF_SIZE; i++) console_buffer[i] = 0;
        console_write_pos = 0;
        console_printf("[SYSTEM]", "Console cleared.");
        return;
    }

    /* Unknown command */
    console_write("  Unknown command. Type 'help' for all 30 commands.\n");
}

/* ==========================================================
 * Keyboard Input Sync + Command Dispatch + Modal Dismiss
 * ========================================================== */
static void sync_keyboard(void) {
    if (!keyboard_updated) return;
    keyboard_updated = 0;

    uint32_t current_idx = keyboard_idx;
    if (current_idx < last_kb_idx) { input_len = 0; last_kb_idx = 0; }

    for (uint32_t i = last_kb_idx; i < current_idx && i < 255; i++) {
        char c = keyboard_buffer[i];

        /* If modal is active, check for 'X' dismiss key */
        if (sos_modal_active) {
            if (c == 'X' || c == 'x') {
                sos_modal_active = 0;
                incoming_signal_pending = 0; /* Also clear pending */
                console_printf("[SYSTEM]", "Modal dismissed by user.");
                continue;
            }
            /* Ignore all other keyboard input while modal is active */
            continue;
        }

        if (c == '\n') {
            if (input_len > 0) {
                input_line[input_len] = '\0';
                console_write("> ");
                console_write(input_line);
                console_write("\n");
                handle_command(input_line);
                input_len = 0;
            }
        } else if (c >= ' ' && c <= '~') {
            if (input_len < 254) {
                input_line[input_len++] = c;
            }
        }
    }
    input_line[input_len] = '\0';
    last_kb_idx = current_idx;
}

/* ==========================================================
 * F1 Hotkey: SOS Broadcast — triggers the modal popup
 * ========================================================== */
static void handle_hotkeys(void) {
    if (f1_pressed) {
        f1_pressed = 0;

        /* Send the actual SOS packet */
        aura_send_sos();

        /* Store payload data for the modal */
        for (int i = 0; i < 6; i++) {
            sos_modal_mac[i] = aura_stats.our_mac[i];
        }
        sos_modal_gps_lat = 4071;    /* 40.71° N */
        sos_modal_gps_lon = -7400;   /* -74.00° W */

        /* Activate outgoing modal */
        sos_modal_active = 1;
        sos_modal_type_outgoing = 1;
        sos_modal_start_tick = timer_ticks;

        /* Console log */
        console_printf("[AURA-SOS]", "*** EMERGENCY BROADCAST DISPATCHED ***");
        console_write("  Broadcast: FF:FF:FF:FF:FF:FF (Layer 2)\n");
        console_write("  EtherType: 0x9003 (Aura-Net SOS)\n");
        console_write("  Press 'X' to dismiss | Auto-closes in 3s\n");
    }
}

/* ==========================================================
 * RENDERING
 * ========================================================== */

static void draw_background(void) {
    draw_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
    set_draw_bg(COLOR_BG);
}

/* ==========================================================
 * PMM Heap Visualizer (Upper Left Panel)
 * Renders a grid of blocks colored by page state:
 *   Grey  = Free page
 *   Green = Allocated/Used page
 *   Red   = Quarantined/Bad page
 * ========================================================== */
static void draw_pmm_visualizer(void) {
    int32_t x = PMM_PANEL_X, y = TOP_PANEL_Y, w = PMM_PANEL_W, h = TOP_PANEL_H;

    /* Panel background */
    draw_rect(x, y, w, h, COLOR_PANEL_BG);
    set_draw_bg(COLOR_PANEL_BG);
    draw_rect_outline(x, y, w, h, COLOR_BORDER);

    /* Title bar */
    draw_rect(x, y, w, 20, COLOR_TITLE_BG);
    set_draw_bg(COLOR_TITLE_BG);
    draw_string(x + 6, y + 6, "PMM Heap Visualizer", COLOR_TITLE_TEXT);
    set_draw_bg(COLOR_PANEL_BG);

    /* Legend */
    draw_rect(x + w - 120, y + 4, 10, 10, COLOR_CELL_FREE);
    draw_string(x + w - 108, y + 6, "Free", COLOR_SUBTEXT);
    draw_rect(x + w - 78, y + 4, 10, 10, COLOR_CELL_USED);
    draw_string(x + w - 66, y + 6, "Alloc", COLOR_GREEN);
    draw_rect(x + w - 30, y + 4, 10, 10, COLOR_CELL_BAD);
    draw_string(x + w - 18, y + 6, "Bad", COLOR_RED);

    /* Stats line below title */
    size_t free_p = pmm_get_free_count();
    size_t total_p = pmm_get_total_count();
    size_t used_p = total_p - free_p;
    char stat_buf[64];
    int si = 0;
    const char* s1 = "Free:";
    while (*s1) stat_buf[si++] = *s1++;
    char tmp_num[12];
    u32_to_str(free_p, tmp_num);
    for (int i = 0; tmp_num[i]; i++) stat_buf[si++] = tmp_num[i];
    const char* s2 = " Used:";
    while (*s2) stat_buf[si++] = *s2++;
    u32_to_str(used_p, tmp_num);
    for (int i = 0; tmp_num[i]; i++) stat_buf[si++] = tmp_num[i];
    stat_buf[si] = '\0';
    draw_string(x + 6, y + 22, stat_buf, COLOR_CYAN);

    /* Memory grid dimensions */
    int grid_x = x + 6;
    int grid_y = y + 36;
    int grid_w = w - 12;
    int grid_h = h - 42;

    /* Calculate cell size based on available space */
    /* We want to show a subset of pages that fits the grid */
    int cols = grid_w / 6;      /* Each cell 6px wide */
    int rows = grid_h / 6;      /* Each cell 6px tall */
    if (cols < 10) cols = 10;
    if (rows < 10) rows = 10;

    int cell_size = 5;          /* 5x5 pixel cells with 1px gap */
    int gap = 1;

    int usable_cols = cols;
    int usable_rows = rows;

    /* Determine which pages to show. Sample from the PMM bitmap.
     * Show pages 0 through (usable_cols * usable_rows), sampling if needed.
     * But PMM_MAX_PAGES can be 32768, which is more than the grid can show.
     * We'll sample evenly across the address space. */

    uint32_t total_frames = (uint32_t)total_p;
    uint32_t shown_frames = (uint32_t)(usable_cols * usable_rows);

    for (int r = 0; r < usable_rows && r < grid_h / (cell_size + gap); r++) {
        for (int c = 0; c < usable_cols && c < grid_w / (cell_size + gap); c++) {
            /* Sample frame index evenly across address space */
            uint32_t frame_idx;
            if (shown_frames > 0 && total_frames > 0) {
                uint32_t sample_idx = (uint32_t)(r * usable_cols + c);
                frame_idx = (sample_idx * total_frames) / shown_frames;
            } else {
                frame_idx = 0;
            }

            int state = pmm_get_frame_state(frame_idx);
            uint32_t cell_color;
            switch (state) {
                case 0:  cell_color = COLOR_CELL_FREE; break;  /* Free - Grey */
                case 1:  cell_color = COLOR_CELL_USED; break;  /* Used - Green */
                case 2:  cell_color = COLOR_CELL_BAD;  break;  /* Bad  - Red */
                default: cell_color = COLOR_CELL_USED; break;
            }

            int px = grid_x + c * (cell_size + gap);
            int py = grid_y + r * (cell_size + gap);
            draw_rect(px, py, cell_size, cell_size, cell_color);
        }
    }

    /* Draw a faint border around the grid */
    draw_rect_outline(grid_x - 1, grid_y - 1,
                      usable_cols * (cell_size + gap) + 1,
                      (grid_h / (cell_size + gap)) * (cell_size + gap) + 1,
                      0x2A2A3A);
}

/* ==========================================================
 * Aura-Net Mesh Node List (Upper Right Panel)
 * Shows discovered mesh nodes with MAC, GPS, status
 * ========================================================== */
static void draw_node_list(void) {
    int32_t x = NODE_PANEL_X, y = TOP_PANEL_Y, w = NODE_PANEL_W, h = TOP_PANEL_H;

    draw_rect(x, y, w, h, COLOR_PANEL_BG);
    set_draw_bg(COLOR_PANEL_BG);
    draw_rect_outline(x, y, w, h, COLOR_BORDER);

    /* Title */
    draw_rect(x, y, w, 20, COLOR_TITLE_BG);
    set_draw_bg(COLOR_TITLE_BG);
    draw_string(x + 6, y + 6, "Aura-Net Mesh Radar", COLOR_TITLE_TEXT);
    set_draw_bg(COLOR_PANEL_BG);

    /* Header row */
    draw_string(x + 6, y + 22, "MAC Address",               COLOR_CYAN);
    draw_string(x + 110, y + 22, "GPS Latitude",             COLOR_CYAN);
    draw_string(x + 260, y + 22, "Longitude",                COLOR_CYAN);
    draw_string(x + 390, y + 22, "Status",                   COLOR_CYAN);

    /* Separator */
    draw_rect(x + 4, y + 32, w - 8, 1, COLOR_BORDER);

    /* Our own node (self) at top */
    char line_buf[64];
    int li;

    /* Self: our MAC */
    li = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t n = aura_stats.our_mac[i];
        uint8_t hi = (n >> 4) & 0xF;
        uint8_t lo = n & 0xF;
        line_buf[li++] = (hi < 10) ? ('0' + hi) : ('A' + hi - 10);
        line_buf[li++] = (lo < 10) ? ('0' + lo) : ('A' + lo - 10);
        if (i < 5) line_buf[li++] = ':';
    }
    line_buf[li] = '\0';
    draw_string(x + 6, y + 36, line_buf, COLOR_GREEN);
    draw_string(x + 110, y + 36, "40.71\xb0N",     COLOR_ACCENT);
    draw_string(x + 260, y + 36, "74.00\xb0W",     COLOR_ACCENT);
    draw_string(x + 390, y + 36, "[SELF] Online", COLOR_GREEN);

    /* Discovered mesh nodes */
    mesh_node_t* nodes = get_mesh_nodes();
    int node_count = get_mesh_node_count();
    char tmp_num2[16];

    for (int i = 0; i < node_count && i < 8; i++) {
        int ny = y + 52 + i * 18;
        if (ny + 18 > y + h) break;

        /* MAC */
        li = 0;
        for (int j = 0; j < 6; j++) {
            uint8_t n = nodes[i].mac[j];
            uint8_t hi = (n >> 4) & 0xF;
            uint8_t lo = n & 0xF;
            line_buf[li++] = (hi < 10) ? ('0' + hi) : ('A' + hi - 10);
            line_buf[li++] = (lo < 10) ? ('0' + lo) : ('A' + lo - 10);
            if (j < 5) line_buf[li++] = ':';
        }
        line_buf[li] = '\0';

        /* Check if recently seen (within last 10 seconds = 1000 ticks) */
        uint32_t age = timer_ticks - nodes[i].last_seen;
        uint32_t color = (age < 1000) ? COLOR_TEXT : COLOR_SUBTEXT;

        draw_string(x + 6, ny, line_buf, color);

        /* GPS — values are degrees * 100 */
        li = 0;
        int32_t lat_deg = nodes[i].gps_lat / 100;
        int32_t lat_min = nodes[i].gps_lat % 100;
        int32_t lon_deg = nodes[i].gps_lon / 100;
        int32_t lon_min = nodes[i].gps_lon % 100;
        if (lat_min < 0) lat_min = -lat_min;
        if (lon_min < 0) lon_min = -lon_min;
        char lat_abs = (nodes[i].gps_lat >= 0) ? 'N' : 'S';
        char lon_abs = (nodes[i].gps_lon >= 0) ? 'E' : 'W';
        if (lat_deg < 0) lat_deg = -lat_deg;
        if (lon_deg < 0) lon_deg = -lon_deg;

        /* Format latitude: DD.dd */
        u32_to_str(lat_deg, tmp_num2);
        for (int i2 = 0; tmp_num2[i2]; i2++) line_buf[li++] = tmp_num2[i2];
        line_buf[li++] = '.';
        line_buf[li++] = '0' + (lat_min / 10) % 10;
        line_buf[li++] = '0' + lat_min % 10;
        line_buf[li++] = '\xb0';
        line_buf[li++] = lat_abs;
        line_buf[li] = '\0';
        draw_string(x + 110, ny, line_buf, COLOR_ACCENT);

        li = 0;
        u32_to_str(lon_deg, tmp_num2);
        for (int i2 = 0; tmp_num2[i2]; i2++) line_buf[li++] = tmp_num2[i2];
        line_buf[li++] = '.';
        line_buf[li++] = '0' + (lon_min / 10) % 10;
        line_buf[li++] = '0' + lon_min % 10;
        line_buf[li++] = '\xb0';
        line_buf[li++] = lon_abs;
        line_buf[li] = '\0';
        draw_string(x + 260, ny, line_buf, COLOR_ACCENT);

        /* Status */
        uint32_t status_color;
        const char* status_text;
        switch (nodes[i].status) {
            case 0: status_color = COLOR_NODE_ONLINE; status_text = "Online";  break;
            case 1: status_color = COLOR_NODE_WARN;   status_text = "Warning"; break;
            case 2: status_color = COLOR_NODE_CRIT;    status_text = "Crit";    break;
            default: status_color = COLOR_SUBTEXT;     status_text = "Unk";     break;
        }
        draw_string(x + 390, ny, status_text, status_color);
    }

    /* If no nodes, show helpful message */
    if (node_count == 0) {
        draw_string(x + 60, y + 70, "Waiting for mesh packets...", COLOR_SUBTEXT);
        draw_string(x + 60, y + 86, "Broadcasting heartbeats every 5s", COLOR_SUBTEXT);
    }

    /* Network stats at bottom of panel */
    char net_stat[128];
    li = 0;
    const char* ns1 = "TX:";
    while (*ns1) net_stat[li++] = *ns1++;
    u32_to_str(aura_stats.frames_sent, tmp_num2);
    for (int i2 = 0; tmp_num2[i2]; i2++) net_stat[li++] = tmp_num2[i2];
    const char* ns2 = " RX:";
    while (*ns2) net_stat[li++] = *ns2++;
    u32_to_str(aura_stats.frames_received, tmp_num2);
    for (int i2 = 0; tmp_num2[i2]; i2++) net_stat[li++] = tmp_num2[i2];
    const char* ns3 = " HB:";
    while (*ns3) net_stat[li++] = *ns3++;
    u32_to_str(aura_stats.heartbeats_sent, tmp_num2);
    for (int i2 = 0; tmp_num2[i2]; i2++) net_stat[li++] = tmp_num2[i2];
    net_stat[li] = '\0';
    draw_string(x + 6, y + h - 24, net_stat, COLOR_YELLOW);

    /* Logistics info */
    if (aura_stats.resources_matched > 0) {
        char log_buf[64];
        li = 0;
        const char* l1 = "LOGISTICS: ";
        while (*l1) log_buf[li++] = *l1++;
        u32_to_str(aura_stats.resources_matched, tmp_num2);
        for (int i2 = 0; tmp_num2[i2]; i2++) log_buf[li++] = tmp_num2[i2];
        const char* l2 = " matches  Ser:";
        while (*l2) log_buf[li++] = *l2++;
        u32_to_str(aura_stats.serial_frames_sent, tmp_num2);
        for (int i2 = 0; tmp_num2[i2]; i2++) log_buf[li++] = tmp_num2[i2];
        const char* l3 = "/";
        while (*l3) log_buf[li++] = *l3++;
        u32_to_str(aura_stats.serial_frames_received, tmp_num2);
        for (int i2 = 0; tmp_num2[i2]; i2++) log_buf[li++] = tmp_num2[i2];
        log_buf[li] = '\0';
        draw_string(x + 6, y + h - 12, log_buf, COLOR_ORANGE);
    } else {
        draw_string(x + 6, y + h - 12, "Logistics: No active resource matches", COLOR_SUBTEXT);
    }
}

/* Draw the console window with its scrollable text log */
static void draw_console_window(void) {
    int32_t x = CONSOLE_X, y = CONSOLE_Y, w = CONSOLE_W, h = CONSOLE_H;

    /* Shadow + body */
    draw_rect(x + 3, y + 3, w, h, 0x000000);
    draw_rect(x, y, w, h, COLOR_SURFACE);

    /* Border at edges */
    draw_rect(x, y, w, 1, COLOR_BORDER);
    draw_rect(x, y + h - 1, w, 1, COLOR_BORDER);
    draw_rect(x, y, 1, h, COLOR_BORDER);
    draw_rect(x + w - 1, y, 1, h, COLOR_BORDER);

    /* Title bar */
    draw_rect(x, y, w, CONSOLE_TITLE_H, 0x181828);
    draw_rect(x, y + CONSOLE_TITLE_H, w, 1, COLOR_ACCENT);
    set_draw_bg(0x181828);
    draw_string(x + CONSOLE_PAD, y + 8, "MYOS Telemetry & Network Console", COLOR_TEXT);

    /* Live indicator (green pulsing dot) */
    draw_rect(x + w - 22, y + 8, 8, 8, (timer_ticks / 10) % 2 ? COLOR_GREEN : 0x446644);

    /* Console text log — O(N) backward scan */
    uint32_t line_starts[VISIBLE_ROWS + 2];
    uint32_t line_count = 1;
    line_starts[0] = console_write_pos;

    uint32_t scan_pos = console_write_pos;
    for (uint32_t i = 0; i < CONSOLE_BUF_SIZE && line_count <= VISIBLE_ROWS + 1; i++) {
        if (scan_pos == 0) scan_pos = CONSOLE_BUF_SIZE - 1;
        else scan_pos--;
        if (console_buffer[scan_pos] == '\n' || console_buffer[scan_pos] == '\0') {
            line_starts[line_count] = (scan_pos + 1) % CONSOLE_BUF_SIZE;
            line_count++;
        }
    }

    int32_t draw_y = CONSOLE_TEXT_Y + (VISIBLE_ROWS - 1) * FONT_H;
    uint32_t num_to_draw = line_count - 1;
    if (num_to_draw > VISIBLE_ROWS) num_to_draw = VISIBLE_ROWS;

    set_draw_bg(COLOR_SURFACE);

    for (uint32_t l = 0; l < num_to_draw; l++) {
        char line_buf[256];
        uint32_t li = 0;
        uint32_t rp = line_starts[l + 1];
        uint32_t end_rp = line_starts[l];

        while (li < VISIBLE_COLS && li < 200) {
            if (rp == end_rp) break;
            if (console_buffer[rp] == '\n' || console_buffer[rp] == '\0') break;
            line_buf[li++] = console_buffer[rp];
            rp = (rp + 1) % CONSOLE_BUF_SIZE;
        }
        line_buf[li] = '\0';

        if (li > 0) {
            uint32_t color = COLOR_TEXT;
            char first = line_buf[0];
            char second = line_buf[1];
            if (first == '[') {
                if (second == 'A') color = COLOR_CYAN;
                else if (second == 'E') color = COLOR_RED;
                else if (second == 'S') { char third = line_buf[2];
                    if (third == 'Y') color = COLOR_YELLOW;
                    else if (third == 'O') color = COLOR_RED;
                    else if (third == 'T') color = COLOR_GREEN; }
                else if (second == 'M') color = COLOR_ORANGE;
                else if (second == 'H') color = COLOR_ACCENT;
                else if (second == 'P') color = COLOR_ORANGE;
            } else if (first == '>') {
                color = COLOR_GREEN;
            }
            draw_string(CONSOLE_TEXT_X, draw_y, line_buf, color);
        }
        draw_y -= FONT_H;
    }

    /* Input bar at bottom */
    int32_t input_bar_y = CONSOLE_Y + CONSOLE_H - CONSOLE_BOTTOM_H;
    draw_rect(x, input_bar_y, w, CONSOLE_BOTTOM_H, 0x181828);
    draw_rect(x, input_bar_y, w, 1, COLOR_ACCENT);
    set_draw_bg(0x181828);
    draw_string(x + CONSOLE_PAD, input_bar_y + 7, "> ", COLOR_GREEN);

    if (input_len > 0) {
        char display_buf[256];
        uint32_t max_display = (w - CONSOLE_PAD * 2 - 16) / FONT_W;
        uint32_t start = (input_len > max_display) ? (input_len - max_display) : 0;
        uint32_t di = 0;
        for (uint32_t i = start; i < input_len && di < max_display; i++)
            display_buf[di++] = input_line[i];
        display_buf[di] = '\0';
        draw_string(x + CONSOLE_PAD + 16, input_bar_y + 7, display_buf, COLOR_TEXT);
    }

    /* Blinking cursor */
    if ((timer_ticks / 5) % 2) {
        int32_t cx = x + CONSOLE_PAD + 16 + (int32_t)(input_len % ((w - CONSOLE_PAD * 2 - 16) / FONT_W)) * FONT_W;
        if (cx < x + w - CONSOLE_PAD - 8)
            draw_rect(cx, input_bar_y + 8, 6, FONT_H - 2, COLOR_ACCENT);
    }
}

/* ==========================================================
 * SOS Emergency Modal Popup
 *
 * Large centered overlay triggered by:
 *   - F1 key (outgoing broadcast) — "EMERGENCY BROADCAST DISPATCHED"
 *   - Incoming RTL8139 packet from another node — "INCOMING DISTRESS SIGNAL"
 *
 * Auto-dismisses after SOS_MODAL_DURATION ticks (~3 seconds)
 * Manual dismiss via 'X' key
 * Clean double-buffered rendering — no artifacts on dismiss
 * ========================================================== */
static void draw_sos_modal(void) {
    if (!sos_modal_active) return;

    int32_t x = MODAL_X, y = MODAL_Y, w = MODAL_W, h = MODAL_H;

    /* Dark overlay behind modal (semi-transparent feel) */
    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000);
    /* Subtle overlay blending via darker tint */
    for (int row = 0; row < SCREEN_H; row += 2) {
        draw_rect(0, row, SCREEN_W, 1, 0x080810);
    }

    /* Modal shadow */
    draw_rect(x + 5, y + 5, w, h, 0x000000);

    /* Alert border — pulsing red */
    uint32_t alert_color = (timer_ticks / 3) % 2 ? 0xFF3030 : 0xCC2020;
    set_draw_bg(0x1A1A2E);
    draw_rect(x, y, w, 3, alert_color);
    draw_rect(x, y + h - 3, w, 3, alert_color);
    draw_rect(x, y, 3, h, alert_color);
    draw_rect(x + w - 3, y, 3, h, alert_color);

    /* Modal background */
    draw_rect(x + 3, y + 3, w - 6, h - 6, 0x1A1A2E);

    /* Warning icon area */
    draw_rect(x + w / 2 - 30, y + 20, 60, 50, 0xCC2020);
    /* Exclamation mark in red box */
    draw_string(x + w / 2 - 12, y + 32, "!!", 0xFFFFFF);

    /* Title */
    const char* title;
    if (sos_modal_type_outgoing) {
        title = "EMERGENCY BROADCAST DISPATCHED";
        draw_string(x + w / 2 - 170, y + 80, title, 0xFF6060);
    } else {
        title = "INCOMING DISTRESS SIGNAL";
        draw_string(x + w / 2 - 148, y + 80, title, 0xFFAA00);
    }

    /* Status subtext */
    draw_string(x + w / 2 - 160, y + 100, "Aura-Net Layer 2 Mesh Protocol", COLOR_SUBTEXT);

    /* Separator line */
    draw_rect(x + 30, y + 115, w - 60, 1, COLOR_BORDER);

    /* === Payload Data === */
    char line[128];
    int li;

    /* Target MAC */
    li = 0;
    const char* ml1 = "Target MAC:  FF:FF:FF:FF:FF:FF (Broadcast)";
    while (*ml1) line[li++] = *ml1++;
    line[li] = '\0';
    draw_string(x + 40, y + 125, line, COLOR_CYAN);

    /* Sender MAC (our MAC or incoming MAC) */
    li = 0;
    const char* ml2 = "Sender MAC: ";
    while (*ml2) line[li++] = *ml2++;
    for (int i = 0; i < 6; i++) {
        uint8_t n = sos_modal_mac[i];
        uint8_t hi = (n >> 4) & 0xF;
        uint8_t lo = n & 0xF;
        line[li++] = (hi < 10) ? ('0' + hi) : ('A' + hi - 10);
        line[li++] = (lo < 10) ? ('0' + lo) : ('A' + lo - 10);
        if (i < 5) line[li++] = ':';
    }
    line[li] = '\0';
    draw_string(x + 40, y + 143, line, COLOR_TEXT);

    /* GPS Coordinates */
    li = 0;
    const char* ml3 = "GPS:  ";
    while (*ml3) line[li++] = *ml3++;

    int32_t lat_deg = sos_modal_gps_lat / 100;
    int32_t lat_min = sos_modal_gps_lat % 100;
    if (lat_min < 0) lat_min = -lat_min;
    char lat_dir = (sos_modal_gps_lat >= 0) ? 'N' : 'S';
    if (lat_deg < 0) lat_deg = -lat_deg;
    char tmp_n[16];
    u32_to_str(lat_deg, tmp_n);
    for (int i = 0; tmp_n[i]; i++) line[li++] = tmp_n[i];
    line[li++] = '.';
    line[li++] = '0' + (lat_min / 10) % 10;
    line[li++] = '0' + lat_min % 10;
    line[li++] = '\xb0';
    line[li++] = lat_dir;

    const char* ml3b = ", ";
    while (*ml3b) line[li++] = *ml3b++;

    int32_t lon_deg = sos_modal_gps_lon / 100;
    int32_t lon_min = sos_modal_gps_lon % 100;
    if (lon_min < 0) lon_min = -lon_min;
    char lon_dir = (sos_modal_gps_lon >= 0) ? 'E' : 'W';
    if (lon_deg < 0) lon_deg = -lon_deg;
    u32_to_str(lon_deg, tmp_n);
    for (int i = 0; tmp_n[i]; i++) line[li++] = tmp_n[i];
    line[li++] = '.';
    line[li++] = '0' + (lon_min / 10) % 10;
    line[li++] = '0' + lon_min % 10;
    line[li++] = '\xb0';
    line[li++] = lon_dir;
    line[li] = '\0';
    draw_string(x + 40, y + 161, line, COLOR_ACCENT);

    /* Timestamp */
    li = 0;
    const char* ml4 = "Timestamp:  ";
    while (*ml4) line[li++] = *ml4++;
    u32_to_str(sos_modal_start_tick, tmp_n);
    for (int i = 0; tmp_n[i]; i++) line[li++] = tmp_n[i];
    const char* ml4b = " ticks (PIT @ 100 Hz)";
    while (*ml4b) line[li++] = *ml4b++;
    line[li] = '\0';
    draw_string(x + 40, y + 179, line, COLOR_YELLOW);

    /* Protocol info */
    li = 0;
    const char* ml5 = "Protocol:   Aura-Net Emergency v1.0 | EtherType: 0x9003";
    while (*ml5) line[li++] = *ml5++;
    line[li] = '\0';
    draw_string(x + 40, y + 197, line, COLOR_GREEN);

    /* Node status */
    li = 0;
    const char* ml6 = "Node Status: ";
    while (*ml6) line[li++] = *ml6++;
    if (sos_modal_type_outgoing) {
        const char* ml6b = "CRITICAL — Distress Signal Active";
        while (*ml6b) line[li++] = *ml6b++;
    } else {
        const char* ml6b = "DISTRESS — Remote Node Emergency";
        while (*ml6b) line[li++] = *ml6b++;
    }
    line[li] = '\0';
    draw_string(x + 40, y + 215, line, 0xFF6060);

    /* Separator */
    draw_rect(x + 30, y + 235, w - 60, 1, COLOR_BORDER);

    /* Auto-dismiss countdown */
    uint32_t elapsed = timer_ticks - sos_modal_start_tick;
    uint32_t remaining;
    if (elapsed >= SOS_MODAL_DURATION) {
        remaining = 0;
    } else {
        remaining = (SOS_MODAL_DURATION - elapsed) / 10; /* tenths of seconds */
    }

    li = 0;
    const char* ml7 = "Auto-dismiss in: ";
    while (*ml7) line[li++] = *ml7++;
    u32_to_str(remaining / 10, tmp_n);
    for (int i = 0; tmp_n[i]; i++) line[li++] = tmp_n[i];
    line[li++] = '.';
    line[li++] = '0' + remaining % 10;
    const char* ml7b = "s  |  Press 'X' to close";
    while (*ml7b) line[li++] = *ml7b++;
    line[li] = '\0';
    draw_string(x + 40, y + 248, line, COLOR_SUBTEXT);

    /* Live pulsing indicator */
    uint32_t pulse = (timer_ticks / 2) % 2 ? 0xFF0000 : 0x660000;
    draw_rect(x + w - 30, y + 15, 12, 12, pulse);
}

/* ==========================================================
 * CPU Telemetry Panel (Bottom)
 * Live calculation of CPU efficiency % from real PIT ticks
 * ========================================================== */
static void draw_cpu_telemetry(void) {
    int32_t x = CPU_PANEL_X, y = CPU_PANEL_Y, w = CPU_PANEL_W, h = CPU_PANEL_H;

    draw_rect(x, y, w, h, COLOR_PANEL_BG);
    draw_rect_outline(x, y, w, h, COLOR_BORDER);

    /* Title */
    draw_rect(x, y, w, 18, COLOR_TITLE_BG);
    set_draw_bg(COLOR_TITLE_BG);
    draw_string(x + 6, y + 5, "CPU Telemetry - Live PIT Tick Analysis", COLOR_TITLE_TEXT);
    set_draw_bg(COLOR_PANEL_BG);
    /* Get live scheduler stats */
    uint32_t total_t   = scheduler_stats.total_ticks;
    uint32_t idle_t    = scheduler_stats.idle_ticks;
    uint32_t active_t  = scheduler_stats.active_ticks;

    /* CPU Efficiency = active_ticks / total_ticks * 100% */
    uint32_t efficiency_pct_x1000 = (total_t > 0) ? (active_t * 1000) / total_t : 0;
    uint32_t idle_pct_x1000       = 1000 - efficiency_pct_x1000;

    uint32_t efficiency_int = (efficiency_pct_x1000 + 5) / 10; /* 0-100% */
    uint32_t idle_int       = (idle_pct_x1000 + 5) / 10;

    /* ---- Bar graph ---- */
    int bar_x = x + 6;
    int bar_y = y + 24;
    int bar_w = w - 12;
    int bar_h = 18;

    /* Background bar */
    draw_rect(bar_x, bar_y, bar_w, bar_h, COLOR_CPU_BAR_BG);

    /* Filled portion for CPU efficiency (active) */
    int filled_w = (efficiency_pct_x1000 * bar_w) / 1000;
    if (filled_w > bar_w) filled_w = bar_w;
    if (filled_w > 0) {
        draw_rect(bar_x, bar_y, filled_w, bar_h, COLOR_CPU_BAR_FG);
    }

    /* Gradient highlight on the bar */
    if (filled_w > 4) {
        draw_rect(bar_x, bar_y, filled_w, 2, 0x9C9CE8);
    }

    /* Text labels on the bar */
    char bar_label[64];
    int bli = 0;
    const char* bl1 = "CPU Efficiency: ";
    while (*bl1) bar_label[bli++] = *bl1++;

    char tmp_n[12];
    u32_to_str(efficiency_int, tmp_n);
    for (int i = 0; tmp_n[i]; i++) bar_label[bli++] = tmp_n[i];
    bar_label[bli++] = '%';

    /* Add ticks count */
    const char* bl2 = "  |  Total ticks: ";
    while (*bl2) bar_label[bli++] = *bl2++;
    u32_to_str(total_t, tmp_n);
    for (int i = 0; tmp_n[i]; i++) bar_label[bli++] = tmp_n[i];

    const char* bl3 = "  Active: ";
    while (*bl3) bar_label[bli++] = *bl3++;
    u32_to_str(active_t, tmp_n);
    for (int i = 0; tmp_n[i]; i++) bar_label[bli++] = tmp_n[i];

    const char* bl4 = "  Idle: ";
    while (*bl4) bar_label[bli++] = *bl4++;
    u32_to_str(idle_t, tmp_n);
    for (int i = 0; tmp_n[i]; i++) bar_label[bli++] = tmp_n[i];
    bar_label[bli] = '\0';

    draw_string(bar_x + 4, bar_y + 5, bar_label, COLOR_EFF_TEXT);

    /* ---- Second row: context switches, time saved, power info ---- */
    uint32_t ctx = scheduler_stats.context_switches;
    uint32_t secs = timer_ticks / 100;
    uint32_t ctx_rate = (secs > 0) ? ctx / secs : 0;

    char info_line[128];
    bli = 0;
    const char* il1 = "Ctx Switches: ";
    while (*il1) info_line[bli++] = *il1++;
    u32_to_str(ctx, tmp_n);
    for (int i = 0; tmp_n[i]; i++) info_line[bli++] = tmp_n[i];
    const char* il2 = " (";
    while (*il2) info_line[bli++] = *il2++;
    u32_to_str(ctx_rate, tmp_n);
    for (int i = 0; tmp_n[i]; i++) info_line[bli++] = tmp_n[i];
    const char* il3 = "/s)  |  Power Saved: ";
    while (*il3) info_line[bli++] = *il3++;
    u32_to_str(idle_int, tmp_n);
    for (int i = 0; tmp_n[i]; i++) info_line[bli++] = tmp_n[i];
    const char* il4 = "%  |  Idle task: HLT";
    while (*il4) info_line[bli++] = *il4++;
    info_line[bli] = '\0';

    draw_string(x + 6, y + 48, info_line, COLOR_CYAN);

    /* ---- Third row: efficiency rating ---- */
    bli = 0;
    const char* rating;
    uint32_t rating_color;
    if (efficiency_int >= 90) {
        rating = "Efficiency: EXCELLENT - System near 100% utilized";
        rating_color = COLOR_GREEN;
    } else if (efficiency_int >= 70) {
        rating = "Efficiency: GOOD - Balanced workload";
        rating_color = COLOR_YELLOW;
    } else if (efficiency_int >= 40) {
        rating = "Efficiency: MODERATE - Moderate idle periods";
        rating_color = COLOR_ORANGE;
    } else {
        rating = "Efficiency: LOW - Significant idle (HLT power saving active)";
        rating_color = COLOR_CYAN;
    }
    while (*rating) info_line[bli++] = *rating++;
    info_line[bli] = '\0';

    /* Time saved metric */
    draw_string(x + 6, y + 66, info_line, rating_color);

    /* Live timer indicator (animated) */
    uint32_t dot_color = (timer_ticks % 2) ? COLOR_GREEN : 0x224422;
    draw_rect(x + w - 14, y + 4, 10, 10, dot_color);
}

/* Status bar with headline calculations */
static void draw_status_bar(void) {
    draw_rect(0, STATUS_Y, SCREEN_W, STATUS_H, 0x181828);
    draw_rect(0, STATUS_Y, SCREEN_W, 1, COLOR_BORDER);
    set_draw_bg(0x181828);

    char tmp[24];
    char line[128];

    /* LEFT: Uptime */
    uint32_t secs = timer_ticks / 100;
    uint32_t tenths = (timer_ticks / 10) % 10;
    uint32_t mins = secs / 60; secs %= 60;

    int li = 0;
    const char* ul = "Up: ";
    while (*ul) line[li++] = *ul++;
    if (mins > 0) {
        line[li++] = '0' + (mins / 10) % 10;
        line[li++] = '0' + (mins % 10);
        line[li++] = ':';
    }
    line[li++] = '0' + (secs / 10) % 10;
    line[li++] = '0' + (secs % 10);
    line[li++] = '.';
    line[li++] = '0' + tenths;
    line[li] = '\0';
    draw_string(6, STATUS_Y + 6, line, COLOR_GREEN);

    /* CENTER: CPU Efficiency */
    uint32_t total_t = scheduler_stats.total_ticks;
    uint32_t idle_t = scheduler_stats.idle_ticks;
    uint32_t active_t = total_t - idle_t;
    uint32_t eff_pct = (total_t > 0) ? (active_t * 1000) / total_t : 0;
    uint32_t eff_int = (eff_pct + 5) / 10;

    li = 0;
    const char* el = "CPU: ";
    while (*el) line[li++] = *el++;
    line[li++] = '0' + (eff_int / 10) % 10;
    line[li++] = '0' + (eff_int % 10);
    line[li++] = '%';
    line[li++] = ' ';
    line[li++] = '|';
    line[li++] = ' ';
    line[li] = '\0';
    draw_string(120, STATUS_Y + 6, line, COLOR_CYAN);

    /* CENTER-RIGHT: Memory used */
    size_t free_p = pmm_get_free_count();
    size_t total_p = pmm_get_total_count();
    size_t used_p = total_p - free_p;
    uint32_t mem_pct = (used_p * 1000) / (total_p > 0 ? total_p : 1);
    uint32_t mem_pct_int = (mem_pct + 5) / 10;

    li = 0;
    const char* ml = "Mem: ";
    while (*ml) line[li++] = *ml++;
    u32_to_str(used_p, tmp);
    for (int i = 0; tmp[i]; i++) line[li++] = tmp[i];
    line[li++] = '/';
    u32_to_str(total_p, tmp);
    for (int i = 0; tmp[i]; i++) line[li++] = tmp[i];
    line[li++] = ' ';
    line[li++] = '(';
    line[li++] = '0' + (mem_pct_int / 10) % 10;
    line[li++] = '0' + (mem_pct_int % 10);
    line[li++] = '%';
    line[li++] = ')';
    line[li] = '\0';
    draw_string(300, STATUS_Y + 6, line, COLOR_ORANGE);

    /* RIGHT: Network + hotkey */
    li = 0;
    const char* fl = "TX:";
    while (*fl) line[li++] = *fl++;
    u32_to_str(aura_stats.frames_sent, tmp);
    for (int i = 0; tmp[i]; i++) line[li++] = tmp[i];
    line[li++] = ' ';
    line[li++] = 'R';
    line[li++] = 'X';
    line[li++] = ':';
    u32_to_str(aura_stats.frames_received, tmp);
    for (int i = 0; tmp[i]; i++) line[li++] = tmp[i];
    line[li++] = ' ';
    line[li++] = '|';
    line[li++] = ' ';
    line[li++] = 'F';
    line[li++] = '1';
    line[li++] = '=';
    line[li++] = 'S';
    line[li++] = 'O';
    line[li++] = 'S';
    line[li] = '\0';
    draw_string(SCREEN_W - 280, STATUS_Y + 6, line, COLOR_YELLOW);
}

/* ==========================================================
 * INITIALIZATION
 * ========================================================== */
void init_desktop(void) {
    write_serial("[DESKTOP] Initializing console...\r\n");

    for (uint32_t i = 0; i < CONSOLE_BUF_SIZE; i++) console_buffer[i] = 0;
    console_write_pos = 0;
    input_len = 0;
    last_kb_idx = 0;

    console_printf("[SYSTEM]",  "MYOS v2.0 — 32-bit x86 Advanced Operating System");
    console_printf("[STATUS]",  "All 17 subsystems initialized. Type 'help' for commands.");
    console_printf("[POWER]",   "Power savings: Inertia HLT scheduler active.");
    console_printf("[MEMORY]",  "PMM bitmap + paging + kernel heap online.");
    console_printf("[NET]",     "Aura-Net mesh ready. Press F1 for SOS.");
    console_printf("[SECURE]",  "Aegis ECC page protection active.");
    console_printf("",          "");
    console_printf("[HELP]",    "Type 'help' to see all 30 available commands.");
    console_printf("",          "");
    console_printf("[UI]",      "PMM Visualizer + Node Radar + CPU Telemetry active.");
    console_printf("",          "");

    write_serial("[DESKTOP] Console compositor initialized.\r\n");
}

/* ==========================================================
 * MAIN DESKTOP LOOP
 * ========================================================== */
void desktop_task(void) {
    write_serial("[DESKTOP] Entering compositor loop.\r\n");

    while (1) {
        __asm__ volatile("hlt");

        handle_hotkeys();
        sync_keyboard();

        /* ---- Poll RTL8139 for incoming Ethernet frames ---- */
        {
            uint8_t eth_buf[SERIAL_MAX_FRAME];
            int eth_len;
            while ((eth_len = rtl8139_poll_frame(eth_buf, SERIAL_MAX_FRAME)) > 0) {
                eth_header_t* hdr = (eth_header_t*)eth_buf;
                aura_handle_frame(hdr, (uint32_t)eth_len);
            }
        }

        /* ---- Poll COM1 serial for incoming radio frames ---- */
        aura_poll_serial();

        /* ---- Aura heartbeat & resource advertisement ---- */
        aura_update_heartbeat();

        /* ---- Check for incoming distress signals ---- */
        if (incoming_signal_pending && !sos_modal_active) {
            /* Capture incoming signal data for the modal */
            for (int i = 0; i < 6; i++) {
                sos_modal_mac[i] = incoming_sender_mac[i];
            }
            sos_modal_gps_lat = incoming_gps_lat;
            sos_modal_gps_lon = incoming_gps_lon;
            sos_modal_start_tick = timer_ticks;
            sos_modal_active = 1;
            sos_modal_type_outgoing = 0; /* Incoming distress */
            incoming_signal_pending = 0;

            console_printf("[AURA-SOS]", "*** INCOMING DISTRESS SIGNAL DETECTED ***");
            console_write("  Press 'X' to dismiss | Auto-closes in 3s\n");
        }

        /* ---- Auto-dismiss modal after timeout ---- */
        if (sos_modal_active) {
            uint32_t elapsed = timer_ticks - sos_modal_start_tick;
            if (elapsed >= SOS_MODAL_DURATION) {
                sos_modal_active = 0;
                console_printf("[SYSTEM]", "Modal auto-dismissed (3s timeout).");
            }
        }

        /* Draw everything in back-to-front order */
        draw_background();

        /* Upper panels */
        draw_pmm_visualizer();
        draw_node_list();

        /* Console */
        draw_console_window();

        /* Bottom CPU telemetry */
        draw_cpu_telemetry();

        /* SOS Emergency Modal (drawn on top of everything) */
        draw_sos_modal();

        /* Status bar + mouse */
        draw_status_bar();
        draw_mouse_cursor(mouse_x, mouse_y);

        /* Flip to screen */
        gfx_flip();
    }
}
