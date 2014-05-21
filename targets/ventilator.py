from fractions import Fraction

from migen.fhdl.std import *
from migen.bus import wishbone
from mibuild.generic_platform import *

from misoclib import gpio, spiflash, lasmicon
from misoclib.gensoc import SDRAMSoC
from misoclib.sdramphy import gensdrphy

from gateware import ttlgpio, ad9858, ventilator

_ventilator_io = [
	("user_led", 1, Pins("B:7"), IOStandard("LVTTL")),
	("ttl", 0,
		Subsignal("d_l", Pins("C:11 C:10 C:9 C:8 C:7 C:6 C:5 C:4")),
		Subsignal("d_h", Pins("C:3 C:2 C:1 C:0 B:4 A:11 B:5 A:10")),
		Subsignal("tx_l", Pins("A:9")),
		Subsignal("tx_h", Pins("B:6")),
		IOStandard("LVTTL")),
	("inputs", 0, Pins("C:12 C:13 C:14 C:15"), IOStandard("LVTTL")),
	("all_gpio", 0, Pins(
		"C:12 C:13 C:14 C:15 " # inputs
		"C:11 C:10 C:9 C:8 C:7 C:6 C:5 C:4 " # out
		"C:3 C:2 C:1 C:0 B:4 A:11 B:5 A:10 " # out
		"A:9 B:6 " # txl txh
		#"A:3 B:11" # dds
		), IOStandard("LVTTL"), Misc("PULLDOWN"), Misc("DRIVE=4")),
	("dds", 0,
		Subsignal("a", Pins("A:5 B:10 A:6 B:9 A:7 B:8")),
		Subsignal("d", Pins("A:12 B:3 A:13 B:2 A:14 B:1 A:15 B:0")),
		Subsignal("sel", Pins("A:2 B:14 A:1 B:15 A:0")),
		Subsignal("p", Pins("A:8 B:12")),
		Subsignal("fud_n", Pins("B:11")),
		Subsignal("wr_n", Pins("A:4")),
		Subsignal("rd_n", Pins("B:13")),
		Subsignal("rst_n", Pins("A:3")),
		IOStandard("LVTTL")),
]

class _CRG(Module):
	def __init__(self, platform, clk_freq):
		self.clock_domains.cd_sys = ClockDomain()
		self.clock_domains.cd_sys_ps = ClockDomain()
		self.clock_domains.cd_sys2 = ClockDomain()
		self.clock_domains.cd_sys80 = ClockDomain()
		self.clock_domains.cd_sys81 = ClockDomain()
		self.sys8_stb = Signal(2)

		f0 = 32e6
		clk32 = platform.request("clk32")
		clk32a = Signal()
		self.specials += Instance("IBUFG", i_I=clk32, o_O=clk32a)
		clk32b = Signal()
		self.specials += Instance("BUFIO2", p_DIVIDE=1,
			p_DIVIDE_BYPASS="TRUE", p_I_INVERT="FALSE",
			i_I=clk32a, o_DIVCLK=clk32b)
		f = Fraction(int(clk_freq), int(f0))
		n, m, p = f.denominator, f.numerator, 8
		assert f0/n*m == clk_freq
		pll_lckd = Signal()
		pll_fb = Signal()
		pll = Signal(6)
		self.specials.pll = Instance("PLL_ADV", p_SIM_DEVICE="SPARTAN6",
				p_BANDWIDTH="OPTIMIZED", p_COMPENSATION="INTERNAL",
				p_REF_JITTER=.01, p_CLK_FEEDBACK="CLKFBOUT",
				i_DADDR=0, i_DCLK=0, i_DEN=0, i_DI=0, i_DWE=0, i_RST=0, i_REL=0,
				p_DIVCLK_DIVIDE=1, p_CLKFBOUT_MULT=m*p//n, p_CLKFBOUT_PHASE=0.,
				i_CLKIN1=clk32b, i_CLKIN2=0, i_CLKINSEL=1,
				p_CLKIN1_PERIOD=1/f0, p_CLKIN2_PERIOD=0.,
				i_CLKFBIN=pll_fb, o_CLKFBOUT=pll_fb, o_LOCKED=pll_lckd,
				o_CLKOUT0=pll[0], p_CLKOUT0_DUTY_CYCLE=.5,
				o_CLKOUT1=pll[1], p_CLKOUT1_DUTY_CYCLE=.5,
				o_CLKOUT2=pll[2], p_CLKOUT2_DUTY_CYCLE=.5,
				o_CLKOUT3=pll[3], p_CLKOUT3_DUTY_CYCLE=.5,
				o_CLKOUT4=pll[4], p_CLKOUT4_DUTY_CYCLE=.5,
				o_CLKOUT5=pll[5], p_CLKOUT5_DUTY_CYCLE=.5,
				p_CLKOUT0_PHASE=0., p_CLKOUT0_DIVIDE=p//8, # sys80
				p_CLKOUT1_PHASE=0., p_CLKOUT1_DIVIDE=p//8, # sys81
				p_CLKOUT2_PHASE=0., p_CLKOUT2_DIVIDE=p//2, # sys2
				p_CLKOUT3_PHASE=0., p_CLKOUT3_DIVIDE=p//1, # sys
				p_CLKOUT4_PHASE=270., p_CLKOUT4_DIVIDE=p//1, # sys_ps
				p_CLKOUT5_PHASE=0., p_CLKOUT5_DIVIDE=p//1, #
			)
		self.specials += Instance("BUFPLL", p_DIVIDE=4,
				i_PLLIN=pll[0], i_GCLK=self.cd_sys2.clk,
				i_LOCKED=pll_lckd, o_IOCLK=self.cd_sys80.clk,
				o_SERDESSTROBE=self.sys8_stb[0])
		self.specials += Instance("BUFPLL", p_DIVIDE=4,
				i_PLLIN=pll[1], i_GCLK=self.cd_sys2.clk,
				i_LOCKED=pll_lckd, o_IOCLK=self.cd_sys81.clk,
				o_SERDESSTROBE=self.sys8_stb[1])
		self.specials += Instance("BUFG", i_I=pll[2], o_O=self.cd_sys2.clk)
		self.specials += Instance("BUFG", i_I=pll[3], o_O=self.cd_sys.clk)
		self.specials += Instance("BUFG", i_I=pll[4], o_O=self.cd_sys_ps.clk)
		self.specials += Instance("FD", p_INIT=1, i_D=~pll_lckd,
				o_Q=self.cd_sys.rst, i_C=self.cd_sys.clk)

		sdram_clk = platform.request("sdram_clock")
		self.specials += Instance("ODDR2", p_DDR_ALIGNMENT="NONE",
				p_INIT=0, p_SRTYPE="SYNC", i_D0=0, i_D1=1, i_S=0,
				i_R=0, i_CE=1, i_C0=self.cd_sys.clk,
				i_C1=~self.cd_sys.clk, o_Q=sdram_clk)

