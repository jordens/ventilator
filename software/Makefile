include $(MSCDIR)/software/common.mak

OBJECTS=isr.o main.o ventilator.o

-include $(OBJECTS:.o=.d)

all: ventilator.bin ventilator.fbi

ventilator.elf: $(OBJECTS) libs

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	chmod -x $@

%.fbi: %.bin
	$(MSCDIR)/mkmscimg.py -f -o $@ $<

%.elf:
	$(LD) $(LDFLAGS) \
		-T $(MSCDIR)/software/libbase/linker-sdram.ld \
		-N -o $@ \
		$(MSCDIR)/software/libbase/crt0.o \
		$(OBJECTS) \
		-L$(MSCDIR)/software/libbase \
		-L$(MSCDIR)/software/libcompiler-rt \
		-lbase-nofloat -lcompiler-rt
	chmod -x $@

%.o: %.c
	$(compile-dep)

%.o: %.S
	$(assemble)

flash: ventilator.fbi
	$(MSCDIR)/flash_extra.py papilio_pro ventilator.fbi 0x70000

load: ventilator.bin
	xc3sprog -c papilio -R
	$(MSCDIR)/tools/flterm --kernel ventilator.bin --port /dev/ttyUSB1

libs:
	$(MAKE) -C $(MSCDIR)/software/libcompiler-rt
	$(MAKE) -C $(MSCDIR)/software/libbase

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) ventilator.elf ventilator.bin .*~ *~

.PHONY: all clean libs flash load
