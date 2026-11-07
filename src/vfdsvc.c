/* vfdsvc — userspace driver for the front-panel 7-segment LED display of
 * Amlogic-based Android TV boxes (FD628/PT6964-family controllers), driven
 * via sysfs GPIO bit-banging: no kernel module, ABI-safe on vendor kernels.
 *
 * Field-tested on: Tanix TX92 (Amlogic S912, slimBOXtv ATV 9.20).
 *
 * Protocol, glyph tables and configuration format mirror the well-known
 * linux_openvfd driver by Arthur Liberman (https://github.com/arthur-liberman/
 * linux_openvfd) — thank you! So vfdsvc is GPL-3.0-or-later too.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 vfdsvc contributors
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>

#ifndef VFD_VERSION
#define VFD_VERSION "vfdsvc 0.1.0"
#endif

#ifndef DEFAULT_CONF
#define DEFAULT_CONF "/system/etc/vfdsvc.conf"
#endif

/* ---------------- configuration model (linux_openvfd conf format) ------- */

#define MAXBANK 6
static const char *bank_names[MAXBANK] = {
	"periphs-banks", "aobus-banks", "gpio0", "gpio1", "gpio2", "gpio3"
};

typedef struct { int bank, pin, flag; } conf_gpio;

static conf_gpio conf_clk = { 0, 76, 0 };   /* TX92 defaults  */
static conf_gpio conf_dat = { 0, 75, 0 };
static conf_gpio conf_stb = { 0, 53, 0 };

static int conf_chars[5] = { 4, 3, 2, 1, 0 };	/* < DHHMM > */
static int conf_dots[7]  = { 0, 1, 2, 3, 4, 5, 6 };

static int conf_type  = 1;		/* layout: 1 = T95/TX92-like */
static int conf_flags = 0;		/* reserved (bit6 low-freq unused w/ sysfs) */
static int conf_ctrl  = 0;		/* 0=FD628 1=FD620 2=TM1618 */

static int opt_brightness = 7;		/* 1..7, override from -b */
static int opt_standby = 1;		/* 0=keep 1=dim 2=off */
static int opt_standby_bright = 1;	/* 1..7: brightness level for standby=dim */
static int opt_tz_offset_sec = INT_MIN;	/* manual conf override; INT_MIN => auto */
static int tz_auto = 1;			/* auto-detect via bionic `date +%z` */
static time_t tz_next_check = 0;
static int opt_time_format = 0;		/* 0=auto, 12 or 24 forced */
static int hour12 = 0;			/* detected/effective 12h flag */

/* logical icons: 0 usb, 1 play, 2 colon, 3 eth, 4 wifi, 5 pause, 6 bt, 7 alarm */
static int icon_en[8] = { 1, 1, 1, 1, 1, 0, 0, 0 };
#define IL_USB 0
#define IL_PLAY 1
#define IL_COLON 2
#define IL_ETH 3
#define IL_WIFI 4
#define IL_PAUSE 5
#define IL_BT 6
#define IL_ALARM 7

/* media/alarm state sensors (configurable) */
static int play_src_vdec = 1, play_src_pcm = 1, play_src_wakelock = 1;
static int pause_mode_on = 1;		/* vdec alive & audio silent => paused */
static int alarm_method = 1;		/* 0 off, 1 auto(settings->dumpsys), 2 settings, 3 dumpsys */
static int alarm_interval = 30;
static int bt_src_rfkill = 1;
static int net_fallback_on = 1;		/* try eth1/wlan1 when eth0/wlan0 absent */
static char vdec_path[160] = "/sys/class/vdec/core";
static int icon_map[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };	/* logical icon -> dot 0..6 */
static time_t alarm_next_check = 0;
static int alarm_on = 0;

static int g_demo_h = 0, g_demo_m = 0;
static const char *conf_path = DEFAULT_CONF;

/* Runs the (bionic) date binary to learn the current UTC offset.
 * Android shells know the timezone (bionic tzdata); static glibc does not. */
static int detect_tz_offset(void)
{
	static const char *cands[] = {
		"/system/bin/date", "/system/xbin/date", "/bin/date", "date", NULL
	};
	int off = INT_MIN, i;

	for (i = 0; cands[i] != NULL; i++) {
		int pfd[2], pid, status, ntot = 0;
		char buf[16] = { 0 }, *argv[8] = { 0 };
		int argc = 0;
		char prog[256];

		snprintf(prog, sizeof(prog), "%s", cands[i]);
		argv[argc++] = "date"; argv[argc++] = "+%z";
		if (pipe(pfd) != 0) return INT_MIN;
		pid = fork();
		if (pid == 0) {
				{
					int d = open("/dev/null", O_WRONLY);
					if (d >= 0) { dup2(d, STDERR_FILENO); close(d); }
				}
				close(pfd[0]);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
			execv(prog, argv);
			_exit(127);
		} else if (pid < 0) {
			close(pfd[0]); close(pfd[1]);
			continue;
		}
		close(pfd[1]);
		for (;;) {
			ssize_t r = read(pfd[0], buf + ntot, sizeof(buf) - 1 - ntot);
			if (r <= 0) break;
			ntot += (int)r;
			if (ntot >= (int)sizeof(buf) - 1) break;
		}
		close(pfd[0]);
		waitpid(pid, &status, 0);
		if (ntot >= 5 && (buf[0] == '+' || buf[0] == '-')) {
			/* tolerate trailing '\n' */
			int sign = (buf[0] == '+') ? 1 : -1;
			int h = (isdigit((int)buf[1]) ? buf[1] - '0' : 0) * 10
			      + (isdigit((int)buf[2]) ? buf[2] - '0' : 0);
			int m = (isdigit((int)buf[3]) ? buf[3] - '0' : 0) * 10
			      + (isdigit((int)buf[4]) ? buf[4] - '0' : 0);
			off = sign * (h * 3600 + m * 60);
			break;
		}
	}
	return off;
}

