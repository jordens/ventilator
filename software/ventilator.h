/* Robert Jordens <jordens@gmail.com>, 2014 */

#ifndef __HW_VENTILATOR_H
#define __HW_VENTILATOR_H

#include <stdint.h>
#include <hw/common.h>
#include <base/irq.h>

#define len(a) (sizeof((a))/sizeof(*(a)))
#define min(a, b) ((a)<(b)?(a):(b))
#define max(a, b) ((a)>(b)?(a):(b))
#ifdef likely
# undef likely
#endif
#ifdef unlikely
# undef unlikely
#endif
#define likely(x)  __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define VENTILATOR_WB_BASE		0xb0000000

#ifdef VENTILATOR_WB_BASE
	#define VENTILATOR_REG(x)		MMPTR(VENTILATOR_WB_BASE | (4*(x)))
	#define VENTILATOR_CYCLE		VENTILATOR_REG(0x00)
	#define VENTILATOR_STATUS		VENTILATOR_REG(0x01)
	#define VENTILATOR_IN_TIME		VENTILATOR_REG(0x02)
	#define VENTILATOR_IN_ADDR		VENTILATOR_REG(0x03)
	#define VENTILATOR_IN_DATA		VENTILATOR_REG(0x04)
	#define VENTILATOR_IN_RE		VENTILATOR_REG(0x05)
	#define VENTILATOR_OUT_TIME		VENTILATOR_REG(0x06)
	#define VENTILATOR_OUT_ADDR		VENTILATOR_REG(0x07)
	#define VENTILATOR_OUT_DATA		VENTILATOR_REG(0x08)
	#define VENTILATOR_OUT_WE		VENTILATOR_REG(0x09)
#endif

#define VENTILATOR_EV_IN_READABLE	0x01
#define VENTILATOR_EV_OUT_OVERFLOW	0x02
#define VENTILATOR_EV_IN_OVERFLOW	0x04
#define VENTILATOR_EV_OUT_READABLE	0x08
#define VENTILATOR_EV_STARTED		0x10
#define VENTILATOR_EV_STOPPED		0x20

#define VENTILATOR_CTRL				0x00000000
#define VENTILATOR_GPIO				0x00000100
#define VENTILATOR_WISHBONE			0x20000000

#define VENTILATOR_CTRL_LOOPBACK		(VENTILATOR_CTRL + 0x00)
#define VENTILATOR_CTRL_START_IN		(VENTILATOR_CTRL + 0x01)
#define VENTILATOR_CTRL_START_OUT		(VENTILATOR_CTRL + 0x02)
#define VENTILATOR_CTRL_PROHIBIT_UNDERFLOW	(VENTILATOR_CTRL + 0x03)
#define VENTILATOR_CTRL_STOP_ONCE		(VENTILATOR_CTRL + 0x04)
#define VENTILATOR_CTRL_CLEAR_FORCE		(VENTILATOR_CTRL + 0x05)
#define VENTILATOR_CTRL_NOP				(VENTILATOR_CTRL + 0xff)

#define VENTILATOR_GPIO_I			(VENTILATOR_GPIO + 0x0)
#define VENTILATOR_GPIO_O			(VENTILATOR_GPIO + 0x1)
#define VENTILATOR_GPIO_OE			(VENTILATOR_GPIO + 0x2)
#define VENTILATOR_GPIO_SENSE_RISE	(VENTILATOR_GPIO + 0x3)
#define VENTILATOR_GPIO_SENSE_FALL	(VENTILATOR_GPIO + 0x4)
#define VENTILATOR_GPIO_INV			(VENTILATOR_GPIO + 0x5)
#define VENTILATOR_GPIO_IN_RISE		(VENTILATOR_GPIO + 0x6)
#define VENTILATOR_GPIO_IN_FALL		(VENTILATOR_GPIO + 0x7)

#define VENTILATOR_WISHBONE_R		(VENTILATOR_WISHBONE | 0x00000000)
#define VENTILATOR_WISHBONE_W		(VENTILATOR_WISHBONE | 0x10000000)
#define VENTILATOR_WISHBONE_SEL		0x0f000000
#define VENTILATOR_WISHBONE_ADDR	0x00ffffff
#define VENTILATOR_WISHBONE_DDS		0x00000000

