# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from migen.genlib.misc import optree
from migen.bus import wishbone
from migen.bank.description import CSR, CSRStorage, CSRStatus, AutoCSR
from migen.bank.eventmanager import (EventManager, EventSourceLevel,
		EventSourceProcess)
from migen.genlib.fifo import SyncFIFOBuffered
from migen.genlib.coding import PriorityEncoder
from migen.flow.actor import Source, Sink

from .slave import ventilator_layout, slave_layout, Slave

class CycleControl(Slave, AutoCSR):
	def __init__(self):
		super(CycleControl, self).__init__()
		time_width = ventilator_layout[0][1]

		self._start = CSR()
		self._prohibit = CSRStorage()
		self._clear = CSR()
		self._run = CSRStatus()
		self._cycle = CSRStatus(time_width)
		self._update = CSR()

		self.have_out = Signal()
		self.have_in = Signal()
		self.cycle = Signal(time_width)
		self.run = Signal()

		###

		start_in = Signal()
		start_out = Signal()
		prohibit_underflow = Signal()
		stop_once = Signal()

		clear_force = Signal()
		clear_force0 = Signal()

		stop = Signal()
		start = Signal()
		clear = Signal()

		run0 = Signal()

		self.comb += [
				self._run.status.eq(run0),
				start.eq(self._start.re |
					(start_out & self.have_out)),
				stop.eq(self._prohibit.storage |
					(prohibit_underflow & ~self.have_out)),
				clear.eq(self._clear.re | clear_force | clear_force0),
				If(stop,
					self.run.eq(0),
				).Elif(start,
					self.run.eq(1),
				).Else(
					self.run.eq(run0),
				),
				]

		self.comb += [
				self.dout.ack.eq(1),
				self.din.payload.addr.eq(self.dout.payload.addr),
				self.din.payload.data.eq(self.dout.payload.data),
				If(self.dout.stb,
					Case(self.dout.payload.addr[:8], {
						0x00: self.din.stb.eq(1),
						0x04: stop_once.eq(1),
						0x05: clear_force.eq(self.dout.payload.data),
					}),
				)]

		self.sync += [
				If(self.dout.stb,
					Case(self.dout.payload.addr[:8], {
						0x01: start_in.eq(self.dout.payload.data),
						0x02: start_out.eq(self.dout.payload.data),
						0x03: prohibit_underflow.eq(self.dout.payload.data),
						0x05: clear_force0.eq(self.dout.payload.data),
						}),
				),
				If(stop_once,
					run0.eq(0),
				).Elif(start_in & self.have_in,
					run0.eq(1),
				).Else(
					run0.eq(self.run),
				),
				If(clear,
					self.cycle.eq(0),
				).Elif(self.run,
					self.cycle.eq(self.cycle + 1),
				),
				If(self._update.re,
					self._cycle.status.eq(self.cycle),
				),
				]