/* Android 12/24-hour switch lives in Settings.System — ask the `settings` CLI.
 * Returns 12, 24 or INT_MIN on failure. */
static int detect_time_format(void)
{
	static const char *cands[] = {
		"/system/bin/settings", "/bin/settings", NULL
	};
	int fmt = INT_MIN, i;

	for (i = 0; cands[i] != NULL; i++) {
		int pfd[2], pid, status, ntot = 0;
		char buf[16] = { 0 }, *argv[8] = { 0 };
		int argc = 0;
		char prog[256];

		snprintf(prog, sizeof(prog), "%s", cands[i]);
		argv[argc++] = "settings"; argv[argc++] = "get";
		argv[argc++] = "system";   argv[argc++] = "TIME_12_24";
		if (pipe(pfd) != 0) return INT_MIN;
		pid = fork();
		if (pid == 0) {
			{
				int d = open("/dev/null", O_WRONLY);
				if (d >= 0) { dup2(d, STDERR_FILENO); close(d); }
			}
			close(pfd[0]);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
			execv(prog, argv);
			_exit(127);
		}
		close(pfd[1]);
		for (;;) {
			ssize_t r = read(pfd[0], buf + ntot, sizeof(buf) - 1 - ntot);
			if (r <= 0) break;
			ntot += (int)r;
			if (ntot >= (int)sizeof(buf) - 1) break;
		}
		close(pfd[0]);
		waitpid(pid, &status, 0);
		if (strncmp(buf, "12", 2) == 0) { fmt = 12; break; }
		if (strncmp(buf, "24", 2) == 0) { fmt = 24; break; }
	}
	return fmt;
}

static void refresh_format(void)
{
	if (opt_time_format != 0) { hour12 = (opt_time_format == 12); return; }
	{
		int f = detect_time_format();
		if (f != INT_MIN) hour12 = (f == 12);
		/* no permission/no provider: keep previous (defaults to 24h) */
	}
}

/* ---------------- small file & exec helpers ----------------------------- */

static int read_head(const char *path, char *buf, int cap)
{
	int f, r = -1;
	if (cap < 1) return -1;
	f = open(path, O_RDONLY);
	if (f < 0) return -1;
	r = (int)read(f, buf, (size_t)cap - 1);
	close(f);
	buf[r > 0 ? r : 0] = 0;
	return r;
}

/* fork/exec a small helper binary and capture stdout (used by alarm probes).
 * Returns number of bytes read (>=0), or -1 when nothing ran. */
static int run_capture(const char *const *progs, const char *a1, const char *a2,
	                   const char *a3, char *buf, int cap)
{
	char prog[256], *argv[8];
	int i, argc = 0, got = -1;

	argv[argc++] = (char *)progs[0]; argv[argc++] = (char *)a1; argv[argc++] = (char *)a2;
	if (a3) argv[argc++] = (char *)a3;
	argv[argc] = NULL;
	for (i = 0; progs[i] != NULL; i++) {
		int pfd[2], pid, status, ntot = 0;

		snprintf(prog, sizeof(prog), "%s", progs[i]);
		if (pipe(pfd) != 0) return -1;
		pid = fork();
		if (pid == 0) {
			int d = open("/dev/null", O_WRONLY);
			if (d >= 0) { dup2(d, STDERR_FILENO); close(d); }
			close(pfd[0]);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
			argv[0] = (char *)progs[i];
			execv(prog, argv);
			_exit(127);
		}
		if (pid < 0) { close(pfd[0]); close(pfd[1]); continue; }
		close(pfd[1]);
		for (;;) {
			ssize_t r = read(pfd[0], buf + ntot, cap - 1 - ntot);
			if (r <= 0) break;
			ntot += (int)r;
			if (ntot >= cap - 1) break;
		}
		close(pfd[0]);
		waitpid(pid, &status, 0);
		if (ntot > 0) { buf[ntot] = 0; got = ntot; break; }
	}
	return got;
}

/* ---------------- icon bit mapping -------------------------------------- */

/* Icon dot-index per display-type group (openvfd LED_DOT enums):
 * columns: USB, PLAY, COLON, ETH, WIFI, PAUSE, BT, ALARM */
