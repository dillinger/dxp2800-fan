/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * itefan - read / set fan PWM on an ITE IT8613E Super I/O
 *          (UGREEN NASync DXP2800). Part of dxp2800-fan.
 *
 * Register map taken from the Linux it87 driver (newer-autopwm chips):
 *   PWM control : 0x15 0x16 0x17 0x7f 0xa7   (bit 7 set = automatic)
 *   PWM duty    : 0x63 0x6b 0x73 0x7b 0xa3   (8-bit, 0..255)
 *   Fan tach    : 0x0d/0x18 0x0e/0x19 0x0f/0x1a 0x80/0x81 0x82/0x83
 *   Temps       : 0x29 0x2a 0x2b
 *
 * USE AT YOUR OWN RISK. A fan left in manual mode at a low duty will
 * NOT be rescued by firmware if the box overheats.
 *
 * Build (FreeBSD):  cc -O2 -Wall -o itefan itefan.c
 * Run as root (needs /dev/io).
 *
 *   itefan                 show chip, temps, fans, PWM modes/duty (read-only)
 *   itefan set CH DUTY     put channel CH (1-5) in manual mode, duty 0-255
 *   itefan pct CH PERCENT  same, but 0-100 %
 *   itefan auto CH         hand channel CH back to the chip's automatic mode
 *
 * Options (before the command):
 *   -p 0x2e|0x4e   Super I/O config port (default: probe both)
 *   -f             skip the chip-ID check
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/io.h>		/* iopl() */
#endif

/*
 * x86 port I/O, done here instead of via <machine/cpufunc.h> or
 * <sys/io.h> so the file is plain C on every OS (editors and language
 * servers on other systems stop complaining). Getting permission to use
 * these instructions is OS-specific and handled in io_open() below.
 */
static inline void IO_OUT(uint16_t port, uint8_t val)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
#else
	(void)port; (void)val;
	abort();		/* no x86 port I/O on this CPU */
#endif
}

static inline uint8_t IO_IN(uint16_t port)
{
#if defined(__x86_64__) || defined(__i386__)
	uint8_t val;
	__asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
#else
	(void)port;
	abort();
#endif
}

/* Ask the OS for I/O port access; exits on failure. */
static void io_open(void)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
	if (open("/dev/io", O_RDWR) < 0)
		err(1, "open /dev/io (run as root)");
#elif defined(__linux__)
	if (iopl(3) < 0)
		err(1, "iopl (run as root)");
#else
	errx(1, "unsupported OS: needs FreeBSD (/dev/io) or Linux (iopl)");
#endif
#if !defined(__x86_64__) && !defined(__i386__)
	errx(1, "unsupported CPU: needs x86 port I/O");
#endif
}

#define NCH 5

static const uint8_t REG_PWM_CTRL[NCH] = { 0x15, 0x16, 0x17, 0x7f, 0xa7 };
static const uint8_t REG_PWM_DUTY[NCH] = { 0x63, 0x6b, 0x73, 0x7b, 0xa3 };
static const uint8_t REG_FAN_LO[NCH]   = { 0x0d, 0x0e, 0x0f, 0x80, 0x82 };
static const uint8_t REG_FAN_HI[NCH]   = { 0x18, 0x19, 0x1a, 0x81, 0x83 };
static const uint8_t REG_TEMP[3]       = { 0x29, 0x2a, 0x2b };

#define REG_FAN_MAIN_CTRL 0x13
#define REG_FAN_CTL       0x14
#define LDN_EC            0x04

static uint16_t ec_base;

/* ---------- Super I/O config space ---------- */

static void sio_enter(uint16_t port)
{
	IO_OUT(port, 0x87);
	IO_OUT(port, 0x01);
	IO_OUT(port, 0x55);
	IO_OUT(port, port == 0x4e ? 0xaa : 0x55);
}

static void sio_exit(uint16_t port)
{
	IO_OUT(port, 0x02);
	IO_OUT(port + 1, 0x02);
}

static uint8_t sio_read(uint16_t port, uint8_t reg)
{
	IO_OUT(port, reg);
	return IO_IN(port + 1);
}

static void sio_write(uint16_t port, uint8_t reg, uint8_t val)
{
	IO_OUT(port, reg);
	IO_OUT(port + 1, val);
}

/* Returns chip id, fills ec_base. */
static uint16_t sio_probe(uint16_t port)
{
	uint16_t id;

	sio_enter(port);
	id = (uint16_t)(sio_read(port, 0x20) << 8 | sio_read(port, 0x21));
	if ((id >> 8) == 0x86 || (id >> 8) == 0x87) {
		sio_write(port, 0x07, LDN_EC);
		ec_base = (uint16_t)(sio_read(port, 0x60) << 8 |
		    sio_read(port, 0x61));
	}
	sio_exit(port);
	return id;
}

/* ---------- Environment controller ---------- */

static uint8_t ec_read(uint8_t reg)
{
	IO_OUT(ec_base + 5, reg);
	return IO_IN(ec_base + 6);
}

static void ec_write(uint8_t reg, uint8_t val)
{
	IO_OUT(ec_base + 5, reg);
	IO_OUT(ec_base + 6, val);
}

