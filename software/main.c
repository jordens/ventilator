#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <irq.h>
#include <uart.h>
#include <generated/csr.h>
#include <console.h>

#define FIXEDPT_WBITS 24
#include "fixedptc.h"

#include "ventilator.h"

#if 1
	#define TTL_INPUTS  0x0000005a
	#define TTL_OUTPUTS 0x000000a5
	#define TTL_OE		0x00300000
#else
	#define TTL_INPUTS  0x0000000f
	#define TTL_OUTPUTS 0x000ffff0
	#define TTL_OE		0x00300000
#endif

static void ttl_init(void)
{
	const ventilator_event_t ev[9] = {
		{0, VENTILATOR_CTRL_CLEAR_FORCE, 1},
		{0, VENTILATOR_CTRL_START_IN, 0},
		{0, VENTILATOR_CTRL_START_OUT, 0},
		{0, VENTILATOR_CTRL_PROHIBIT_UNDERFLOW, 0},
		{0, VENTILATOR_GPIO_INV, 0},
		{0, VENTILATOR_GPIO_SENSE_RISE, TTL_INPUTS},
		{0, VENTILATOR_GPIO_SENSE_FALL, 0},
		{0, VENTILATOR_GPIO_O, TTL_OE},
		{0, VENTILATOR_GPIO_OE, TTL_OE | TTL_OUTPUTS},
	};
	ventilator_stop();
	ventilator_push_many(ev, len(ev), 0);
	ventilator_start();
}

static void ti(void)
{
	ventilator_event_t ev;
	while (ventilator_pop(&ev, 1)) {
		printf("0x%08x 0x%08x 0x%08x\n", ev.time, ev.addr, ev.data);
	}
}

static void to(char* time, char* addr, char* data)
{
	char *c;
	ventilator_event_t ev;
	if ((*time == 0) || (*addr == 0) || (*data == 0)) {
		printf("to <time> <addr> <data>\n");
		return;
	}
	ev.time = strtoul(time, &c, 0);
	if(*c != 0) {
		printf("invalid time\n");
		return;
	}
	ev.addr = strtoul(addr, &c, 0);
	if(*c != 0) {
		printf("invalid addr\n");
		return;
	}
	ev.data = strtoul(data, &c, 0);
	if(*c != 0) {
		printf("invalid data\n");
		return;
	}
	ventilator_push(&ev, 0);
}

static void tp(void)
{
	uint32_t status = ventilator_ev_status_read();
	printf("status: 0x%08x\n", status);
	printf("in readable: %d\n", !!(status & VENTILATOR_EV_IN_READABLE));
	printf("out overflow: %d\n", !!(status & VENTILATOR_EV_OUT_OVERFLOW));
	printf("in overflow: %d\n", !!(status & VENTILATOR_EV_IN_OVERFLOW));
	printf("out readable: %d\n", !!(status & VENTILATOR_EV_OUT_READABLE));
	printf("run: %d\n", ventilator_ctrl_run_read());
	ventilator_ctrl_update_write(0);
	printf("cycle: 0x%08x\n", ventilator_ctrl_cycle_read());
}


static void leds(char *value)
{
	char *c;
	unsigned int value2;
	if(*value == 0) {
		puts("leds <value>");
		return;
	}
	value2 = strtoul(value, &c, 0);
	if(*c != 0) {
		puts("incorrect value");
		return;
	}
	leds_out_write(value2);
}

static void inputs(void)
{
	ventilator_push1(0, VENTILATOR_GPIO_I, 0, 0);
	ti();
}

static void ttlout(char *value)
{
	char *c;
	unsigned int value2;
	if(*value == 0) {
		puts("ttlout <value>");
		return;
	}
	value2 = strtoul(value, &c, 0);
	if(*c != 0) {
		puts("incorrect value");
		return;
	}
	ventilator_push1(0, VENTILATOR_GPIO_O, (value2 & TTL_OUTPUTS) | TTL_OE, 0);
}

static void ttlin(void)
{
	ventilator_push1(0, VENTILATOR_GPIO_OE, TTL_OE, 0);
	ventilator_push1(0, VENTILATOR_GPIO_I, 0, 0);
	ti();
}