static const int grp_icons[5][8] = {
	{ 1, 2, 4, 5, 6, 3, -1, 0 },	/* types 0,1 (TX92): ALARM,USB,PLAY,PAUSE,SEC,ETH,WIFI */
	{ 2,-1, 4,-1,-1,-1,-1,-1 },	/* type 2 (X92): APPS,SETUP,USB,CARD,SEC,HDMI,CVBS */
	{-1,-1, 4, 3, 5,-1,-1,-1 },	/* type 3 (ABOX): POWER,LAN,SEC,WIFI */
	{-1,-1, 4, 1, 2,-1, 0,-1 },	/* type 6 (M9_PRO): BT,ETH,WIFI,SPDIF,SEC,HDMI,AV */
	{ 1, 2, 4, 5, 6, 3,-1, 0 },	/* other/unknown -> group0 layout */
};

static int icon_group(void)
{
	switch (conf_type) {
	case 0: case 1: return 0;
	case 2: return 1;
	case 3: return 2;
	case 6: return 3;
	default: return 4;
	}
}

static int icon_bit(int which)	/* 0..7 logical icons, see defines above */
{
	if (which < 0 || which > 7) return -1;
	if (icon_map[which] >= 0) {
		int d = icon_map[which];
		if (d < 0 || d > 6) return -1;
		return 1 << conf_dots[d];
	}
	{
		int g = icon_group();
		int i = grp_icons[g][which];
		if (i < 0 || i > 6) return -1;
		return 1 << conf_dots[i];
	}
}

/* ---------------- glyph tables (from linux_openvfd glyphs.h) ------------ */
/* digit bitmaps for 4 supported layout/controller combos */
static const unsigned char seg_tabs[4][10] = {
	/* tab1 (display type 1, T95-like): a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40 */
	{ 0x3F, 0x30, 0x5B, 0x79, 0x74, 0x6D, 0x6F, 0x38, 0x7F, 0x7D },
	/* tab2 (default, others): a=0x08 b=0x10 c=0x20 d=0x01 e=0x02 f=0x04 g=0x40 */
	{ 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F },
	/* tab3 (G9SX): a=0x01 b=0x08 c=0x20 d=0x02 e=0x10 f=0x04 g=0x40 */
	{ 0x3F, 0x14, 0x5B, 0x57, 0x74, 0x67, 0x6F, 0x16, 0x7F, 0x77 },
	/* tab5 (TAP1-like): a=0x40 b=0x20 c=0x10 d=0x01 e=0x02 f=0x04 g=0x08 */
	{ 0x77, 0x06, 0x6B, 0x4F, 0x1E, 0x5D, 0x7D, 0x07, 0x7F, 0x5F },
};
static const unsigned char seg_blank = 0x00;
static const unsigned char seg_dash  = 0x40;

static const unsigned char *active_tab(void)
{
	switch (conf_type) {
	case 1: return seg_tabs[0];	/* T95-like */
	case 7: return seg_tabs[2];	/* G9SX */
	case 9: return seg_tabs[3];	/* TAP1-like */
	default: return seg_tabs[1];	/* default tab2 */
	}
}

/* ---------------- sysfs GPIO ------------------------------------------- */

static int fd_clk = -1, fd_dat = -1, fd_stb = -1;
static volatile sig_atomic_t g_running = 1;
static void on_signal(int s) { (void)s; g_running = 0; }

static int sysfs_write1(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	int r, len = (int)strlen(val);
	if (fd < 0) return -errno;
	r = write(fd, val, len);
	close(fd);
	return (r == len) ? 0 : -EIO;
}

static int pin_open(int bank, int pin)
{
	DIR *d;
	struct dirent *de;
	char best[256] = { 0 }, path[128], buf[16];
	FILE *f;
	int sysnum, fd, efd, len;

	if (bank < 0 || bank >= MAXBANK) { errno = EINVAL; return -1; }

	d = opendir("/sys/class/gpio");
	if (!d) { perror("vfdsvc: /sys/class/gpio"); return -1; }
	while ((de = readdir(d))) {
		char lp[320];
		if (strncmp(de->d_name, "gpiochip", 8)) continue;
		snprintf(lp, sizeof(lp), "/sys/class/gpio/%s/label", de->d_name);
		f = fopen(lp, "r");
		if (!f) continue;
		len = (int)fread(lp, 1, sizeof(lp) - 1, f);
		fclose(f);
		if (len <= 0) continue;
		lp[len] = 0;
		if (!strncmp(lp, bank_names[bank], strlen(bank_names[bank]))) {
			snprintf(best, sizeof(best), "%s", de->d_name);
			break;
		}
	}
	closedir(d);
	if (!best[0]) {
		fprintf(stderr, "vfdsvc: gpiochip '%s' not found\n", bank_names[bank]);
		return -1;
	}
	snprintf(path, sizeof(path), "/sys/class/gpio/%s/base", best);
	f = fopen(path, "r");
	if (!f) { perror(best); return -1; }
	if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
	fclose(f);
	sysnum = atoi(buf) + pin;

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", sysnum);
	if (access(path, 0) != 0) {
		snprintf(buf, sizeof(buf), "%d\n", sysnum);
		efd = open("/sys/class/gpio/export", O_WRONLY);
		if (efd < 0) { perror("vfdsvc: export"); return -1; }
		if (write(efd, buf, strlen(buf)) < 0) {
			close(efd);
			fprintf(stderr, "vfdsvc: cannot export gpio %d (a driver may claim these pins)\n", sysnum);
			return -1;
		}
		close(efd);
	}
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", sysnum);
	if (sysfs_write1(path, "out") < 0) {
		fprintf(stderr, "vfdsvc: cannot set gpio %d as output (pin may be claimed)\n", sysnum);
		return -1;
	}
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", sysnum);
	fd = open(path, O_WRONLY);
	return fd;
}

