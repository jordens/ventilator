from migen.fhdl.std import *
from migen.genlib.fsm import FSM, NextState
from migen.bus import wishbone
from . import Slave

class Wishbone(Slave):
	"""
	ventilator slave wishbone master: time accurate (if single master)
	writes and reads making them asynchronous from ventilator.
	"""
	def __init__(self, we_bit=28, sel_bits=slice(24, 28)):
		super(Wishbone, self).__init__()
		self.bus = bus = wishbone.Interface()

		###

		start = Signal()
		read = Signal()
		self.sync += [
				If(start,
					self.bus.adr.eq(self.dout.payload.addr),
					self.bus.dat_w.eq(self.dout.payload.data),
				),
				If(read,
					self.din.payload.data.eq(self.bus.dat_r),
				)]
		self.comb += [
				self.dout.ack.eq(1),
				self.din.payload.addr.eq(self.bus.adr),
				self.bus.cyc.eq(self.bus.stb),
				self.bus.we.eq(self.bus.adr[we_bit]),
				]

		if sel_bits is not None:
			self.comb += self.bus.sel.eq(self.bus.adr[sel_bits])
		else:
			self.bus.sel.reset = 0b1111

		self.submodules.fsm = fsm = FSM()
		fsm.act("IDLE",
				If(self.dout.stb,
					start.eq(1),
					NextState("BUS"),
				))
		fsm.act("BUS",
				self.bus.stb.eq(1),
				If(self.bus.ack,
					If(self.bus.we,
						NextState("IDLE"),
					).Else(
						read.eq(1),
						NextState("QUEUE"),
					),
				))
		fsm.act("QUEUE",
				self.din.stb.eq(1),
				If(self.din.ack,
					NextState("IDLE"),
				))