static void inputs_read(void)
{
	ventilator_push1(0, VENTILATOR_GPIO_SENSE_RISE, TTL_INPUTS, 0);
	ventilator_push1(0, VENTILATOR_GPIO_SENSE_FALL, TTL_INPUTS, 0);
	while (!readchar_nonblock())
		ti();
	ttl_init();
}

static void freq(void)
{
	int n;
	ventilator_event_t ev;
	ventilator_push1(0, VENTILATOR_GPIO_SENSE_RISE, TTL_INPUTS, 0);
	ventilator_push1(0, VENTILATOR_GPIO_SENSE_FALL, 0, 0);
	timer0_en_write(0);
	timer0_reload_write(0);
	timer0_load_write(identifier_frequency_read()/10);
	while (!readchar_nonblock()) {
		n = 0;
		timer0_en_write(1);
		timer0_update_value_write(1);
		while (timer0_value_read()) {
			while (ventilator_pop(&ev, 1))
				n++;
			timer0_update_value_write(1);
		}
		timer0_en_write(0);
		printf("\e[8D%08d", n);
		uart_sync();
	}
	puts("");
	ttl_init();
}

static void pulses(void)
{
	int j, k, t = 0x1000, dt = identifier_frequency_read()/100000;
	ventilator_event_t ev[2];
	ventilator_push1(0, VENTILATOR_CTRL_CLEAR_FORCE, 0, 0);

	ev[0].data = TTL_OE | 0x0000a0;
	ev[1].data = TTL_OE | 0x000050;
	while (1) {
		for (k=0; k<8; k++) {
			for (j=8; j<80; j++) {
				t += dt;
				if (readchar_nonblock()) {
					ttl_init();
					return;
				}
				ev[0].time = t;
				ev[0].addr = VENTILATOR_GPIO_O | (k<<4);
				ev[1].time = t + j/8;
				ev[1].addr = VENTILATOR_GPIO_O | ((j%8)<<4);
				ventilator_push_many(ev, 2, 0);
			}
		}
	}
}

static int tl1(int buf, int mode)
{
#define TL_PIN 0x01
	uint32_t t = 0, temp;
	ventilator_event_t eva;
	const ventilator_event_t ev_start[7] = {
		{0, VENTILATOR_CTRL_CLEAR_FORCE, 0},
		{1, VENTILATOR_GPIO_O, 0x00},
		{2, VENTILATOR_GPIO_OE, TL_PIN},
		{3, VENTILATOR_GPIO_SENSE_FALL, 0x00},
		{4, VENTILATOR_GPIO_SENSE_RISE, TL_PIN},
		{0x200, VENTILATOR_GPIO_O, TL_PIN},
		{0x201, VENTILATOR_GPIO_O, 0x00}
	};
	ventilator_stop();
	ventilator_push_many(ev_start, len(ev_start), 0);
	ventilator_out_addr_write(VENTILATOR_GPIO_O);
	ventilator_out_data_write(TL_PIN);

	ventilator_start();
	switch (mode) {
		case 0:
			ventilator_pop(&eva, 0);
			t = eva.time;
			ventilator_push1(t + buf, VENTILATOR_GPIO_O, TL_PIN, 0);
			break;
		case 1:
			while (!(ventilator_ev_status_read() & VENTILATOR_EV_IN_READABLE));
			t = ventilator_in_time_read();
			ventilator_out_time_write(t + buf);
			ventilator_out_next_write(0);
			ventilator_in_next_write(0);
			break;
#ifdef VENTILATOR_WB_BASE
		case 2:
			while (!(VENTILATOR_STATUS & VENTILATOR_EV_IN_READABLE));
			t = VENTILATOR_IN_TIME;
			VENTILATOR_OUT_TIME = t + buf;
			VENTILATOR_OUT_WE = 0;
			ventilator_in_next_write(0);
			break;
		case 3:
			asm volatile(
				"\n0:\n\t"
				"lw   %[temp], (%[base]+0x04)\n\t" /* load status */
				"lw   %[t], (%[base]+0x08)\n\t" /* in time */
				"andi %[temp], %[temp], 1\n\t" /* and readable */
				"be   %[temp], r0, 0b\n\t" /* while not */
				"add  %[temp], %[t], %[buf]\n\t" /* add buf */
				"sw   (%[base]+0x18), %[temp]\n\t" /* out time */
				"sw   (%[base]+0x24), r0\n\t" /* out next */
			: [t] "=&r" (t), [temp] "=&r" (temp)
			: [base] "r" (VENTILATOR_WB_BASE), [buf] "r" (buf)
			);
			ventilator_in_next_write(0);
			break;
#endif
	}

	do {
		ventilator_ctrl_update_write(0);
	} while (ventilator_ctrl_cycle_read() < t + buf);
	if (!ventilator_pop(&eva, 1))
		return -1;
	return eva.time - t;
}