static inline void tw(int fd, char c)
{
	ssize_t x = pwrite(fd, &c, 1, 0);
	(void)x;
}

static void idles(void) { tw(fd_stb, '1'); tw(fd_dat, '1'); tw(fd_clk, '1'); }

/* FD628/PT6964 3-wire serial: per bit clk LOW -> data -> clk HIGH, LSB first */
static void byte_out(unsigned char b)
{
	int i;
	for (i = 0; i < 8; i++) {
		tw(fd_clk, '0');
		tw(fd_dat, (b & 0x01) ? '1' : '0');
		tw(fd_clk, '1');
		b >>= 1;
	}
}

static void frame_begin(void) { tw(fd_stb, '0'); }
static void frame_end(void)   { tw(fd_clk, '1'); tw(fd_stb, '1'); }

static void send_cmd(unsigned char c) { frame_begin(); byte_out(c); frame_end(); }

static void send_data(unsigned char addr, const unsigned char *d, int n)
{
	frame_begin();
	byte_out(0xC0 | addr);
	while (n--) byte_out(*d++);
	frame_end();
}

static void send_brightness(unsigned char lvl, int off)
{
	send_cmd(0x80 | (off ? 0x00 : ((lvl & 7) | 0x08)));
}

/* ---------------- compose & send the display --------------------------- */

static void init_controller(void)
{
	unsigned char zero[16];
	int n;
	switch (conf_ctrl) {
	case 1: /* FD620 */
		send_cmd(conf_type == 4 ? 0x00 : 0x01);	/* 4-dig / 5-dig mode */
		n = 10;
		break;
	case 2: /* TM1618 */
		send_cmd(conf_type == 5 ? 0x03 : (conf_type == 4 ? 0x00 : 0x01));
		n = 14;
		break;
	case 0: /* FD628 */
	default:
		send_cmd(0x03);		/* 7-digit mode */
		n = 14;
		break;
	}
	memset(zero, 0, sizeof(zero));
	send_data(0, zero, n);
	send_brightness((unsigned char)opt_brightness, 0);
}

static void compose_send(int colon_on, int h10, int h1, int m10, int m1, int icon_mask)
{
	unsigned short wdata[5], wbuf[8];
	unsigned char seq[16];
	int i, grids, len;
	const unsigned char *tab = active_tab();

	switch (conf_ctrl) {
	case 1: grids = 5; break;
	case 2: grids = 7; break;
	default: grids = 7; break;
	}
	memset(wbuf, 0, sizeof(wbuf));
	wdata[0] = (unsigned short)(icon_mask | (colon_on ? icon_bit(2) : 0));
	wdata[1] = (h10 >= 0 && h10 <= 9) ? tab[h10] : 0;	/* -1 = blank */
	wdata[2] = (h1  >= 0 && h1  <= 9) ? tab[h1]  : 0;
	wdata[3] = (m10 >= 0 && m10 <= 9) ? tab[m10] : 0;
	wdata[4] = (m1  >= 0 && m1  <= 9) ? tab[m1]  : 0;
	for (i = 0; i < 5; i++) {
		int pos = conf_chars[i];
		if (pos >= grids) pos = 4 - i;		/* fallback identity */
		wbuf[pos] = wdata[i];
	}
	for (i = 0; i < grids; i++) {
		switch (conf_ctrl) {
		case 1: wbuf[i] |= (wbuf[i] & 0x0080) ? 0x2000 : 0; break;
		case 2: wbuf[i] |= (unsigned short)((wbuf[i] & 0x00E0) << 6); break;
		default: wbuf[i] |= (unsigned short)((wbuf[i] & 0xFC00) << 1); break;
		}
	}
	len = 0;
	for (i = 0; i < grids; i++) {
		seq[len++] = (unsigned char)(wbuf[i] & 0xFF);
		seq[len++] = (unsigned char)((wbuf[i] >> 8) & 0xFF);
	}
	send_data(0, seq, len);
	(void)seg_blank; (void)seg_dash;
}

static void display_clear(void)
{
	unsigned char seq[16];
	int n = (conf_ctrl == 1) ? 10 : 14;
	memset(seq, 0, sizeof(seq));
	send_data(0, seq, n);
}

static void display_frame(int secs, int h10, int h1, int m10, int m1)
{
	int z;
	compose_send(1, h10, h1, m10, m1, 0);
	while (secs-- > 0 && g_running) {
		int t; for (t = 0; t < 10 && g_running; t++) usleep(100 * 1000);
	}
	(void)z;
}