#define VENTILATOR_MAGIC	0xa5

#define VENTILATOR_MSG_NONE		0x00
#define VENTILATOR_MSG_ERR		0xff
#define VENTILATOR_MSG_LOAD		0x10
#define VENTILATOR_MSG_UNLOAD	0x11
#define VENTILATOR_MSG_EXIT		0x12
#define VENTILATOR_MSG_STATUS	0x13
#define VENTILATOR_MSG_START	0x14
#define VENTILATOR_MSG_STOP		0x15
#define VENTILATOR_MSG_PUSH		0x18
#define VENTILATOR_MSG_POP		0x19

/*
 * Bring hardware to a state that can be
 * ARMed. Needs to be idempotent (work from any state).
 * Usually called after LOAD or CLEANUP.
 */
#define VENTILATOR_MSG_SETUP	0x20

/*
 * Changes parameters. Called after SETUP or DONE, before ARM.
 * Hardware is in an ARMable state before and after this.
 */
#define VENTILATOR_MSG_UPDATE	0x21

/*
 * After SETUP or UPDATE, bring the device to a TRIGGERable
 * state. Do as much work as possible here. Devices are
 * synchronized afterwards.
 */
#define VENTILATOR_MSG_ARM		0x22

/*
 * Start the sequence. Called after ARM. Be as fast as
 * possible here or do nothing if externally triggered.
 */
#define VENTILATOR_MSG_TRIGGER	0x23

/*
 * Perform the necessary steps to halt further execution.
 * May be called at any time. Does not need to be idempotent.
 * SETUP or CLEANUP will be called after this. Be fast here.
 */
#define VENTILATOR_MSG_ABORT	0x24

#define VENTILATOR_MSG_DONE		0x25

/*
 * Bring the hardware to a safe state. Called after DONE
 * or after ABORT, before SETUP.
 */
#define VENTILATOR_MSG_CLEANUP	0x26

#define VENTILATOR_MSG_REQ		0x01
#define VENTILATOR_MSG_ACK		0x02
#define VENTILATOR_MSG_NACK		0x03

typedef struct ventilator_event_t {
	uint32_t time;
	uint32_t addr;
	uint32_t data;
} ventilator_event_t;

typedef struct __attribute__((packed, aligned(4))) ventilator_msg_t {
	uint8_t magic;
	uint8_t type;
	uint8_t status;
	uint8_t len;
	union {
		uint8_t data8[255];
		uint16_t data16[255/sizeof(uint16_t)];
		uint32_t data32[255/sizeof(uint32_t)];
		ventilator_event_t ev[255/sizeof(ventilator_event_t)];
	};
} ventilator_msg_t;

typedef struct ventilator_t {
	void (* kernel)(ventilator_msg_t *msg);
	uint32_t (* isr)(uint32_t pending);
	void (* const start)(void);
	void (* const stop)(void);
	void (* const set_callbacks)(void (*kernel)(ventilator_msg_t *),
			uint32_t (*isr)(uint32_t), uint32_t irq);
	int (* const push1)(uint32_t time, uint32_t addr, uint32_t data, int noblock);
	int (* const push)(const ventilator_event_t *ev, int noblock);
	int (* const push_many)(const ventilator_event_t *ev, int n, int noblock);
	int (* const pop)(ventilator_event_t *ev, int noblock);
	int (* const pop_count)(uint32_t addr, uint32_t mask, uint32_t *counter, int noblock);
	int (* const pop_many)(ventilator_event_t *ev, int n, int noblock);
	void (* const send)(const ventilator_msg_t *msg);
	void (* const send_many)(ventilator_msg_t *msg,
			const ventilator_event_t *ev, unsigned int n);
	void (* const send_array)(ventilator_msg_t *msg,
			uint32_t time, uint32_t addr,
			const uint32_t *data, unsigned int n);
} ventilator_t;

register ventilator_t *ventilator asm ("r25");

void ventilator_init(void);
void ventilator_loop(void);
void ventilator_isr(void);
void ventilator_start(void);
void ventilator_stop(void);

/*
 * Called in IRQ context with pending != 0.
 * Returns acknowledged IRQs.
 * If a FIFO interface is used here, it can not be used in normal
 * context while the IRQ is allowed.
 */