static void tl(char* mode)
{
#define BUF_START 512
#define N_ITER 1000
	char *c;
	int buf = BUF_START;
	int dbuf = buf;
	int dt, i, lat, ddt, mode2;

	if (*mode == 0) {
		mode2 = 0;
	} else {
		mode2 = strtoul(mode, &c, 0);
		if (*c != 0) {
			printf("invalid flag\n");
			return;
		}
	}

	while (dbuf > 0) {
		dt = 0;
		irq_setie(0);
		for (i=0; i<10; i++) /* warm up, caching */
			tl1(buf, mode2);
		for (i=0; i<N_ITER; i++) {
			ddt = tl1(buf, mode2);
			if (ddt < 0)
				break;
			dt += ddt;
		}
		irq_setie(1);
		lat = dt - i*buf;
		printf("buf %d success %d/%d latency %d/%d\n",
				buf, i, N_ITER, lat, N_ITER);
		dbuf /= 2;
		if (i == N_ITER) {
			buf -= dbuf;
		} else {
			buf += dbuf;
		}
	}
	if (i != N_ITER)
		buf += 1;
	printf("safe cycle buffer: %d\n", buf);
	ttl_init();
}

static void ventilator_wb_write(uint32_t time, uint32_t addr, uint32_t data)
{
	const ventilator_event_t ev = {
		time,
		VENTILATOR_WISHBONE_W | VENTILATOR_WISHBONE_SEL | (addr & VENTILATOR_WISHBONE_ADDR),
		data};
	ventilator_push(&ev, 0);
}

static uint32_t ventilator_wb_read(uint32_t time, uint32_t addr)
{
	uint32_t wb_addr = VENTILATOR_WISHBONE_R | VENTILATOR_WISHBONE_SEL |
		(addr & VENTILATOR_WISHBONE_ADDR);
	ventilator_event_t ev;
	ventilator_push1(time, wb_addr, 0, 0);
	ventilator_pop(&ev, 0);
	if (wb_addr == ev.addr)
		return ev.data;
	printf("stray event 0x%08x 0x%08x 0x%08x\n", ev.time, ev.addr, ev.data);
	ti();
	return 0;
}

#define DDS_FUD			64
#define DDS_GPIO		65

static void dds_write(uint32_t addr, uint32_t data)
{
	ventilator_wb_write(0, VENTILATOR_WISHBONE_DDS + addr, data);
}

static uint32_t dds_read(uint32_t addr)
{
	return ventilator_wb_read(0, VENTILATOR_WISHBONE_DDS + addr);
}

static void ddssel(char *n)
{
	char *c;
	unsigned int n2;

	if(*n == 0) {
		puts("ddssel <n>");
		return;
	}

	n2 = strtoul(n, &c, 0);
	if(*c != 0) {
		puts("incorrect number");
		return;
	}

	dds_write(DDS_GPIO, n2);
}

static void ddsw(char *addr, char *value)
{
	char *c;
	unsigned int addr2, value2;

	if((*addr == 0) || (*value == 0)) {
		puts("ddsr <addr> <value>");
		return;
	}

	addr2 = strtoul(addr, &c, 0);
	if(*c != 0) {
		puts("incorrect address");
		return;
	}
	value2 = strtoul(value, &c, 0);
	if(*c != 0) {
		puts("incorrect value");
		return;
	}

	dds_write(addr2, value2);
}

static void ddsr(char *addr)
{
	char *c;
	unsigned int addr2;

	if(*addr == 0) {
		puts("ddsr <addr>");
		return;
	}

	addr2 = strtoul(addr, &c, 0);
	if(*c != 0) {
		puts("incorrect address");
		return;
	}

	printf("0x%02x\n", dds_read(addr2));
}