static void display_all_segments(int secs)
{
	unsigned short wbuf[8];
	unsigned char seq[16];
	int i, len = 0, colon_mask = 0;
	for (i = 0; i < 7; i++) colon_mask |= (1 << conf_dots[i]);
	memset(wbuf, 0, sizeof(wbuf));
	for (i = 0; i < 5; i++) {
		int pos = conf_chars[i];
		if (pos >= 7) pos = 4 - i;
		wbuf[pos] = 0xFFFF;
	}
	(void)colon_mask;
	for (i = 0; i < 7; i++) {
		switch (conf_ctrl) {
		case 1: wbuf[i] |= (wbuf[i] & 0x0080) ? 0x2000 : 0; break;
		case 2: wbuf[i] |= (unsigned short)((wbuf[i] & 0x00E0) << 6); break;
		default: wbuf[i] |= (unsigned short)((wbuf[i] & 0xFC00) << 1); break;
		}
	}
	for (i = 0; i < 7; i++) {
		seq[len++] = (unsigned char)(wbuf[i] & 0xFF);
		seq[len++] = (unsigned char)((wbuf[i] >> 8) & 0xFF);
	}
	send_data(0, seq, (conf_ctrl == 1) ? 10 : 14);
	send_brightness(7, 0);
	while (secs-- > 0 && g_running) {
		int z; for (z = 0; z < 10 && g_running; z++) usleep(100 * 1000);
	}
}

/* ---------------- icons state ------------------------------------------ */

/* Sleep detection, in priority order:
 *  1) framebuffer blank flag (/sys/class/graphics/fb0/blank == 1..4)
 *  2) cached "dumpsys power" wakefulness probe (wake_lock on this firmware
 *     keeps PowerManagerService.Display even when the screen is off)
 *  3) legacy wake_lock signal (Display lock disappears when asleep)      */
static time_t awake_poll_next = 0;
static int awake_poll_secs = 2;		/* dumpsys probe cadence, 0 = off */
static int awake_cached = 1;
static int awake_probed = 0;

static int fb_blank_asleep(void)
{
	char buf[32];
	int n = read_head("/sys/class/graphics/fb0/blank", buf, (int)sizeof(buf));
	if (n <= 0) return -1;			/* file absent / write-only: unknown */
	{
		int v = atoi(buf);
		if (v <= 0) return 0;			/* 0 = screen active */
	}
	return 1;					/* 1..4 = blanked (4 = POWERDOWN) */
}

static int screen_awake(void)
{
	int b = fb_blank_asleep();
	if (b >= 0) return b ? 0 : 1;
	if (awake_poll_secs > 0 && time(NULL) >= awake_poll_next) {
		static const char *dumpsys[] = { "/system/bin/dumpsys", "/bin/dumpsys", NULL };
		char buf[8192];
		int n = run_capture(dumpsys, "power", NULL, NULL, buf, (int)sizeof(buf));
		if (n > 0) {
			awake_probed = 1;
			awake_cached = !(strstr(buf, "mWakefulness=Asleep") != NULL ||
				strstr(buf, "mHoldingDisplaySuspendBlocker=false") != NULL);
		}
		awake_poll_next = time(NULL) + awake_poll_secs;
	}
	if (awake_probed) return awake_cached;
	{
		char buf[4096];
		int n = read_head("/sys/power/wake_lock", buf, (int)sizeof(buf));
		if (n <= 0) return 1;		/* no signal file: assume awake */
		return strstr(buf, "PowerManagerService.Display") != NULL;
	}
}

static char has_file(const char *p) { return access(p, 0) == 0; }

/* ------ media state sensors (all cheap reads, evaluated ~1/s) ---------- */

static int media_pcm_running(void)
{
	static const char *devs[] = { "card0/pcm0p", "card0/pcm1p", "card0/pcm2p", NULL };
	char path[96], buf[48];
	int i, on = 0, n;
	for (i = 0; devs[i] != NULL; i++) {
		snprintf(path, sizeof(path), "/proc/asound/%s/sub0/status", devs[i]);
		n = read_head(path, buf, (int)sizeof(buf));
		if (n > 0 && strstr(buf, "state: RUNNING")) { on = 1; break; }
	}
	return on;
}

static int media_vdec_busy(void)
{
	char buf[256];
	int n = read_head(vdec_path, buf, (int)sizeof(buf));
	return (n > 0 && strstr(buf, "VDEC_STATUS_CONNECTED")) ? 1 : 0;
}

static int wakelock_playback(void)
{
	char buf[4096];
	int n = read_head("/sys/power/wake_lock", buf, (int)sizeof(buf));
	if (n <= 0) return 0;
	return (strstr(buf, "Playback") || strstr(buf, "playback")) ? 1 : 0;
}

static int sensor_play(void)
{
	if (!icon_en[IL_PLAY]) return 0;
	if (play_src_vdec && media_vdec_busy()) return 1;
	if (play_src_pcm && media_pcm_running()) return 1;
	if (play_src_wakelock && wakelock_playback()) return 1;
	return 0;
}

static int sensor_pause(void)
{
	if (!icon_en[IL_PAUSE] || !pause_mode_on) return 0;
	/* video decoder alive while audio is not flowing => paused (or muted) */
	return (media_vdec_busy() && !media_pcm_running()) ? 1 : 0;
}

static int sensor_bt(void)
{
	DIR *d;
	struct dirent *de;
	int on = 0;

	if (!icon_en[IL_BT] || !bt_src_rfkill) return 0;
	d = opendir("/sys/class/rfkill");
	if (!d) return 0;
	while ((de = readdir(d)) && !on) {
		char p[96], b[32];
		if (strncmp(de->d_name, "rfkill", 6)) continue;
		snprintf(p, sizeof(p), "/sys/class/rfkill/%s/type", de->d_name);
		if (read_head(p, b, (int)sizeof(b)) <= 0 || strncmp(b, "bluetooth", 9)) continue;
		snprintf(p, sizeof(p), "/sys/class/rfkill/%s/state", de->d_name);
		if (read_head(p, b, (int)sizeof(b)) > 0 && (atoi(b) & 1)) on = 1;
	}
	closedir(d);
	return on;
}