void ventilator_set_callbacks(void (*kernel)(ventilator_msg_t *),
		uint32_t (*isr)(uint32_t), uint32_t irq);
int ventilator_push1(uint32_t time, uint32_t addr, uint32_t data, int noblock);
int ventilator_push(const ventilator_event_t *ev, int noblock);
int ventilator_pop(ventilator_event_t *ev, int noblock);
int ventilator_push_many(const ventilator_event_t *ev, int n, int noblock);
int ventilator_pop_many(ventilator_event_t *ev, int n, int noblock);
int ventilator_pop_count(uint32_t addr, uint32_t mask, uint32_t *counter, int noblock);
void ventilator_send(const ventilator_msg_t *msg);
void ventilator_send_many(ventilator_msg_t *msg,
		const ventilator_event_t *ev, unsigned int n);
void ventilator_send_array(ventilator_msg_t *msg,
		uint32_t time, uint32_t addr,
		const uint32_t *data, unsigned int n);

/*
 * Called on KERNEL message reception and very frequently in each
 * main loop iteration in normal context.
 */
void ventilator_kernel(ventilator_msg_t *msg);


#define SYS_CLK 80e6
#define DDS_CLK 125e6
#define PI 3.141592653589793115998

#define us_to_cycles(time) ((uint32_t) ((time)*(1e-6*SYS_CLK) + .5))

#define cycles_to_us(cycles) (((float) (cycles))*(1/(1e-6*SYS_CLK)))

#define us_to_hires_cycles(time) ((uint32_t) \
		(8*((time)*(1e-6*SYS_CLK) - us_to_cycles(time)) + .5))

#define gpio_start() \
	uint32_t _t = 0, _c = 0, _r = 0; \
	int (* const _v_push1)(uint32_t, uint32_t, uint32_t, int) = \
		ventilator->push1;

#define now_cycles() _t

#define now_us() (cycles_to_us(_t))

#define _push1(addr, data) _v_push1(_t, addr, data, 0); _t++;

#define loopback(data) _push1(VENTILATOR_CTRL_LOOPBACK, data)

#define at_us(time) _t = us_to_cycles(time);

#define at_cycles(time) _t = time;

#define wait_us(time) _t += us_to_cycles(time);

#define wait_cycles(time) _t += time;

#define gpio_set(channels, hires) \
	_c = (channels); _push1(VENTILATOR_GPIO_O | ((hires & 7) << 4), _c)

#define gpio_on(channels, hires) gpio_set(_c | (channels), hires)
#define gpio_off(channels, hires) gpio_set(_c & ~(channels), hires)

#define gpio_pulse_us(time, channels) \
	gpio_on(channels, 0) _t--; wait_us(time) gpio_off(channels, 0)

#define gpio_open(channels) \
	_r |= (channels); _push1(VENTILATOR_GPIO_SENSE_RISE, _r)

#define gpio_close(channels) \
	_r &= ~(channels); _push1(VENTILATOR_GPIO_SENSE_RISE, _r)

#define gpio_detect_us(time, channels) \
	gpio_open(channels) _t--; wait_us(time) gpio_close(channels)

#define DDS_FUD			64
#define DDS_GPIO		65

#define dds_write1(addr, data) \
	_push1(VENTILATOR_WISHBONE_W | VENTILATOR_WISHBONE_SEL | \
			(VENTILATOR_WISHBONE_ADDR & (VENTILATOR_WISHBONE_DDS \
			+ (addr))), (data)) _t++;

#define dds_write2(addr, data) \
	dds_write1(addr, (data) >> 8) dds_write1((addr) + 1, data)

#define dds_write4(addr, data) \
	dds_write2(addr, (data) >> 16) dds_write2((addr) + 2, data)

#define dds_tune(sel, ftw, ptw) \
	dds_write1(DDS_GPIO, sel) \
	dds_write4(0x0a, (uint32_t) ((ftw)*((1<<23)/(DDS_CLK/(1<<9)) + .5))) \
	dds_write2(0x0e, (uint32_t) ((ptw)*((1<<14)/(2*PI)) + .5)) \
	dds_write1(DDS_FUD, 0)

#endif /* __HW_VENTILATOR_H */
