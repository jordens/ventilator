/* Robert Jordens <jordens@gmail.com>, 2014 */

#include <hw/flags.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <console.h>
#include <string.h>
#include <system.h>
#include <irq.h>
#include <uart.h>

#include <generated/csr.h>
#include <generated/mem.h>

#include <crc.h>
#include "ventilator.h"

static void ventilator_load(ventilator_msg_t *msg)
{
	if (msg->len == sizeof(uint32_t)) {
		uint32_t adr;
		flush_cpu_icache();
		adr = ((uint32_t (*)(void)) msg->data32[0])();
		ventilator_set_callbacks((void (*)(ventilator_msg_t *)) adr,
				NULL, 0);
	} else {
		memcpy((void *) msg->data32[0], (void *) &msg->data32[1],
				msg->len - sizeof(uint32_t));
	}
	msg->len = 0;
}

#define VENTILATOR_MSG_MAX_FAIL 5

static int ventilator_recv(ventilator_msg_t **tmsg)
{
	static ventilator_msg_t msg;
	static int len = 0;
	static int fail = 0;
	if (!tmsg)
		return 0;
	*tmsg = NULL;
	while (uart_read_nonblock()) {
		((char *) &msg)[len++] = uart_read();
		if ((len == 1) & (msg.magic != VENTILATOR_MAGIC)) {
			fail++;
			len = 0;
			if (fail == VENTILATOR_MSG_MAX_FAIL) {
				fail = 0;
				return 0;
			}
		} else if (len == 4 + msg.len) {
			len = 0;
			fail = 0;
			*tmsg = &msg;
			break;
		}
	}
	return 1;
}

static void ventilator_get_status(ventilator_msg_t *msg)
{
	ventilator_ctrl_update_write(0);
	msg->ev[0].time = ventilator_ctrl_cycle_read();
	msg->ev[0].addr = 0;
	msg->ev[0].data = ventilator_ev_status_read();
	msg->len = sizeof(ventilator_event_t);
}

static int ventilator_handle(ventilator_msg_t *msg)
{
	int req = (msg->status == VENTILATOR_MSG_REQ);
	int ev_len;
	switch (msg->type) {
		case VENTILATOR_MSG_LOAD:
			ventilator_load(msg);
			break;
		case VENTILATOR_MSG_UNLOAD:
			msg->len = 0;
			ventilator_set_callbacks(NULL, NULL, 0);
			break;
		case VENTILATOR_MSG_EXIT:
			msg->len = 0;
			return 0;
			break;
		case VENTILATOR_MSG_STATUS:
			ventilator_get_status(msg);
			break;
		case VENTILATOR_MSG_START:
			msg->len = 0;
			ventilator_start();
			break;
		case VENTILATOR_MSG_STOP:
			msg->len = 0;
			ventilator_stop();
			break;
		case VENTILATOR_MSG_PUSH:
			ev_len = msg->len/sizeof(ventilator_event_t);
			msg->len = 0;
			if (ventilator_push_many(msg->ev, ev_len, 1) < ev_len)
				msg->status = VENTILATOR_MSG_NACK;
			break;
		case VENTILATOR_MSG_POP:
			msg->len = sizeof(ventilator_event_t) * ventilator_pop_many(
					msg->ev, len(msg->ev), 1);
			break;
		default:
			if (ventilator->kernel) {
				ventilator->kernel(msg);
			} else {
				msg->status = VENTILATOR_MSG_NACK;
				msg->len = 0;
			}
			break;
	}
	if (req) {
		if (msg->status == VENTILATOR_MSG_REQ)
			msg->status = VENTILATOR_MSG_ACK;
		ventilator_send(msg);
	}
	return 1;
}

void ventilator_send(const ventilator_msg_t *msg)
{
	int i;
	if (!msg)
		return;
	uart_write(VENTILATOR_MAGIC);
	uart_write(msg->type);
	uart_write(msg->status);
	uart_write(msg->len);
	for (i=0; i<msg->len; i++)
		uart_write(msg->data8[i]);
}

void ventilator_send_many(ventilator_msg_t *msg,
		const ventilator_event_t *ev, unsigned int n)
{
	unsigned int i = 0, k;
	msg->type = VENTILATOR_MSG_UPDATE;
	msg->status = VENTILATOR_MSG_NONE;
	while (n) {
		k = min(len(msg->ev), n);
		msg->len = k*sizeof(ventilator_event_t);
		memcpy((void *) msg->ev, (void *) &ev[i], msg->len);
		ventilator->send(msg);
		i += k;
		n -= k;
	}
}

void ventilator_send_array(ventilator_msg_t *msg,
		uint32_t time, uint32_t addr,
		const uint32_t *data, unsigned int n)
{
	unsigned int i = 0, k;
	msg->type = VENTILATOR_MSG_UPDATE;
	msg->status = VENTILATOR_MSG_NONE;
	while (n) {
		msg->data32[0] = time;
		msg->data32[1] = addr + i;
		k = min(len(msg->data32) - 2, n);
		memcpy((void *) &msg->data32[2], (void *) &data[i],
				k*sizeof(uint32_t));
		msg->len = (k + 2)*sizeof(uint32_t);
		ventilator_send(msg);
		i += k;
		n -= k;
	}
}