static void alarm_check(void)
{
	char buf[16384];
	int n;

	if (alarm_method == 0) { alarm_on = 0; return; }
	if (alarm_method == 1 || alarm_method == 2) {
		static const char *settings[] = { "/system/bin/settings", "/bin/settings", NULL };
		n = run_capture(settings, "get", "global", "next_alarm_formatted", buf, 128);
		if (n > 0 && buf[0] && strncmp(buf, "null", 4) != 0) {
			alarm_on = 1;
			alarm_next_check = time(NULL) + alarm_interval;
			return;
		}
		if (alarm_method == 2) { alarm_on = 0; alarm_next_check = time(NULL) + alarm_interval; return; }
	}
	{
		static const char *dumpsys[] = { "/system/bin/dumpsys", "/bin/dumpsys", NULL };
		n = run_capture(dumpsys, "alarm", NULL, NULL, buf, (int)sizeof(buf));
		alarm_on = (n > 0 && strstr(buf, "mNextAlarmClock=AlarmClock")) ? 1 : 0;
	}
	alarm_next_check = time(NULL) + alarm_interval;
}

static int sensor_alarm(void)
{
	if (!icon_en[IL_ALARM] || alarm_method == 0) return 0;
	return alarm_on;
}

static int if_operstate_up(const char *ifc)
{
	char path[64], buf[16];
	int n = 0;

	snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifc);
	if (access(path, 0) != 0) return -1;		/* interface absent */
	n = read_head(path, buf, (int)sizeof(buf));
	return (n > 0 && strncmp(buf, "up", 2) == 0) ? 1 : 0;
}

static int icon_is_on(int which)
{
	switch (which) {
	case IL_USB:
		return icon_en[IL_USB] &&
			(has_file("/storage/usb_storage") || has_file("/dev/block/sda"));
	case IL_PLAY:	return sensor_play();
	case IL_PAUSE:	return sensor_pause();
	case IL_BT:	return sensor_bt();
	case IL_ALARM:	return sensor_alarm();
	case IL_ETH: {
		int up = if_operstate_up("eth0");
		if (up < 0 && net_fallback_on) up = if_operstate_up("eth1");
		return icon_en[IL_ETH] && up == 1;
	}
	case IL_WIFI: {
		int up = if_operstate_up("wlan0");
		if (up < 0 && net_fallback_on) up = if_operstate_up("wlan1");
		return icon_en[IL_WIFI] && up == 1;
	}
	default: return 0;
	}
}

static int icons_mask(void)
{
	int m = 0, i;
	for (i = 0; i < 8; i++) {
		if (i == IL_COLON) continue;		/* colon handled separately */
		if (icon_is_on(i)) {
			int b = icon_bit(i);
			if (b > 0) m |= b;
		}
	}
	return m;
}

/* ---------------- config parsing --------------------------------------- */

static char *strip(char *s)
{
	char *e;
	while (isspace((int)*s)) s++;
	e = s + strlen(s);
	while (e > s && isspace((int)e[-1])) *--e = 0;
	return s;
}

static int parse_ints(char *v, int *out, int max)
{
	int n = 0;
	char *tok = strtok(v, ", \t");
	while (tok && n < max) {
		out[n++] = (int)strtol(tok, NULL, 0);
		tok = strtok(NULL, ", \t");
	}
	return n;
}

static void dequote(char *v)
{
	size_t l = strlen(v);
	if (l >= 2 && ((v[0] == '\'' && v[l - 1] == '\'') || (v[0] == '"' && v[l - 1] == '"'))) {
		v[l - 1] = 0;
		memmove(v, v + 1, l - 1);
	}
}

static const struct { const char *name; int idx; } icon_names[8] = {
	{ "usb", 0 }, { "play", 1 }, { "colon", 2 }, { "eth", 3 },
	{ "wifi", 4 }, { "pause", 5 }, { "bt", 6 }, { "alarm", 7 }
};

static void parse_icon_map(char *v)
{
	char *tok = strtok(v, ", ");
	while (tok) {
		char *eq = strchr(tok, '=');
		int i;
		if (eq) {
			int d = (int)strtol(eq + 1, NULL, 0);
			*eq = 0;
			for (i = 0; i < 8; i++)
				if (!strcmp(tok, icon_names[i].name)) { icon_map[i] = d; break; }
		}
		tok = strtok(NULL, ", ");
	}
}

