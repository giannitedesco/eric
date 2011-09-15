start:
	; load parameters
	ldi x1, $3
	ldi y1, $7

	; result
	ldi x0, $0

	; -1 to decrement loop counter
	ldi y0, $0xff
loop:
	; do this round of the multiplication
	add x0, y1, x0

	; decrement 
	add x1, y0, x1
	jnz loop
end:
	halt
