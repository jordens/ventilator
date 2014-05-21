# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from migen.bus import wishbone, csr
from migen.bus.transactions import *
from migen.sim.generic import run_simulation
from migen.bank import csrgen

from gateware.ventilator import Master, Loopback, Gpio, Wishbone

def _test_gen():
	yield TWrite(0, 0) # start
	yield
	yield TWrite(8, 0) # update
	t0 = TRead(7) # cycle0
	yield t0
	assert t0.data >= 0 and t0.data < 4, t0.data
	yield TWrite(8, 0) # update
	t = TRead(7) # cycle0
	yield t
	assert t.data > t0.data and t.data - t0.data < 10, (t.data, t0.data)

	yield from _test_out(0x30, 0x00000000, 0xdeadbeef)
	out = []
	yield from _test_in(out)
	assert out[0] == [0x30, 0x0, 0xdeadbeef], out

	yield from _test_out(0x60, 0x3f000001, 0x12345678)
	yield from _test_out(0x70, 0x2f000002, 0x00000070)
	yield from _test_out(0x80, 0x2f000003, 0x00000080)
	for i in range(2):
		out = []
		yield from _test_in(out)
		print(list(map(hex, out[0])))

	yield from _test_out(0x100, 0x00000100, 0x00000000) # r i
	out = []
	yield from _test_in(out)
	print(list(map(hex, out[0])))

	yield from _test_out(0x18d, 0x00000102, 0x0000000f) # w oe
	yield from _test_out(0x18e, 0x00000103, 0x000000f0) # w rise
	yield from _test_out(0x18f, 0x00000104, 0x000000f0) # w fall
	yield from _test_out(0x190, 0x00000101, 0x0000000f) # w o
	yield from _test_out(0x191, 0x00000101, 0x00000000) # w o
	yield from _test_out(0x192, 0x00000101, 0x0000000f) # w o
	yield from _test_out(0x193, 0x00000101, 0x00000000) # w o
	for i in range(4):
		out = []
		yield from _test_in(out)
		print(list(map(hex, out[0])))
		exp = [0x190 + 2 + i, [0x106, 0x107][i % 2], 0xf0]
		assert out[0] == exp, (out, exp)


def _test_write32(addr, val):
	for i in range(4):
		yield TWrite(addr + 3 - i, val & 0xff)
		val >>= 8

def _test_read32(addr, out):
	val = 0
	for i in range(4):
		t = TRead(addr + i)
		yield t
		val = (val << 8) | (t.data & 0xff)
	out.append(val)

def _test_out(time, addr, data):
	i = 26
	yield from _test_write32(i, time)
	yield from _test_write32(i+4, addr)
	yield from _test_write32(i+8, data)
	while True:
		t = TRead(9)
		yield t
		if t.data & 0x2:
			break
	yield TWrite(i+12, 0) # out_next

def _test_in(out):
	while True:
		t = TRead(9)
		yield t
		if t.data & 0x1:
			break
	v = []
	i = 12
	yield from _test_read32(i, v)
	yield from _test_read32(i+4, v)
	yield from _test_read32(i+8, v)
	yield TWrite(i+12, 0) # in_next
	out.append(v)

def _test_gen_wb():
	for i in range(100):
		yield
	yield TRead(0)
	yield TWrite(0x20, 1)
	yield TWrite(0x21, 2)
	yield TWrite(0x22, 3)
	yield TWrite(0x2f, 0)
	for i in [0, 0x10, 0x11, 0x12]:
		yield TRead(i)

class _TB(Module):
	def __init__(self):
		pads = Signal(8)
		self.submodules.gp = Gpio(pads)
		self.comb += pads[4:].eq(pads[:4])
		self.submodules.wb = Wishbone()
		self.submodules.dut = Master([
			(self.gp, 0x00000100, 0xffffff00),
			# (self.dds, 0x00010000, 0xffff0000),
			(self.wb, 0x20000000, 0xe0000000),
			# (self.spi, 0x40000000, 0xe0000000),
			# (self.i2c, 0x60000000, 0xe0000000),
			])
		self.submodules.csrbanks = csrgen.BankArray(self,
				lambda name, mem: {"dut": 0}[name])
		self.submodules.ini = csr.Initiator(_test_gen())
		self.submodules.con = csr.Interconnect(self.ini.bus,
				self.csrbanks.get_buses())
		#self.submodules.wbini = wishbone.Initiator(_test_gen_wb())
		#self.submodules.wbtap = wishbone.Tap(self.dut.bus)
		#self.submodules.wbcon = wishbone.InterconnectPointToPoint(
		#		self.wbini.bus, self.dut.bus)
		self.submodules.wbtg = wishbone.Target(wishbone.TargetModel())
		self.submodules.wbtap = wishbone.Tap(self.wbtg.bus)
		self.submodules.wbic = wishbone.InterconnectPointToPoint(
				self.wb.bus, self.wbtg.bus)


if __name__ == "__main__":
	from migen.fhdl import verilog
	#print(verilog.convert(_TB()))
	run_simulation(_TB(), vcd_name="ventilator.vcd", ncycles=1000)