static int parse_conf(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[512];
	int have = 0;
	if (!f) return -1;
	while (fgets(line, sizeof(line), f)) {
		char *p = strip(line), *eq, *k, *v;
		if (*p == 0 || *p == '#' || *p == '/') continue;
		eq = strchr(p, '=');
		if (!eq) continue;
		*eq = 0;
		k = strip(p);
		v = strip(eq + 1);
		dequote(v);
		if (!strcmp(k, "vfd_gpio_clk")) { parse_ints(v, (int *)&conf_clk, 3); have = 1; }
		else if (!strcmp(k, "vfd_gpio_dat")) { parse_ints(v, (int *)&conf_dat, 3); have = 1; }
		else if (!strcmp(k, "vfd_gpio_stb")) { parse_ints(v, (int *)&conf_stb, 3); have = 1; }
		else if (!strcmp(k, "vfd_chars")) { int t[8]; if (parse_ints(v, t, 8) >= 5) { int i; for (i = 0; i < 5; i++) conf_chars[i] = t[i]; have = 1; } }
		else if (!strcmp(k, "vfd_dot_bits")) { int t[8]; if (parse_ints(v, t, 8) >= 7) { int i; for (i = 0; i < 7; i++) conf_dots[i] = t[i]; have = 1; } }
		else if (!strcmp(k, "vfd_display_type")) { int t[8]; if (parse_ints(v, t, 8) >= 4) { conf_type = t[0]; conf_flags = t[2]; conf_ctrl = t[3]; have = 1; } }
		else if (!strcmp(k, "brightness")) opt_brightness = atoi(v) & 7;
		else if (!strcmp(k, "tz_offset")) {
			int sign = 1, h = 0, m = 0;
			const char *p = v;
			if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
			if (strlen(p) >= 4) { h = (p[0]-'0')*10 + (p[1]-'0'); m = (p[2]-'0')*10 + (p[3]-'0'); }
			opt_tz_offset_sec = sign * (h * 3600 + m * 60);
			tz_auto = 0;	/* explicit conf value wins over auto-detect */
		}
		else if (!strcmp(k, "time_format")) {
			if (v[0] == '1' && v[1] == '2') opt_time_format = 12;
			else if (v[0] == '2' && v[1] == '4') opt_time_format = 24;
			else opt_time_format = 0;	/* auto */
		}
		else if (!strcmp(k, "standby")) {
			if (!strncmp(v, "off", 3)) opt_standby = 2;
			else if (!strncmp(v, "keep", 4)) opt_standby = 0;
			else opt_standby = 1;
		}
		else if (!strcmp(k, "standby_brightness")) {
			opt_standby_bright = atoi(v);
			if (opt_standby_bright < 1) opt_standby_bright = 1;
			if (opt_standby_bright > 7) opt_standby_bright = 7;
		}
		else if (!strcmp(k, "awake_poll")) {
			awake_poll_secs = atoi(v);
			if (awake_poll_secs < 0) awake_poll_secs = 0;
			if (awake_poll_secs > 60) awake_poll_secs = 60;
		}
		else if (!strcmp(k, "icons")) {
			icon_en[IL_USB]   = strstr(v, "usb")   ? 1 : 0;
			icon_en[IL_PLAY]  = strstr(v, "play")  ? 1 : 0;
			icon_en[IL_PAUSE] = strstr(v, "pause") ? 1 : 0;
			icon_en[IL_BT]    = strstr(v, "bt")    ? 1 : 0;
			icon_en[IL_ALARM] = strstr(v, "alarm") ? 1 : 0;
			icon_en[IL_ETH]   = strstr(v, "eth")   ? 1 : 0;
			icon_en[IL_WIFI]  = strstr(v, "wifi")  ? 1 : 0;
		}
		else if (!strcmp(k, "play_sources")) {
			play_src_vdec     = strstr(v, "vdec")  ? 1 : 0;
			play_src_pcm      = strstr(v, "pcm")   ? 1 : 0;
			play_src_wakelock = strstr(v, "lock")  ? 1 : 0;
		}
		else if (!strcmp(k, "pause_mode"))	pause_mode_on = strncmp(v, "off", 3) != 0;
		else if (!strcmp(k, "alarm_method")) {
			if (!strncmp(v, "off", 3)) alarm_method = 0;
			else if (!strncmp(v, "settings", 8)) alarm_method = 2;
			else if (!strncmp(v, "dumpsys", 7)) alarm_method = 3;
			else alarm_method = 1;
		}
		else if (!strcmp(k, "alarm_interval")) {
			alarm_interval = atoi(v);
			if (alarm_interval < 5) alarm_interval = 5;
			if (alarm_interval > 3600) alarm_interval = 3600;
		}
		else if (!strcmp(k, "bt_source"))	bt_src_rfkill = strncmp(v, "off", 3) != 0;
		else if (!strcmp(k, "net_fallback"))	net_fallback_on = strncmp(v, "off", 3) != 0;
		else if (!strcmp(k, "vdec_file"))	snprintf(vdec_path, sizeof(vdec_path), "%s", v);
		else if (!strcmp(k, "icon_map"))	parse_icon_map(v);
	}
	fclose(f);
	return have ? 0 : -2;
}

/* ---------------- main -------------------------------------------------- */