class Master(Module, AutoCSR):
	"""Hard timing adapter
	* exposed via wishbone and csr
	* time, addr, data go through two fifos
	* write: on 0 <= cycle - time <= 1<<30 (within wrap plausibility):
	    write to sink using address decoders
	* read: downstreams are sources and get time-tagged
	* downstream only see addr/data stb/ack
	* device nop/loopback: nop read and write address for wraps
	* lowest slave has priority on din
	"""
	def __init__(self, slaves, depth=256, bus=None, with_wishbone=True):
		time_width, addr_width, data_width = [_[1] for _ in ventilator_layout]

		self.submodules.ctrl = CycleControl()

		self.submodules.ev = ev = EventManager()
		ev.in_readable = EventSourceLevel()
		ev.out_overflow = EventSourceProcess()
		ev.in_overflow = EventSourceLevel()
		ev.out_readable = EventSourceProcess()
		ev.stopped = EventSourceProcess()
		ev.started = EventSourceProcess()
		ev.finalize()

		self._in_time = CSRStatus(time_width)
		self._in_addr = CSRStatus(addr_width)
		self._in_data = CSRStatus(data_width)
		self._in_next = CSR()
		self._in_flush = CSR()

		self._out_time = CSRStorage(time_width, write_from_dev=with_wishbone)
		self._out_addr = CSRStorage(addr_width, write_from_dev=with_wishbone)
		self._out_data = CSRStorage(data_width, write_from_dev=with_wishbone)
		self._out_next = CSR()
		self._out_flush = CSR()

		self.busy = Signal()

		###

		if with_wishbone:
			if bus is None:
				bus = wishbone.Interface()
			self.bus = bus

		slaves = [(self.ctrl, 0x00000000, 0xffffff00)] + slaves

		self.submodules.in_fifo = in_fifo = SyncFIFOBuffered(
				ventilator_layout, depth)
		self.submodules.out_fifo = out_fifo = SyncFIFOBuffered(
				ventilator_layout, depth)
		self.submodules.enc = PriorityEncoder(len(slaves))

		wb_in_next = Signal()
		wb_out_next = Signal()
		out_request = Signal()
		in_request = Signal()

		# CSRs and Events
		self.comb += [
				ev.in_readable.trigger.eq(in_fifo.readable),
				ev.out_overflow.trigger.eq(~out_fifo.writable),
				ev.in_overflow.trigger.eq(~in_fifo.writable),
				ev.out_readable.trigger.eq(out_fifo.readable),
				ev.started.trigger.eq(~self.ctrl.run),
				ev.stopped.trigger.eq(self.ctrl.run),
				self.ctrl.have_in.eq(~self.enc.n),
				self.ctrl.have_out.eq(out_fifo.readable),

				self._in_time.status.eq(in_fifo.dout.time),
				self._in_addr.status.eq(in_fifo.dout.addr),
				self._in_data.status.eq(in_fifo.dout.data),
				in_fifo.re.eq(self._in_next.re | wb_in_next),
				in_fifo.flush.eq(self._in_flush.re),

				out_fifo.din.time.eq(self._out_time.storage),
				out_fifo.din.addr.eq(self._out_addr.storage),
				out_fifo.din.data.eq(self._out_data.storage),
				out_fifo.we.eq(self._out_next.re | wb_out_next),
				out_fifo.flush.eq(self._out_flush.re),
				]

		# din dout strobing
		self.comb += [
				# TODO: 0 <= diff <= plausibility range
				out_request.eq(out_fifo.readable & self.ctrl.run &
					(self.ctrl.cycle == out_fifo.dout.time)),
				# ignore in_fifo.writable
				in_request.eq(~self.enc.n & self.ctrl.run),
				self.busy.eq(out_request | in_request),
				]

		# to slaves
		addrs = []
		datas = []
		stbs = []
		acks = []
		for i, (slave, prefix, mask) in enumerate(slaves):
			prefix &= mask
			source = Source(slave_layout)
			sink = Sink(slave_layout)
			self.comb += [
					source.connect(slave.dout),
					sink.connect(slave.din),
					]
			sel = Signal()
			acks.append(sel & source.ack)
			addrs.append(prefix | (sink.payload.addr & (~mask & 0xffffffff)))
			datas.append(sink.payload.data)
			stbs.append(sink.stb)
			self.comb += [
					sel.eq(out_fifo.dout.addr & mask == prefix),
					source.payload.addr.eq(out_fifo.dout.addr),
					source.payload.data.eq(out_fifo.dout.data),
					source.stb.eq(sel & out_request),
					sink.ack.eq((self.enc.o == i) & in_request),
					]
		self.comb += out_fifo.re.eq(out_request & optree("|", acks))

		# from slaves
		self.comb += [
				self.enc.i.eq(Cat(stbs)),
				in_fifo.din.time.eq(self.ctrl.cycle),
				in_fifo.din.addr.eq(Array(addrs)[self.enc.o]),
				in_fifo.din.data.eq(Array(datas)[self.enc.o]),
				in_fifo.we.eq(in_request),
				]

		# optional high throughput wishbone access
		if with_wishbone:
			self.comb += [
					self._out_time.dat_w.eq(bus.dat_w),
					self._out_addr.dat_w.eq(bus.dat_w),
					self._out_data.dat_w.eq(bus.dat_w),
					If(bus.cyc & bus.stb,
						If(bus.we,
							Case(bus.adr[:4], {
								0x5: wb_in_next.eq(1),
								0x6: self._out_time.we.eq(1),
								0x7: self._out_addr.we.eq(1),
								0x8: self._out_data.we.eq(1),
								0x9: wb_out_next.eq(1),
							}),
						),
						Case(bus.adr[:4], {
							0x0: bus.dat_r.eq(self.ctrl.cycle),
							0x1: bus.dat_r.eq(self.ev.status.w),
							0x2: bus.dat_r.eq(in_fifo.dout.time),
							0x3: bus.dat_r.eq(in_fifo.dout.addr),
							0x4: bus.dat_r.eq(in_fifo.dout.data),
						}),
					)]
			self.sync += bus.ack.eq(bus.cyc & bus.stb & ~bus.ack)