void ventilator_set_callbacks(void (*kernel)(ventilator_msg_t*),
		uint32_t (*isr)(uint32_t), uint32_t irq)
{
	uint32_t ack;
	ventilator->kernel = kernel;
	ack = irq & ~ventilator_ev_enable_read();
	irq_setie(0);
	ventilator_ev_pending_write(ack);
	ventilator_ev_enable_write(irq);
	ventilator->isr = isr;
	irq_setie(1);
}

static ventilator_t _ventilator = {
		.start = &ventilator_start,
		.stop = &ventilator_stop,
		.set_callbacks = &ventilator_set_callbacks,
		.push1 = &ventilator_push1,
		.push = &ventilator_push,
		.push_many = &ventilator_push_many,
		.pop = &ventilator_pop,
		.pop_count = &ventilator_pop_count,
		.pop_many = &ventilator_pop_many,
		.send = &ventilator_send,
		.send_many = &ventilator_send_many,
		.send_array = &ventilator_send_array
};

void ventilator_isr(void)
{
	uint32_t stat, temp;
	asm volatile ("mv %0, r25\n\t": "=r" (temp));
	ventilator = &_ventilator;
	stat = ventilator_ev_pending_read();
	if (ventilator->isr)
		stat = ventilator->isr(stat);
	ventilator_ev_pending_write(stat);
	asm volatile ("mv r25, %0\n\t":: "r" (temp));
}

void ventilator_init(void)
{
	unsigned int mask;
	uart_sync();
	uart_divisor_write(identifier_frequency_read()/115200/16);
	ventilator = &_ventilator;
	ventilator_stop();
	ventilator_set_callbacks(NULL, NULL, 0);
	mask = irq_getmask();
	mask |= 1 << VENTILATOR_INTERRUPT;
	irq_setmask(mask);
}

static void ventilator_exit(void)
{
	unsigned int mask;
	ventilator_stop();
	ventilator_set_callbacks(NULL, NULL, 0);
	uart_divisor_write(identifier_frequency_read()/115200/16);
	mask = irq_getmask();
	mask &= ~(1 << VENTILATOR_INTERRUPT);
	irq_setmask(mask);
}

void ventilator_start(void)
{
	ventilator_ctrl_prohibit_write(0);
	ventilator_ctrl_start_write(0);
}

void ventilator_stop(void)
{
	ventilator_ctrl_prohibit_write(1);
	ventilator_out_flush_write(0);
	ventilator_in_flush_write(0);
	ventilator_ctrl_clear_write(0);
}

void ventilator_loop(void)
{
	ventilator_msg_t *msg;
	ventilator_init();
	while (1) {
		if (!ventilator_recv(&msg))
			break;
		if (msg) {
			if (!ventilator_handle(msg))
				break;
		} else if (ventilator->kernel) {
			ventilator->kernel(NULL);
		}
	}
	ventilator_exit();
}

inline int ventilator_push1(uint32_t time, uint32_t addr, uint32_t data, int noblock)
{
	while (ventilator_ev_status_read() & VENTILATOR_EV_OUT_OVERFLOW)
		if (noblock)
			return 0;
#ifdef VENTILATOR_WB_BASE
	VENTILATOR_OUT_TIME = time;
	VENTILATOR_OUT_ADDR = addr;
	VENTILATOR_OUT_DATA = data;
#else
	ventilator_out_time_write(time);
	ventilator_out_addr_write(addr);
	ventilator_out_data_write(data);
#endif
	ventilator_out_next_write(0);
	return 1;
}

inline int ventilator_push(const ventilator_event_t *ev, int noblock)
{
	return ventilator_push1(ev->time, ev->addr, ev->data, noblock);
}

inline int ventilator_pop(ventilator_event_t *ev, int noblock)
{
	while (!(ventilator_ev_status_read() & VENTILATOR_EV_IN_READABLE))
		if (noblock)
			return 0;
	if (ev) {
#ifdef VENTILATOR_WB_BASE
		ev->time = VENTILATOR_IN_TIME;
		ev->addr = VENTILATOR_IN_ADDR;
		ev->data = VENTILATOR_IN_DATA;
#else
		ev->time = ventilator_in_time_read();
		ev->addr = ventilator_in_addr_read();
		ev->data = ventilator_in_data_read();
#endif
	}
	ventilator_in_next_write(0);
	return 1;
}

int ventilator_push_many(const ventilator_event_t *ev, int n, int noblock)
{
	int i;
	for (i=0; i<n; i++)
		if (!ventilator_push1(ev[i].time, ev[i].addr, ev[i].data, noblock))
			break;
	return i;
}

int ventilator_pop_many(ventilator_event_t *ev, int n, int noblock)
{
	int i = 0;
	for (i=0; i<n; i++)
		if (!ventilator_pop(&ev[i], noblock))
			break;
	return i;
}

int ventilator_pop_count(uint32_t addr, uint32_t mask, uint32_t *counter, int noblock)
{
	uint32_t a, n=0;
	while (1) {
		if (ventilator_ev_status_read() & VENTILATOR_EV_IN_READABLE) {
#ifdef VENTILATOR_WB_BASE
			a = VENTILATOR_IN_ADDR;
#else
			a = ventilator_in_addr_read();
#endif
			if ((a & ~mask) == (addr & ~mask)) {
				n++;
				ventilator_in_next_write(0);
			} else {
				if (counter)
					*counter += n;
				return 1;
			}
		} else if (noblock) {
			break;
		}
	}
	if (counter)
		*counter += n;
	return 0;
}
