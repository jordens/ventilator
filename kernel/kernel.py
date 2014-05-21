#!/usr/bin/python3
# Robert Jordens <jordens@gmail.com>, 2014

import os
import struct
import asyncio
import logging
import termios
from enum import Enum
from tempfile import mkstemp
from collections import namedtuple

logger = logging.getLogger("ventilator")

class MsgType(Enum):
	NONE = 0x00
	ERR = 0xff

	LOAD = 0x10
	UNLOAD = 0x11
	EXIT = 0x12
	STATUS = 0x13
	START = 0x14
	STOP = 0x15
	PUSH = 0x18
	POP = 0x19
	SETUP = 0x20
	UPDATE = 0x21
	ARM = 0x22
	TRIGGER = 0x23
	ABORT = 0x24
	DONE = 0x25
	CLEANUP = 0x26

class MsgStatus(Enum):
	NONE = 0x00
	REQ = 0x01
	ACK = 0x02
	NACK = 0x03

_magic = b"\xa5"

_Msg = struct.Struct(">cBBB")
_Event = struct.Struct(">III")
Event = namedtuple("Event", "time addr data")


class Ventilator:
	compile_opt = """lm32-elf-gcc
		-mbarrel-shift-enabled -mmultiply-enabled
		-mdivide-enabled -msign-extend-enabled
		-Os -s -nostdinc -nostdlib -nodefaultlibs
		-fno-builtin -fdata-sections
		-ffunction-sections -funit-at-a-time
		-Wall -Wstrict-prototypes -Wold-style-definition
		-Wextra -Wmissing-prototypes
		-Isoftware -I../misoc/software/include/base
		-I../misoc/software/include
		-Wl,-L../misoc/software/include -Tkernel/kernel.ld
		-Wl,-N -Wl,--gc-section -Wl,--oformat=binary""".split()
	crt0 = "kernel/crt0.S"

	def __init__(self, port, speed=115200):
		self.loop = asyncio.get_event_loop()
		self._open(port, speed)

	def _open(self, port, speed):
		self._fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
		self.port = os.fdopen(self._fd, "r+b", buffering=0)
		iflag, oflag, cflag, lflag, ispeed, ospeed, cc = \
				termios.tcgetattr(self._fd)
		iflag = termios.IGNBRK | termios.IGNPAR
		oflag = 0
		cflag |= termios.CLOCAL | termios.CREAD | termios.CS8
		lflag = 0
		ispeed = ospeed = getattr(termios, "B%s" % speed)
		cc[termios.VMIN] = 1
		cc[termios.VTIME] = 0
		termios.tcsetattr(self._fd, termios.TCSANOW, [
			iflag, oflag, cflag, lflag, ispeed, ospeed, cc])
		termios.tcdrain(self._fd)
		termios.tcflush(self._fd, termios.TCOFLUSH)
		termios.tcflush(self._fd, termios.TCIFLUSH)

	@asyncio.coroutine
	def connect(self):
		wtransport, wprotocol = yield from self.loop.connect_write_pipe(
				asyncio.Protocol, self.port)
		self.reader = asyncio.StreamReader(loop=self.loop)
		rprotocol = asyncio.StreamReaderProtocol(self.reader, loop=self.loop)
		rtransport, _ = yield from self.loop.connect_read_pipe(
				lambda: rprotocol, self.port)
		self.writer = asyncio.StreamWriter(wtransport, wprotocol, self.reader,
				self.loop)

	def close(self):
		self.writer.close()
		self.port.close()

	@asyncio.coroutine
	def compile(self, *files):
		fd, temp = mkstemp()
		proc = yield from asyncio.create_subprocess_exec(
				*(self.compile_opt + ["-o", temp, self.crt0] + list(files)))
		exit = yield from asyncio.wait_for(proc.wait(), timeout=None)
		kernel = os.fdopen(fd, "rb").read()
		try:
			os.remove(temp)
		except FileNotFoundError:
			pass
		if exit:
			raise ValueError("compilation failed ({})".format(exit))
		return kernel

	def pack_events(self, ev):
		return b"".join(_Event.pack(*i) for i in ev)

	def unpack_events(self, data):
		for i in range(0, len(data), _Event.size):
			yield Event._make(_Event.unpack(data[i:i+_Event.size]))

	def send(self, typ, status=MsgType.NONE, data=b""):
		assert len(data) < 256, len(data)
		logger.debug("send, %s, %s, %s", typ, status, data)
		s = _Msg.pack(_magic, typ.value, status.value, len(data))
		return self.writer.write(s + data)

	@asyncio.coroutine
	def recv(self):
		s = yield from self.reader.readexactly(_Msg.size)
		fail = b""
		while not s.startswith(_magic):
			fail = fail + s[:1]
			assert len(fail) < 128, fail
			c = yield from self.reader.readexactly(1)
			s = s[1:] + c
		magic, typ, status, n = _Msg.unpack(s)
		typ, status = MsgType(typ), MsgStatus(status)
		data = b""
		if n:
			data = yield from self.reader.readexactly(n)
		logger.debug("recv, %s, %s, %s", typ, status, data)
		return typ, status, data

	@asyncio.coroutine
	def req(self, typ, data=b""):
		self.send(typ, MsgStatus.REQ, data)
		rtyp, status, data = yield from self.recv()
		assert rtyp == typ, (typ, rtyp, status, data)
		assert status == MsgStatus.ACK, (typ, rtyp, status, data)
		return data

	@asyncio.coroutine
	def rep(self):
		typ, status, data = yield from self.recv()
		assert status == MsgStatus.REQ
		status, data = yield from self.handle_req(typ, data)
		self.send(typ, status, data)

	@asyncio.coroutine
	def handle_req(self, typ, data):
		return MsgStatus.NACK, b""

	@asyncio.coroutine
	def load(self, kernel, address):
		for pos in range(0, len(kernel), 256 - 8):
			chunk = kernel[pos:pos+256-8]
			addr = struct.pack(">I", address + pos)
			yield from self.req(MsgType.LOAD, data=addr+chunk)
		yield from self.req(MsgType.LOAD, struct.pack(">I", address))

	@asyncio.coroutine
	def kernel(self, sources, address, runs, repeats):
		yield from self.connect()
		s = yield from self.req(MsgType.STATUS)
		logger.info("status %s", list(self.unpack_events(s)))
		self.send(MsgType.STOP)
		self.send(MsgType.UNLOAD)

		kernel = yield from self.compile(*sources)
		yield from self.load(kernel, address)
		yield from self.req(MsgType.SETUP)
		yield from self.req(MsgType.UPDATE, self.pack_events([
			Event(time=0, addr=0, data=runs),
			Event(time=0, addr=1, data=repeats),
			]))
		yield from self.req(MsgType.ARM)

		yield from self.req(MsgType.TRIGGER)
		for i in range(repeats):
			n = {}
			while True:
				typ, status, data = yield from self.recv()
				assert status == MsgStatus.NONE
				if typ == MsgType.UPDATE:
					r = struct.unpack(">%iI" % (len(data)//4), data)
					for a in range(len(r) - 2):
						n[a + r[1]] = r[2 + a]
				if typ == MsgType.DONE:
					break
			logger.info("result %s", n)

		self.send(MsgType.CLEANUP)


def main():
	"""
Serial boot program and terminal client;

Usage: ventilator [options] [SOURCE ...]

Options:

-p, --port <port>         serial port [default: /dev/ttyUSB1]
-s, --speed <speed>       line speed [default: 115200]
-a, --address <address>   load address [default: 0x40010000]
-r, --repeats <repeats>   repetitions [default: 10]
-x, --runs <runs>         runs [default: 100]
-d, --debug
"""
	import docopt
	args = docopt.docopt(main.__doc__)
	v = Ventilator(args["--port"], int(args["--speed"]))
	if args["--debug"]:
		logging.basicConfig(level=logging.DEBUG)
	else:
		logging.basicConfig(level=logging.INFO)

	t = v.kernel(args["SOURCE"], address=int(args["--address"], 16),
		runs=int(args["--runs"]), repeats=int(args["--repeats"]))
	asyncio.get_event_loop().run_until_complete(t)

if __name__ == "__main__":
	main()
