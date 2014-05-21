.section    .text, "ax", @progbits
.global     _start
_start:
	mvhi    r1, hi(_fbss)
	ori     r1, r1, lo(_fbss)
	mvhi    r2, hi(_ebss)
	ori     r2, r2, lo(_ebss)
0:
	be      r1, r2, 1f
	sw      (r1+0), r0
	addi    r1, r1, 4
	bi      0b
1:
	mvhi    r1, hi(ventilator_kernel)
	ori     r1, r1, lo(ventilator_kernel)
	ret
