# Robert Jordens <jordens@gmail.com>, 2014

from migen.fhdl.std import *
from migen.genlib.coding import PriorityEncoder
from . import Slave

class TristateDS(Module):
	def __init__(self, pad, pad_n=None):
		if pad_n is None:
			dq = TSTriple()
			self.i = dq.i
			self.o = dq.o
			self.oe = dq.oe
			self.specials += dq.get_tristate(pad)
		else:
			self.i = Signal()
			self.o = Signal()
			self.oe = Signal()
			self.specials += Instance("IOBUFDS",
					i_T=~self.oe, o_O=self.i, i_I=self.o,
					io_IO=pad, io_IOB=pad_n)

class IOSerdesMS(Module):
	def __init__(self, pad, pad_n=None):
		self.i = i = Signal(8)
		self.o = o = Signal(8)
		self.oe = oe = Signal(8)
		self.sys8_stb = Signal()

		###

		q = TristateDS(pad, pad_n)
		self.submodules += q
		t = Signal()
		self.comb += q.oe.eq(~t)

		sys8 = ClockSignal("sys8")
		icascade = Signal()
		icommon = dict(p_BITSLIP_ENABLE="FALSE", p_DATA_RATE="SDR",
				p_DATA_WIDTH=8, p_INTERFACE_TYPE="RETIMED",
				i_BITSLIP=0, i_CE0=1, i_IOCE=self.sys8_stb, i_RST=ResetSignal(),
				i_CLK0=sys8, i_CLK1=0, i_CLKDIV=ClockSignal())
		self.specials += Instance("ISERDES2", p_SERDES_MODE="MASTER",
				o_Q4=i[7], o_Q3=i[6], o_Q2=i[5], o_Q1=i[4],
				o_SHIFTOUT=icascade, i_D=q.i, i_SHIFTIN=0, **icommon)
		self.specials += Instance("ISERDES2", p_SERDES_MODE="SLAVE",
				o_Q4=i[3], o_Q3=i[2], o_Q2=i[1], o_Q1=i[0],
				i_D=0, i_SHIFTIN=cascade, **icommon)

		ocascade = Signal(4)
		ocommon = dict(p_DATA_RATE_OQ="SDR", p_DATA_RATE_OT="SDR",
				p_DATA_WIDTH=8, p_OUTPUT_MODE="SINGLE_ENDED", i_TRAIN=0,
				i_CLK0=sys8, i_CLK1=0, i_CLKDIV=ClockSignal(),
				i_IOCE=self.sys8_stb, i_OCE=1, i_TCE=1, i_RST=ResetSignal())
		self.specials += Instance("OSERDES2", p_SERDES_MODE="MASTER",
				i_T4=~oe[7], i_T3=~oe[6], i_T2=~oe[5], i_T1=~oe[4],
				i_D4=q[7], i_D3=q[6], i_D2=q[5], i_D1=q[4],
				i_SHIFTIN1=1, i_SHIFTIN2=1,
				i_SHIFTIN3=cascade[2], i_SHIFTIN4=cascade[3],
				o_SHIFTOUT1=cascade[0], o_SHIFTOUT2=cascade[1],
				o_OQ=q.o, o_TQ=t,
				**ocommon)
		self.specials += Instance("OSERDES2", p_SERDES_MODE="SLAVE",
				i_T4=~oe[3], i_T3=~oe[2], i_T2=~oe[1], i_T1=~oe[0],
				i_D4=q[3], i_D3=q[2], i_D2=q[1], i_D1=q[0],
				i_SHIFTIN1=cascade[0], i_SHIFTIN2=cascade[1],
				i_SHIFTIN3=1, i_SHIFTIN4=1,
				o_SHIFTOUT3=cascade[2], o_SHIFTOUT4=cascade[3],
				**ocommon)

class IOSerdes(Module):
	def __init__(self, pad, pad_n=None):
		self.i = Signal(8)
		self.o = Signal(8)
		self.oe = Signal(8)
		self.sys8_stb = Signal()
		self.sys2_stb = Signal()

		###

		q = TristateDS(pad, pad_n)
		self.submodules += q
		t = Signal()
		self.comb += q.oe.eq(~t)

		sys8 = ClockSignal("sys8")
		sys2 = ClockSignal("sys2")
		o4 = Signal(4)
		oe4 = Signal(4)
		i4 = Signal(4)
		i4h = Signal(4)
		self.sync += self.i.eq(Cat(i4h, i4)) # 1 cycle
		self.sync.sys2 += i4h.eq(i4)
		self.comb += [
				o4.eq(Mux(self.sys2_stb, self.o[:4], self.o[4:])),
				oe4.eq(Mux(self.sys2_stb, self.oe[:4], self.oe[4:])),
				]
		self.specials += Instance("ISERDES2", p_SERDES_MODE="NONE",
				p_BITSLIP_ENABLE="FALSE", p_DATA_RATE="SDR",
				p_DATA_WIDTH=4, p_INTERFACE_TYPE="RETIMED",
				i_BITSLIP=0, i_CE0=1, i_IOCE=self.sys8_stb, i_RST=ResetSignal(),
				i_CLK0=sys8, i_CLK1=0, i_CLKDIV=sys2,
				o_Q4=i4[3], o_Q3=i4[2], o_Q2=i4[1], o_Q1=i4[0],
				i_D=q.i, i_SHIFTIN=0)
		self.specials += Instance("OSERDES2", p_SERDES_MODE="NONE",
				p_DATA_RATE_OQ="SDR", p_DATA_RATE_OT="SDR",
				p_DATA_WIDTH=4, p_OUTPUT_MODE="SINGLE_ENDED", i_TRAIN=0,
				i_CLK0=sys8, i_CLK1=0, i_CLKDIV=sys2,
				i_IOCE=self.sys8_stb, i_OCE=1, i_TCE=1, i_RST=ResetSignal(),
				i_T4=~oe4[3], i_T3=~oe4[2], i_T2=~oe4[1], i_T1=~oe4[0],
				i_D4=o4[3], i_D3=o4[2], i_D2=o4[1], i_D1=o4[0],
				o_OQ=q.o, o_TQ=t)


