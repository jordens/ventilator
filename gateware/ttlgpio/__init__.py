from migen.fhdl.std import *
from migen.bank.description import *
from migen.genlib.cdc import MultiReg

class TTLGPIO(Module, AutoCSR):
	def __init__(self, pads):
		l = flen(pads.d_l) + flen(pads.d_h)
		self.i = CSRStatus(l)
		self.o = CSRStorage(l)
		self.oe = CSRStorage(2)

		###

		ts_l = TSTriple(flen(pads.d_l))
		ts_h = TSTriple(flen(pads.d_h))
		self.specials += ts_l.get_tristate(pads.d_l), ts_h.get_tristate(pads.d_h)
		self.comb += [
			Cat(ts_l.o, ts_h.o).eq(self.o.storage),
			Cat(ts_l.oe, ts_h.oe).eq(self.oe.storage),
			Cat(pads.tx_l, pads.tx_h).eq(self.oe.storage)
		]
		self.specials += MultiReg(Cat(ts_l.i, ts_h.i), self.i.status)
