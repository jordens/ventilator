include $(MSCDIR)/software/common.mak

CFLAGS := $(CFLAGS) -I../software \
	-ffunction-sections -fdata-sections
CFLAGS := $(CFLAGS) -Os --verbose-asm -Wa,-a=kernel.lst -save-temps
LDFLAGS := $(LDFLAGS) --gc-sections

OBJECTS=crt0.o kernel.o

-include $(OBJECTS:.o=.d)

all: kernel.bin

kernel.elf: $(OBJECTS)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	chmod -x $@

%.elf:
	$(LD) $(LDFLAGS) \
		-T kernel.ld \
		-N -o $@ \
		$(OBJECTS)
	chmod -x $@

%.o: %.c
	$(compile-dep)

%.o: %.S
	$(assemble)

load: kernel.bin
	$(MSCDIR)/tools/flterm --kernel kernel.bin --kernel-adr 0x10000000 --port /dev/ttyUSB1

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) kernel.elf kernel.bin .*~ *~ *.lst

.PHONY: all clean load