class HiresGpio(Slave):
	"""
	priority is:
	* only first edge in a clock
	* pins have decreasing priority
	the following can be missed:
	* pulses shorter than one clock
	* opposite edges on lower priority pins

	latency is 2 + oserdes2 out and iserdes2 + 2 in
	"""
	def __init__(self, pads, pads_n=None, cdmap=None):
		super(HiresGpio, self).__init__()
		self.sys8_stb = Signal(4)

		###

		n = flen(pads)

		i0 = Signal(n)
		i = Signal(n)
		o = Signal(n)
		b = Signal(n)
		o0 = Signal(n)
		oe = Signal(n)
		r = Signal(n)
		f = Signal(n)
		ti = Signal(3)
		to = Signal(3)

		j = Signal()
		k = Signal()
		sys2_stb = Signal()
		self.sync += j.eq(~j)
		self.sync.sys2 += k.eq(j), sys2_stb.eq(k == j)

		# dout
		edges = Array([0xff^((1<<i) - 1) for i in range(8)])
		edge_out = Signal(8)
		edge_out_n = Signal(8)
		rise_out = Signal(n)
		fall_out = Signal(n)
		self.comb += [
				edge_out.eq(edges[to]),
				edge_out_n.eq(~edge_out),
				rise_out.eq(~o0 & o),
				fall_out.eq(o0 & ~o),
				]

		ios = []
		for j in range(n):
			io = IOSerdes(pads[j], pads_n[j] if pads_n else None)
			cdj = cdmap[j] if cdmap else 0
			io = RenameClockDomains(io, {"sys8": "sys8%i" % cdj})
			self.submodules += io
			ios.append(io)
			self.comb += io.sys8_stb.eq(self.sys8_stb[cdj])
			self.comb += io.sys2_stb.eq(sys2_stb)
			self.comb += [ # 0 cycle
					io.oe.eq(Replicate(oe[j], 8)),
					If(rise_out[j],
						io.o.eq(edge_out),
					).Elif(fall_out[j],
						io.o.eq(edge_out_n),
					).Else(
						io.o.eq(Replicate(o[j], 8)),
					)]

		# din
		self.submodules.pe_ti = pe_ti = PriorityEncoder(8)
		self.submodules.pe_sel = pe_sel = PriorityEncoder(n)
		rise_in = Signal(n)
		fall_in = Signal(n)
		edge_in = Signal(8)
		bi = Signal()
		i0i = Signal()
		self.comb += [
				i.eq(Cat(io.i[-1] for io in ios) ^ b),
				rise_in.eq((~i0 & r) & i),
				fall_in.eq((i0 & f) & ~i),
				pe_sel.i.eq(rise_in | fall_in),
				i0i.eq(Array(fiter(i0))[pe_sel.o]),
				bi.eq(Array(fiter(b))[pe_sel.o]),
				edge_in.eq(Array([io.i for io in ios])[pe_sel.o]),
				pe_ti.i.eq(edge_in ^ Replicate(i0i ^ bi, 8)),
				ti.eq(pe_ti.o),
				]

		# ventilator bus
		self.comb += [
				self.dout.ack.eq(1),
				self.busy.eq(self.din.stb & ~self.din.ack),
				]

		self.sync += [ # 1 cycle each
				If(self.din.stb & self.din.ack,
					self.din.stb.eq(0),
				),
				If(self.dout.stb,
					to.eq(self.dout.payload.addr[4:]),
					self.din.payload.addr.eq(0),
					Case(self.dout.payload.addr[:4], {
						0x0: [self.din.stb.eq(1), self.din.payload.data.eq(i)],
						0x1: [o.eq(self.dout.payload.data ^ b), o0.eq(o)],
						0x2: oe.eq(self.dout.payload.data),
						0x3: r.eq(self.dout.payload.data),
						0x4: f.eq(self.dout.payload.data),
						0x5: b.eq(self.dout.payload.data),
					}),
				),
				If(~(self.dout.stb & (self.dout.payload.addr[:4] == 0))
						& ~(self.din.stb & ~self.din.ack),
					i0.eq(i),
					If(~pe_sel.n,
						self.din.stb.eq(1),
						self.din.payload.addr[4:].eq(ti),
						self.din.payload.addr[:4].eq(Mux(~i0i, 0x6, 0x7)),
						self.din.payload.data.eq(Mux(~i0i, rise_in, fall_in)),
					),
				)]