static void ddsfud(void)
{
	dds_write(DDS_FUD, 0);
}

static void ddsftw(char *n, char *ftw)
{
	char *c;
	unsigned int n2, ftw2;

	if((*n == 0) || (*ftw == 0)) {
		puts("ddsftw <n> <ftw>");
		return;
	}

	n2 = strtoul(n, &c, 0);
	if(*c != 0) {
		puts("incorrect number");
		return;
	}
	ftw2 = strtoul(ftw, &c, 0);
	if(*c != 0) {
		puts("incorrect value");
		return;
	}

	dds_write(DDS_GPIO, n2);
	dds_write(0x0a, ftw2 & 0xff);
	dds_write(0x0b, (ftw2 >> 8) & 0xff);
	dds_write(0x0c, (ftw2 >> 16) & 0xff);
	dds_write(0x0d, (ftw2 >> 24) & 0xff);
	dds_write(DDS_FUD, 0);
}

static void ddsreset(void)
{
	uint32_t t = dds_read(DDS_GPIO);
	dds_write(DDS_GPIO, t | (1 << 7));
	dds_write(DDS_GPIO, t);
}

static void ddsinit(void)
{
	ddsreset();
	dds_write(0x00, 0x78);
	dds_write(0x01, 0x00);
	dds_write(0x02, 0x00);
	dds_write(0x03, 0x00);
	ddsfud();
}

static void ddstest_one(unsigned int i)
{
	unsigned int v[12] = {
		0xaaaaaaaa, 0x55555555, 0xa5a5a5a5, 0x5a5a5a5a,
		0x00000000, 0xffffffff, 0x12345678, 0x87654321,
		0x0000ffff, 0xffff0000, 0x00ff00ff, 0xff00ff00,
		};
	unsigned int f, g, j;

	dds_write(DDS_GPIO, i);
	ddsinit();

	for (j=0; j<12; j++) {
		f = v[j];
		dds_write(0x0a, f & 0xff);
		dds_write(0x0b, (f >> 8) & 0xff);
		dds_write(0x0c, (f >> 16) & 0xff);
		dds_write(0x0d, (f >> 24) & 0xff);
		ddsfud();
		g = dds_read(0x0a);
		g |= dds_read(0x0b) << 8;
		g |= dds_read(0x0c) << 16;
		g |= dds_read(0x0d) << 24;
		if(g != f) {
			printf("readback fail on DDS %d, 0x%08x != 0x%08x\n", i, g, f);
		}
	}
}

static void ddstest(char *n)
{
	int i, j;
	char *c;
	unsigned int n2;

	if (*n == 0) {
		puts("ddstest <cycles>");
		return;
	}
	n2 = strtoul(n, &c, 0);

	for(i=0; i<n2; i++) {
		for(j=0; j<8; j++) {
			ddstest_one(j);
		}
	}
}

static void cycle_measure(void)
{
	uint32_t cc0, cc1, a=1, b=1, c=1;
	irq_setie(0);
	/*
	 * flush_cpu_dcache();
	 * flush_cpu_icache();
	 */
	asm volatile (
			"rcsr	%[cc0], CC\n\t"
			"addi	%[a], %[a], 1\n\t"
			"addi	%[a], %[a], 1\n\t"
			"addi	%[a], %[a], 1\n\t"
			"addi	%[a], %[a], 1\n\t"
			"addi	%[a], %[a], 1\n\t"
			"rcsr  	%[cc1], CC\n\t"
	: [a] "=&r" (a), [b] "=&r" (b), [c] "=&r" (c),
	  [cc0] "=r" (cc0), [cc1] "=r" (cc1)
	);
	irq_setie(1);
	printf("0x%08x, %i\n", cc0, cc1-cc0-1);
}

