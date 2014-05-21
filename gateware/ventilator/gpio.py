# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from . import Slave

class Gpio(Slave):
	def __init__(self, pads):
		super(Gpio, self).__init__()

		###

		n = flen(pads)
		i0 = Signal(n)
		i = Signal(n)
		o = Signal(n)
		oe = Signal(n)
		r = Signal(n)
		f = Signal(n)
		b = Signal(n)

		rising = Signal(n)
		falling = Signal(n)
		self.comb += [
				self.dout.ack.eq(1),
				rising.eq((~i0 & r) & i),
				falling.eq((i0 & f) & ~i),
				self.busy.eq(self.din.stb & ~self.din.ack),
				]

		self.sync += [
				If(self.din.stb & self.din.ack,
					self.din.stb.eq(0),
				),
				If(self.dout.stb,
					self.din.payload.addr.eq(0),
					Case(self.dout.payload.addr[:4], {
						0x0: [self.din.payload.data.eq(i), self.din.stb.eq(1)],
						0x1: o.eq(self.dout.payload.data),
						0x2: oe.eq(self.dout.payload.data),
						0x3: r.eq(self.dout.payload.data),
						0x4: f.eq(self.dout.payload.data),
						0x5: b.eq(self.dout.payload.data),
					}),
				),
				If(~(self.dout.stb & (self.dout.payload.addr[:4] == 0)),
					i0.eq(i),
					If(rising,
						self.din.payload.addr.eq(0x6),
						self.din.payload.data.eq(rising),
						self.din.stb.eq(1),
					).Elif(falling,
						self.din.payload.addr.eq(0x7),
						self.din.payload.data.eq(falling),
						self.din.stb.eq(1),
					)
				)]

		for j in range(n):
			dq = TSTriple()
			self.comb += [
					i[j].eq(dq.i ^ b[j]),
					dq.o.eq(o[j] ^ b[j]),
					dq.oe.eq(oe[j]),
					]
			self.specials += dq.get_tristate(pads[j])
