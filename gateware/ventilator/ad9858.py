# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from slave import Slave

class Ad9858(Slave):
	"""
	* device dds: writes, reads (fud, reset go through tdcdtc)
		addr 14 (1 r/w, 5 sel, 8 addr), n data
	"""