static void photon_phase(char *f)
{
#if 1
	#define PP_GATE	0x08
	#define PP_RF	0x02
	#define PP_PMT	0x10
#else
	#define PP_GATE	0x01
	#define PP_RF	0x02
	#define PP_PMT	0x04
#endif
#define PP_T_SCALE 10
#define PP_N_GATE 10
	char *c;
	unsigned int n[20], m;
	fixedpt x, y, r, p;
	unsigned int t_rf=0, t_pmt, i, f2;
	ventilator_event_t ev;
	const ventilator_event_t ev_tag[9] = {
		{0, VENTILATOR_CTRL_CLEAR_FORCE, 1},
		{0, VENTILATOR_CTRL_START_IN, 1},
		{0, VENTILATOR_GPIO_SENSE_FALL, PP_GATE},
		{0, VENTILATOR_GPIO_SENSE_RISE, PP_GATE},
		{0, VENTILATOR_CTRL_STOP_ONCE, 0},
		{0, VENTILATOR_GPIO_SENSE_RISE, PP_RF},
		{0, VENTILATOR_CTRL_STOP_ONCE, 0},
		{0, VENTILATOR_GPIO_SENSE_RISE, PP_PMT},
		{0, VENTILATOR_CTRL_CLEAR_FORCE, 0},
	};

	if(*f == 0) {
		puts("pp <rf>");
		return;
	}
	f2 = strtoul(f, &c, 0);
	if(*c != 0) {
		puts("incorrect number");
		return;
	}
	f2 = identifier_frequency_read()*8/(f2>>PP_T_SCALE);

	ventilator_stop();
	ventilator_push_many(ev_tag, len(ev_tag), 0);
	ventilator_start();
	for (i=0; i<len(n); i++)
		n[i] = 0;
	i = 0;

	while (!readchar_nonblock()) {
		if (!ventilator_pop(&ev, 1))
			continue;
		if (ev.data & PP_GATE) {
			if ((ev.addr & ~0xf0) == VENTILATOR_GPIO_IN_FALL) {
				puts("\\");
				ventilator_stop();
				ventilator_push_many(ev_tag, len(ev_tag), 0);
				ventilator_start();
				i++;
				if (i == PP_N_GATE) {
					x = fixedpt_rconst(0.);
					y = fixedpt_rconst(0.);
					m = 0;
					for (i=0; i<len(n); i++) {
						printf("%03d ", n[i]);
						p = fixedpt_div(fixedpt_mul(FIXEDPT_TWO_PI,
									fixedpt_fromint(i<<PP_T_SCALE)), fixedpt_fromint(f2));
						x += fixedpt_mul(fixedpt_fromint(n[i]), fixedpt_cos(p));
						y += fixedpt_mul(fixedpt_fromint(n[i]), fixedpt_sin(p));
						m += n[i];
						n[i] = 0;
					}
					i = 0;
					puts("");
					printf("m=%d  x,y=%s,", m, fixedpt_cstr(x, -1));
					printf("%s", fixedpt_cstr(y, -1));
					if (m > 0)
						r = fixedpt_div(fixedpt_sqrt(fixedpt_mul(x, x) + fixedpt_mul(y, y)),
								fixedpt_fromint(m));
					else
						r = 0;
					p = fixedpt_arctan2(y, x);
					printf("  r,p=%s,", fixedpt_cstr(r, -1));
					printf("%s\n", fixedpt_cstr(p, -1));
				}
			} else {
				putsnonl("/");
			}
		}
		if (ev.data & PP_RF) {
			putsnonl("r");
			t_rf = (ev.time << 3) + ((ev.addr & 0x70) >> 4);
			printf("%08d\n", t_rf);
		}
		if (ev.data & PP_PMT) {
			putsnonl("p");
			t_pmt = ((ev.time << 3) + ((ev.addr & 0x70) >> 4));
			printf("%08d ", t_pmt);
			t_pmt = (((t_pmt - t_rf)<<PP_T_SCALE) % f2)>>PP_T_SCALE;
			printf("%08d\n", t_pmt);
			n[min(t_pmt, len(n) - 1)]++;
		}
	}
	ttl_init();
}