static void usage(void)
{
	printf("Usage: vfdsvc [options]\n"
	       "  -c FILE       config path (default %s)\n"
	       "  -b 1..7       brightness override\n"
	       "  -s MODE       standby: 0=keep 1=dim 2=off\n"
	       "  -f MODE       time format: auto|12|24 (default auto)\n"
	       "  --test [sec]  light all segments+icons for sec (default 10), then exit\n"
	       "  --version     version info\n"
	       "  --help        this help\n", DEFAULT_CONF);
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	int i, test_secs = 10, do_test = 0;
	int cli_time_format = 0, cli_brightness = -1, cli_standby = -1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c") && i + 1 < argc) conf_path = argv[++i];
		else if (!strcmp(argv[i], "-b") && i + 1 < argc) cli_brightness = atoi(argv[++i]) & 7;
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) cli_standby = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
			const char *m = argv[++i];
			if (m[0] == '1' && m[1] == '2') cli_time_format = 12;
			else if (m[0] == '2' && m[1] == '4') cli_time_format = 24;
			else cli_time_format = -1;
		}
		else if (!strcmp(argv[i], "--demo") && argc - i >= 3 &&
			strchr(argv[i + 1], ':')) {
			int h = -1, mnt = -1;
			sscanf(argv[i + 1], "%d:%d", &h, &mnt);
			do_test = 2;
			test_secs = argc - i >= 3 ? atoi(argv[i + 2]) : 10;
			g_demo_h = h; g_demo_m = mnt;
			i += argc - i >= 3 ? 2 : 1;
		}
		else if (!strcmp(argv[i], "--test")) { do_test = 1; if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') test_secs = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--version")) { printf("%s\n", VFD_VERSION); return 0; }
		else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
		else { fprintf(stderr, "unknown option: %s\n", argv[i]); usage(); return 2; }
	}

	if (parse_conf(conf_path) != 0)
		fprintf(stderr, "vfdsvc: config '%s' not found/partial, using defaults\n", conf_path);

	/* CLI wins over conf */
	if (cli_time_format != 0) opt_time_format = cli_time_format;
	if (cli_brightness >= 1) opt_brightness = cli_brightness;
	if (cli_standby >= 0 && cli_standby <= 2) opt_standby = cli_standby;

	refresh_format();

	fd_clk = pin_open(conf_clk.bank, conf_clk.pin);
	fd_dat = pin_open(conf_dat.bank, conf_dat.pin);
	fd_stb = pin_open(conf_stb.bank, conf_stb.pin);
	if (fd_clk < 0 || fd_dat < 0 || fd_stb < 0) {
		fprintf(stderr, "vfdsvc: failed to claim GPIOs (clk=%d/%d dat=%d/%d stb=%d/%d)\n",
			conf_clk.bank, conf_clk.pin, conf_dat.bank, conf_dat.pin, conf_stb.bank, conf_stb.pin);
		return 1;
	}
	idles();
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	init_controller();

	if (tz_auto) {
		int off = detect_tz_offset();
		if (off != INT_MIN) {
			opt_tz_offset_sec = off;
			tz_next_check = time(NULL) + 3600;
		}
	}

	if (do_test) {
		if (do_test == 2) {
			int hh = g_demo_h, mm = g_demo_m, h10, h1;
			if (hour12) {
				hh = hh % 12; if (hh == 0) hh = 12;
			}
			h10 = hh / 10;
			if (hour12 && h10 == 0) h10 = -1;
			h1 = hh % 10;
			display_frame(test_secs, h10, h1, mm / 10, mm % 10);
		} else {
			display_all_segments(test_secs);
		}
		display_clear();
		send_brightness((unsigned char)opt_brightness, 1);
		idles();
		return 0;
	}

	{
		int last_state = -1;
		refresh_format();
	while (g_running) {
		int awake = screen_awake();
		time_t now = time(NULL);
		struct tm tv;
		int colon, h10, h1, m10, m1;
		if (awake == 0) {
			if (last_state != 3) {
				if (opt_standby == 2) send_brightness((unsigned char)opt_brightness, 1);
				else if (opt_standby == 1) send_brightness((unsigned char)opt_standby_bright, 0);
				last_state = 3;
			}
			for (i = 0; i < 12 && g_running; i++) usleep(250 * 1000);
			continue;
		}
		if (last_state == 3) { send_brightness((unsigned char)opt_brightness, 0); }
		last_state = awake;
		if (time(NULL) >= alarm_next_check)
			alarm_check();
		if (time(NULL) >= tz_next_check) {
			if (tz_auto) {
				int off = detect_tz_offset();
				if (off != INT_MIN) {
					opt_tz_offset_sec = off;
				}
			}
			if (opt_time_format == 0) refresh_format();
			tz_next_check = time(NULL) + 3600;
		}
		if (opt_tz_offset_sec != INT_MIN) {
			now = time(NULL) + opt_tz_offset_sec;
			gmtime_r(&now, &tv);
		} else {
			localtime_r(&now, &tv);
		}
		colon = (tv.tm_sec % 2) == 0;
		{
			int hh = tv.tm_hour;
			if (hour12) {
				hh = hh % 12;
				if (hh == 0) hh = 12;
			}
			h10 = hh / 10;
			if (hour12 && h10 == 0) h10 = -1;	/* 12h: hide leading zero */
			h1  = hh % 10;
			m10 = tv.tm_min / 10;
			m1  = tv.tm_min % 10;
		}
		compose_send(colon, h10, h1, m10, m1, icons_mask());
		for (i = 0; i < 4 && g_running; i++) usleep(250 * 1000);
	}
	}

	display_clear();
	send_brightness((unsigned char)opt_brightness, 1);
	idles();
	return 0;
}
