/* Robert Jordens <jordens@gmail.com>, 2014 */

#include <ventilator.h>

#define AO_BD 0x10
#define AO_BDD 0x20
#define PMT0 0x01
#define DDS_BD 0
#define DDS_BDD 1
#define DETECTS 1
#define DETECT_MASK 0xff
#define HISTS 20
#define HISTOGRAM_ADDR 0x01000000
#define PARAMS 2
#define PARAM_ADDR 0x00000000
#define PARAM_N_RUNS 0
#define PARAM_N_REPEATS 1

static void push_events(void)
{
	/* TODO: this will deadlock when the output FIFO gets filled */
	int i;
	gpio_start()
	irq_setie(0);
	ventilator->push1(now_cycles(), VENTILATOR_CTRL_CLEAR_FORCE, 0, 0); /* 0, 0, 1,... */
	wait_us(35.)
	dds_tune(DDS_BD, 100e6, 0)
	dds_tune(DDS_BD, 200e6, .11)
	dds_tune(DDS_BD, 300e6, .22)
	gpio_open(PMT0 | AO_BD)
	wait_us(.1)
	for (i=0; i<5; i++) {
		gpio_pulse_us(.1, AO_BD)
		wait_us(.1)
		gpio_pulse_us(.2, AO_BD)
		wait_us(.1)
	}
	gpio_close(PMT0 | AO_BD)
	loopback(0xdead) /* detection end marker */
	at_us(100.)
	ventilator->push1(now_cycles(), VENTILATOR_CTRL_CLEAR_FORCE, 1, 0); /* n, n+1, 0,... */
	irq_setie(1);
}

static volatile uint32_t detect[DETECTS];
static volatile int done = 0;
static void count_rises(void)
{
	if (ventilator->pop_count(VENTILATOR_GPIO_IN_RISE, DETECT_MASK,
					(uint32_t *) &detect[done], 1)) {
		ventilator->pop(0, 0); /* loopback 0xdead marker */
		done++;
	}
}

static uint32_t isr(uint32_t pending)
{
	if (likely(pending & VENTILATOR_EV_IN_READABLE))
		count_rises();
	return pending;
}

static void send_histograms(uint32_t *hist, int n)
{
	ventilator_msg_t ret;
	ventilator->send_array(&ret, 0, HISTOGRAM_ADDR, hist, n);
	ret.type = VENTILATOR_MSG_DONE;
	ret.len = 0;
	ventilator->send(&ret);
}

static uint32_t param[PARAMS];
static int trigger = 0;
static uint32_t hist[DETECTS*HISTS];
static void poll(void)
{
	static uint32_t runs = 0;
	static uint32_t repeats = 0;
	int i;
	if (done == DETECTS) {
		done = 0;
		runs++;
		for (i=0; i<DETECTS; i++) {
			hist[i*HISTS + min(detect[i], HISTS - 1)]++;
			detect[i] = 0;
		}
		if (runs < param[PARAM_N_RUNS]
				|| (!param[PARAM_N_RUNS])) {
			trigger = 1;
		} else {
			runs = 0;
			repeats++;
			send_histograms(hist, DETECTS*HISTS);
			for (i=0; i<DETECTS*HISTS; i++)
				hist[i] = 0;
			if ((repeats < param[PARAM_N_REPEATS])
				|| (!param[PARAM_N_REPEATS])) {
				trigger = 1;
			} else {
				repeats = 0;
			}
		}
	}
	if (trigger) {
		trigger = 0;
		push_events();
	}
}

static void handle_msg(ventilator_msg_t *msg)
{
	unsigned int i;
	static const ventilator_event_t ev_setup[8] = {
		{0, VENTILATOR_CTRL_CLEAR_FORCE, 1},
		{0, VENTILATOR_GPIO_SENSE_RISE, 0x000000},
		{0, VENTILATOR_GPIO_SENSE_FALL, 0x000000},
		{0, VENTILATOR_GPIO_INV, 0x000000},
		/* {0, VENTILATOR_GPIO_O, 0x300000}, */
		{0, VENTILATOR_GPIO_OE, 0x3ffff0},
		{0, VENTILATOR_CTRL_PROHIBIT_UNDERFLOW, 1},
		{0, VENTILATOR_CTRL_START_OUT, 1},
		{0, VENTILATOR_CTRL_LOOPBACK, 0},
	};
	switch (msg->type) {
		case VENTILATOR_MSG_SETUP:
			for (i=0; i<HISTS*DETECTS; i++)
				hist[i] = 0;
			for (i=0; i<DETECTS; i++)
				detect[i] = 0;
			param[PARAM_N_RUNS] = 100;
			param[PARAM_N_REPEATS] = 1;
			ventilator->stop();
			ventilator->push_many(ev_setup, len(ev_setup), 0);
			ventilator->start();
			ventilator->pop(0, 0);
			ventilator->stop();
			ventilator->set_callbacks(&ventilator_kernel, &isr,
					VENTILATOR_EV_IN_READABLE);
			break;
		case VENTILATOR_MSG_UPDATE:
			for (i=0; msg->len>i*sizeof(ventilator_event_t); i++) {
				if (msg->ev[i].addr >= PARAMS) {
					msg->status = VENTILATOR_MSG_NACK;
					break;
				}
				param[msg->ev[i].addr - PARAM_ADDR] = msg->ev[i].data;
			}
			break;
		case VENTILATOR_MSG_ARM:
			trigger = 1;
			break;
		case VENTILATOR_MSG_TRIGGER:
			ventilator->start();
			break;
		case VENTILATOR_MSG_ABORT:
			ventilator->stop();
			break;
		case VENTILATOR_MSG_CLEANUP:
			ventilator->stop();
			ventilator->set_callbacks(0, 0, 0);
			break;
		default:
			msg->status = VENTILATOR_MSG_NACK;
			break;
	}
	msg->len = 0;
}

void ventilator_kernel(ventilator_msg_t *msg)
{
	if (msg)
		handle_msg(msg);
	poll();
}