static void help(void)
{
	puts("Ventilator");
	puts("Available commands:");
	puts("help           - this message");
	puts("revision       - display revision");
	puts("inputs         - read inputs");
	puts("inr            - read inputs until button");
	puts("ttlout <n>     - output ttl");
	puts("ttlin          - read ttll");
	puts("outw           - write pulses on 1 until button");
	puts("freq           - rising edges per 100ms");
	puts("ddssel <n>     - select a dds");
	puts("ddsinit        - reset, cfr, fud dds");
	puts("ddsreset       - reset dds");
	puts("ddsw <a> <d>   - write to dds register");
	puts("ddsr <a>       - read dds register");
	puts("ddsfud         - pulse FUD");
	puts("ddsftw <n> <d> - write FTW");
	puts("ddstest <n>    - perform test sequence on dds");
	puts("leds <n>       - set leds");
	puts("ts             - ventilator start");
	puts("th             - ventilator stop");
	puts("ti             - ventilator read in events");
	puts("to <t> <a> <d> - ventilator push out event");
	puts("tp             - ventilator status");
	puts("tl <mode>      - ventilator turn around measurement");
	puts("pp <rf>        - photon phase for rf freq");
	puts("vx             - ventilator engine");
}

static void readstr(char *s, int size)
{
	char c[2];
	int ptr;

	c[1] = 0;
	ptr = 0;
	while(1) {
		c[0] = readchar();
		switch(c[0]) {
			case 0x7f:
			case 0x08:
				if(ptr > 0) {
					ptr--;
					putsnonl("\x08 \x08");
				}
				break;
			case 0x07:
				break;
			case '\r':
			case '\n':
				s[ptr] = 0x00;
				putsnonl("\n");
				return;
			default:
				putsnonl(c);
				s[ptr] = c[0];
				ptr++;
				break;
		}
	}
}

static char *get_token(char **str)
{
	char *c, *d;

	c = (char *)strchr(*str, ' ');
	if(c == NULL) {
		d = *str;
		*str = *str+strlen(*str);
		return d;
	}
	*c = 0;
	d = *str;
	*str = c+1;
	return d;
}

static void do_command(char *c)
{
	char *token;

	token = get_token(&c);

	if(strcmp(token, "help") == 0) help();
	else if(strcmp(token, "revision") == 0) printf("%08x\n", MSC_GIT_ID);
	else if(strcmp(token, "leds") == 0) leds(get_token(&c));

	else if(strcmp(token, "cm") == 0) cycle_measure();

	else if(strcmp(token, "inputs") == 0) inputs();
	else if(strcmp(token, "inr") == 0) inputs_read();
	else if(strcmp(token, "outw") == 0) pulses();
	else if(strcmp(token, "ttlout") == 0) ttlout(get_token(&c));
	else if(strcmp(token, "ttlin") == 0) ttlin();
	else if(strcmp(token, "freq") == 0) freq();

	else if(strcmp(token, "ddssel") == 0) ddssel(get_token(&c));
	else if(strcmp(token, "ddsw") == 0) ddsw(get_token(&c), get_token(&c));
	else if(strcmp(token, "ddsr") == 0) ddsr(get_token(&c));
	else if(strcmp(token, "ddsreset") == 0) ddsreset();
	else if(strcmp(token, "ddsinit") == 0) ddsinit();
	else if(strcmp(token, "ddsfud") == 0) ddsfud();
	else if(strcmp(token, "ddsftw") == 0) ddsftw(get_token(&c), get_token(&c));
	else if(strcmp(token, "ddstest") == 0) ddstest(get_token(&c));

	else if(strcmp(token, "tp") == 0) tp();
	else if(strcmp(token, "to") == 0) to(get_token(&c), get_token(&c), get_token(&c));
	else if(strcmp(token, "ti") == 0) ti();
	else if(strcmp(token, "ts") == 0) ventilator_start();
	else if(strcmp(token, "th") == 0) ventilator_stop();
	else if(strcmp(token, "tl") == 0) tl(get_token(&c));
	else if(strcmp(token, "pp") == 0) photon_phase(get_token(&c));

	else if(strcmp(token, "vx") == 0) ventilator_loop();

	else if(strcmp(token, "") != 0)
		puts("Command not found");
}

int main(void)
{
	char buffer[64];

	irq_setmask(0);
	irq_setie(1);
	uart_init();

	puts("Ventilator built "__DATE__" "__TIME__"\n");

	puts("Starting engine\n");
	ventilator_loop();

	ttl_init();
		
	while(1) {
		putsnonl("\e[1mventilator>\e[0m ");
		readstr(buffer, 64);
		do_command(buffer);
	}
	
	return 0;
}
