# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from slave import Slave

class Spi(Slave):
	"""
	* device i2c/spi: writes, reads (with selects, all other lines (fud,
	      reset..) are tdcdtc)
		addr 22 (1 r/w, 3 speed, 2 len, 8 sel, 8 addr), 32 data
	"""