static unsigned fan_rpm(int ch)
{
	unsigned cnt = ec_read(REG_FAN_LO[ch]) |
	    (unsigned)ec_read(REG_FAN_HI[ch]) << 8;

	if (cnt == 0 || cnt == 0xffff)
		return 0;
	return 1350000U / (cnt * 2U);
}

static void show(void)
{
	int i;

	printf("EC base 0x%04x  fan_main_ctrl=0x%02x  fan_ctl=0x%02x"
	    " (PWM polarity %s)\n", ec_base,
	    ec_read(REG_FAN_MAIN_CTRL), ec_read(REG_FAN_CTL),
	    (ec_read(REG_FAN_CTL) & 0x80) ? "active-high" : "active-low");

	for (i = 0; i < 3; i++) {
		int8_t t = (int8_t)ec_read(REG_TEMP[i]);
		printf("temp%d: %d C\n", i + 1, t);
	}
	printf("\n ch  fan_rpm  pwm_ctrl  mode    duty\n");
	for (i = 0; i < NCH; i++) {
		uint8_t c = ec_read(REG_PWM_CTRL[i]);
		uint8_t d = ec_read(REG_PWM_DUTY[i]);
		printf(" %d   %6u   0x%02x     %-6s  %3u (%3u%%)\n",
		    i + 1, fan_rpm(i), c, (c & 0x80) ? "auto" : "manual",
		    d, (d * 100U + 127) / 255U);
	}
	printf("\nNote: on the IT8613E not every channel is wired; on the"
	    " DXP2800\nthe fan is reportedly on channel 2 or 3. Channels"
	    " with 0 rpm\nand junk values are probably unconnected.\n");
}

static int parse_ch(const char *s)
{
	char *e;
	long v = strtol(s, &e, 0);

	if (*e != '\0' || v < 1 || v > NCH)
		errx(1, "channel must be 1..%d", NCH);
	return (int)v - 1;
}

static long parse_num(const char *s, long lo, long hi, const char *what)
{
	char *e;
	long v = strtol(s, &e, 0);

	if (e != s && e[0] == '%' && e[1] == '\0')	/* allow "30%" */
		e++;
	if (e == s || *e != '\0' || v < lo || v > hi)
		errx(1, "%s must be %ld..%ld", what, lo, hi);
	return v;
}

static void set_manual(int ch, uint8_t duty)
{
	uint8_t c = ec_read(REG_PWM_CTRL[ch]);

	/* duty first, then switch to manual, so there is no stale-duty blip */
	ec_write(REG_PWM_DUTY[ch], duty);
	ec_write(REG_PWM_CTRL[ch], c & 0x7f);

	if (duty < 50)
		fprintf(stderr, "warning: duty %u is very low; make sure the"
		    " fan still spins and temps stay sane\n", duty);
	printf("ch%d: manual, duty %u (%u%%)\n", ch + 1, duty,
	    (duty * 100U + 127) / 255U);
}

static void set_auto(int ch)
{
	uint8_t c = ec_read(REG_PWM_CTRL[ch]);

	ec_write(REG_PWM_CTRL[ch], c | 0x80);
	printf("ch%d: automatic\n", ch + 1);
}

static void usage(void)
{
	fprintf(stderr,
	    "usage: itefan [-f] [-p port] [show]\n"
	    "       itefan [-f] [-p port] set  CH DUTY(0-255)\n"
	    "       itefan [-f] [-p port] pct  CH PERCENT(0-100)\n"
	    "       itefan [-f] [-p port] auto CH\n");
	exit(2);
}

int main(int argc, char **argv)
{
	uint16_t ports[2] = { 0x2e, 0x4e }, id = 0;
	int nports = 2, force = 0, opt, i;

	while ((opt = getopt(argc, argv, "fp:")) != -1) {
		switch (opt) {
		case 'f': force = 1; break;
		case 'p':
			ports[0] = (uint16_t)strtol(optarg, NULL, 0);
			nports = 1;
			break;
		default: usage();
		}
	}
	argc -= optind;
	argv += optind;

	io_open();

	for (i = 0; i < nports; i++) {
		ec_base = 0;
		id = sio_probe(ports[i]);
		if (ec_base != 0 && ec_base != 0xffff)
			break;
	}
	if (ec_base == 0 || ec_base == 0xffff)
		errx(1, "no ITE Super I/O found (last id 0x%04x)", id);

	printf("ITE chip id 0x%04x at config port 0x%02x\n", id, ports[i]);
	if (id != 0x8613 && !force)
		errx(1, "not an IT8613E; register map may differ"
		    " (use -f to override)");

	if (argc == 0 || strcmp(argv[0], "show") == 0) {
		show();
	} else if (strcmp(argv[0], "set") == 0 && argc == 3) {
		set_manual(parse_ch(argv[1]),
		    (uint8_t)parse_num(argv[2], 0, 255, "duty"));
	} else if (strcmp(argv[0], "pct") == 0 && argc == 3) {
		long p = parse_num(argv[2], 0, 100, "percent");
		set_manual(parse_ch(argv[1]), (uint8_t)((p * 255 + 50) / 100));
	} else if (strcmp(argv[0], "auto") == 0 && argc == 2) {
		set_auto(parse_ch(argv[1]));
	} else {
		usage();
	}
	return 0;
}