class VentilatorSoC(SDRAMSoC):
	default_platform = "papilio_pro"

	csr_map = {
		"test_inputs":	10,
		"test_ttl":		11,
		"ventilator":	12,
	}
	csr_map.update(SDRAMSoC.csr_map)
	interrupt_map = {
		"ventilator":	2,
	}
	interrupt_map.update(SDRAMSoC.interrupt_map)

	def __init__(self, platform, **kwargs):
		clk_freq = int(80e6)
		SDRAMSoC.__init__(self, platform,
			clk_freq=clk_freq,
			cpu_reset_address=0x160000,
			sram_size=0x400,
			**kwargs)
		platform.add_extension(_ventilator_io)
		platform.ise_commands = """
trce -v 12 -fastpaths -o {build_name} {build_name}.ncd {build_name}.pcf
"""

		self.submodules.crg = _CRG(platform, clk_freq)

		sdram_geom = lasmicon.GeomSettings(
				bank_a=2,
				row_a=12,
				col_a=8
		)
		sdram_timing = lasmicon.TimingSettings(
				tRP=self.ns(15),
				tRCD=self.ns(15),
				tWR=self.ns(14),
				tWTR=2,
				tREFI=self.ns(64e6/4096, False),
				tRFC=self.ns(66),
				req_queue_size=8,
				read_time=32,
				write_time=16
		)
		self.submodules.sdrphy = gensdrphy.GENSDRPHY(platform.request("sdram"))
		self.register_sdram_phy(self.sdrphy.dfi, self.sdrphy.phy_settings, sdram_geom, sdram_timing)

		# BIOS is in SPI flash
		self.submodules.spiflash = spiflash.SpiFlash(platform.request("spiflash2x"),
			cmd=0xefef, cmd_width=16, addr_width=24, dummy=4, div=4)
		self.flash_boot_address = 0x70000
		self.register_rom(self.spiflash.bus)

		self.submodules.leds = gpio.GPIOOut(Cat(platform.request("user_led", i) for i in range(2)))
		#self.comb += platform.request("user_led", 0).eq(ResetSignal())
		if False:
			self.submodules.test_inputs = gpio.GPIOIn(platform.request("inputs"))
			self.submodules.test_ttl = ttlgpio.TTLGPIO(platform.request("ttl"))
			self.submodules.dds = ad9858.AD9858(platform.request("dds"))
			self.add_wb_slave(
					lambda a: (a & (0x70000000 >> 2)) == (0x30000000 >> 2),
					self.dds.bus)
		else:
			#self.submodules.gp = ventilator.Gpio(platform.request("all_gpio"))
			# have ise distribute the two pll signals and the bufplls to
			# banks 0 and 1. gpio distribution on the banks is:
			# 4 in, 8 out, 4 out on bank0 and 4out, 2oe on bank1
			self.submodules.gp = ventilator.HiresGpio(platform.request("all_gpio"),
					cdmap=[0] * (4 + 8 + 4) + [1] * (4 + 2))
			self.comb += self.gp.sys8_stb.eq(self.crg.sys8_stb)
			self.submodules.wb = ventilator.Wishbone()
			self.submodules.dds = ad9858.AD9858(platform.request("dds"))
			self.submodules.ventilator = ventilator.Master([
					(self.gp,  0x00000100, 0xffffff00),
					#(self.dds, 0x00010000, 0xffff0000),
					(self.wb,  0x20000000, 0xe0000000), # 0bxxxWSSSS
					#(self.spi, 0x40000000, 0xe0000000),
					#(self.i2c, 0x60000000, 0xe0000000),
					])
			self.submodules.wbcon = wishbone.Decoder(self.wb.bus, [
					(lambda a: (a & 0x00ffff00) == 0x00000000, self.dds.bus),
					])
			self.add_wb_slave(
					lambda a: (a & (0x70000000 >> 2)) == (0x30000000 >> 2),
					self.ventilator.bus)

default_subtarget = VentilatorSoC
