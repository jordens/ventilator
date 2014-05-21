# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from migen.flow.actor import Source, Sink
from migen.genlib.record import Record

ventilator_layout = [
		("time", 32),
		("addr", 32),
		("data", 32),
		]
slave_layout = ventilator_layout[1:]

class Slave(Module):
	def __init__(self):
		self.dout = Sink(slave_layout)
		self.din = Source(slave_layout)
		self.busy = Signal()

class Loopback(Slave):
	def __init__(self):
		super(Loopback, self).__init__()
		self.comb += Record.connect(self.dout, self.din)
