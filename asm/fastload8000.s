;fastload8000.s

.include "apple2.inc"
.include "inflate.inc"
.include "autoload.inc"
.include "fastload.inc"

cout	=	COUT		; character out sub
crout	=	CROUT		; CR out sub
prbyte	=	PRBYTE 		; print byte in hex
warm	=	MONZ		; back to monitor
tapein	=	TAPEIN		; read tape interface

pointer	=	$06
endbas	=	$80C
;target	=	$1000
target	=	$801
chksum	=	$00
;inf_zp	=	$0		; see inflate.inc

start:
        .org	endbas
move:
	ldx	#0
move1:	lda	moved,x
        sta	fast,x
	inx
	bne	move1		; move 256 bytes
;	ldx	#0
move2:	lda	moved+256,x
	sta	fast+256,x
	inx
	bpl	move2		; only 128 bytes to move

	lda	#<loadm		; print "LOADING ..."
	ldy	#>loadm
	jsr	print		; in high mem

	jmp	fast

	; this will cause a range error if autoload_msg is incorrectly positioned
	.res	autoload_msg-*, 0	; align to fixed start of loadm
loadm:	
	.asciiz	"LOADING..."		; overwritten by audio builder
	.res	autoload_mlen-11	; reserved message space

moved:
	.org	fastload_org
fast:
	lda	#$ff		; initial
	sta	chksum		; .. checksum value

	lda	ld_beg		; load begin LSB location
	sta	store+1		; store it
	lda	ld_beg+1	; load begin MSB location
	sta	store+2		; store it

	ldx	#0		; set X to 0

nsync:	bit	tapein		; 4 cycles, sync bit ; first pulse
	bpl	nsync		; 2 + 1 cycles

main:	lda	#1		; 2, load sentinel bit
	clc			; 2, clear carry (byte complete flag)
nextbit:
	ldy	#1		; 2, one iteration of ploop always consumed	

psync:	bit	tapein		; 4
	bmi	psync		; 3  [7 cycle loop]

	; consume up to 3x9 ploop iteration times (wasted time otherwise)
	bcc	plpm6		; 2(3), skip if byte not complete  [3+6=9 cycles]

	eor	chksum		; 3, incorporate byte
	sta	chksum		; 3, .. into the checksum
	lda	#1		; 2, reload sentinel bit

	iny			; 2, two iterations consumed
	inx			; 2 cycles
	bne	ploop		; 2(3)  [17 cycles]

	inc	store+2		; 6 cycles
	iny			; 2, three iterations consumed
	bne	ploop		; 3 (always)  [27 cycles]

plpm6:	nop			; 2, waste time for alignment
plpm4:	nop			; 2, waste time for alignment
	nop			; 2, waste time for alignment
ploop:	iny			; 2 cycles
	bit	tapein		; 4 cycles
	bpl	ploop		; 2 +1 if branch, +1 if in another page
				; total ~9 cycles

	cpy	#$40		; 2 cycles if Y - $40 > 0 endcode (770Hz)
	bpl	endcode		; 2(3)

	cpy	#$15		; 2 cycles if Y - $15 > 0 main (2000Hz)
	bpl	main		; 2(3)

	cpy	#$07		; 2, if Y<, then clear carry, if Y>= set carry
	rol			; 2, shift carry into A, shift sentinel out into carry
	bcc	nextbit		; 2(3), byte not complete

store:	sta	store+1,x	; 5, store data byte
	jmp	nextbit		; 3, next byte
				; [24 cycles]

endcode:  
	txa			; write end of file location + 1
	clc
	adc	store+1
	sta	store+1
	bcc	endcheck	; LSB didn't roll over to zero
	inc	store+2		; did roll over to zero, inc MSB
endcheck:			; check for match of expected length
	lda	ld_end
	cmp	store+1
	bne	error
	lda	ld_end+1
	cmp	store+2
	bne	error
sumcheck:
	lda	chksum
	bne	sumerror

	lda	inf_flag	; if inf_flag = 0 runit
	beq	runit
inf:
	jsr	crout
	lda	#<infm
	ldy	#>infm
	jsr	print

	lda	inf_src		;src lsb
	sta	inflate_zp+0
	lda	inf_src+1	;src msb
	sta	inflate_zp+1
	lda	inf_dst		;dst lsb
	sta	inflate_zp+2
	lda	inf_dst+1	;dst msb
	sta	inflate_zp+3

	jsr	inflate

; >>> Code above this point may be freely destroyed by inflate! <<<
afterinf:
	lda	inf_end		;dst end +1 lsb
	cmp	inflate_zp+2
	bne	error
	lda	inf_end+1	;dst end +1 msb
	cmp	inflate_zp+3
	bne	error
runit:
	lda	warm_flag	; if warm_flag = 1 warm boot
	bne	warmit
	jmp	(runcode)
warmit:
	jmp	warm		; run it
sumerror:
	jsr	crout
	lda	#<chkm
	ldy	#>chkm
	jsr	print
error:
	lda	#<errm
	ldy	#>errm
	jsr	print
	jmp	warm	
print:
	sta	pointer
	sty	pointer+1
	ldy	#0
	lda	(pointer),y		; load initial char
print1:	ora	#$80
	jsr	cout
	iny
	lda	(pointer),y
	bne	print1
	rts
chkm:	.asciiz	"CHKSUM "
errm:	.asciiz	"ERROR"
infm:	.asciiz	"INFLATING "
ld_beg:
	.org	*+2
ld_end:
	.org	*+2
inf_src:
	.org	*+2
runcode:
inf_dst:
	.org	*+2
inf_end:
	.org	*+2
inf_flag:
	.org	*+1
warm_flag:
	.org	*+1

end:

.assert	end <= $C000, warning, "fastload8000 object overruns I/O segment"
.assert	inflate_data + inflate_datalen < afterinf, warning, "inflate_data segment overruns end code"
