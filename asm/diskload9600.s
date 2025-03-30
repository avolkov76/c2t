;diskload9600.s

org	=	$9000		; should be $9000
cout	=	$FDED		; character out sub
crout	=	$FD8E		; CR out sub
prbyte	=	$FDDA 		; print byte in hex
tapein	=	$C060		; read tape interface
warm	=	$FF69		; back to monitor
clear	=	$FC58		; clear screen
endbas	=	$80C
target	=	$1000

; zero page parameters

begload	=	$00		; begin load location LSB/MSB
endload	=	$02		; end load location LSB/MSB
chksum	=	$04		; checksum location
pointer	=	$0C		; LSB/MSB pointer

start:
        .org	endbas
move:				; end of BASIC, move code to readtape addr
	ldx	#0
move1:
	lda	moved,x
        sta	readtape,x
	lda	moved+256,x
	sta	readtape+256,x
	inx
	bne	move1
phase1:
	jsr	crout		; print LOADING...
	lda	#<loadm
	ldy	#>loadm
	jsr	print
				; diskload2 ORG
	lda	#$D0		; store begin location LSB
	sta	begload
	lda	#$96		; store begin location MSB
	sta	begload+1
				; end of DOS + 1 for comparison
	lda	#$00		; store end location LSB
	sta	endload
	lda	#$C0		; store end location MSB
	sta	endload+1

	jsr	readtape	; get the code
	jmp	$9700		; run it
loadm:
	.byte	"LOADING INSTA-DISK, ETA "
loadsec:			; 10 bytes for "XX SEC. ",$00
	.byte	0,0,0,0,0,0,0,0,0,0
moved:
	.org	org		; $9000
readtape:
	lda	begload		; load begin LSB location
	sta	store+1		; store it
	lda	begload+1	; load begin MSB location
	sta	store+2		; store it

	lda	#$ff		; init checksum
	sta	chksum

wait:	bit	tapein
	bpl	wait
waithi:	bit	tapein		; Wait for input to go high.
	bmi	waithi
pre:	lda	#1		; Load sentinel bit
	ldx	#0		; Clear data index
	clc			; Clear carry (byte complete flag)
data:	bcc	waitlo		; Skip if byte not complete
store:	sta	target,x	; Store data byte

	eor	chksum		; compute checksum
	sta	chksum		; store checksum

	lda	#1		; Re-load sentinel bit
waitlo:	bit	tapein		; 4 - Wait for input to go low
	bpl	waitlo		; 2 (fall through); 3 (for branch)
	nop			; 2 - waste time (see point break below)
	dec	waste		; 6 - waste time (see point break below)
	nop			; 2 - waste time (see point break below)
	bcc	poll13		; 3 (branch) - Poll at +13 cycles if no store; 2 (fall through) if store
	inx			; 2 - Stored, so increment data index
	nop			; 2 - waste time for 6-cycle alignment
	bne	poll19		; 3 (branch) - Poll at +19 cycles if no carry; 2 (fall through) if next page
	nop			; 2 - waste time for 6-cycle alignment
	nop			; 2 - waste time for 6-cycle alignment
	inc	store+2		; 6 - Increment data page
	bne	poll31		; 3 - (branch always) poll at +31 cycles

one:	sec			; one bit detected
	rol			;  shift it into A
	jmp	data		;   and go handle data (C = sentinel)

zero:	clc			; zero bit detected
	rol			;  shift it into A
	jmp	data		;   and go handle data (C = sentinel)

poll13:	dec	waste		; 6 - waste time; ** no man's land ** (13-18 cycles)
poll19:	dec	waste		; 6 - waste time; ** no man's land ** (19-24 cycles)
poll25:	dec	waste		; 6 - waste time; ** no man's land ** (25-30 cycles)
poll31:	dec	waste		; 6 - waste time; ** no man's land ** (31-36 cycles)
	bit	tapein
	bpl	rng0err		; ** no man's land ** (0-42 cycles)
	dec	waste		; 6 - waste time; zero bit (43-48 cycles); ok to wait
	bit	tapein
	bpl	zero		; zero bit (49-54 cycles)
	bit	tapein
	bpl	zero		; zero bit (55-60 cycles)
	bit	tapein
	bpl	zero		; zero bit (61-66 cycles)
	; NB: This is the 0/1 point break: 67 cycles with a +/-4 margin. The margin is slim!
	;   The point is centered by NOPs after waitlo loop.
	;   Add/remove the NOPs above (+/-2) to experiment.
	; XXX: currently, the offset is +10 cycles
	dec	waste		; 6 - waste time; one bit (67-72 cycles); ok to wait
	bit	tapein
	bpl	one		; one bit (73-78 cycles)
	bit	tapein
	bpl	one		; one bit (79-84 cycles)
	bit	tapein
	bpl	one		; ** no man's land ** (85-90 cycles), closer to bit 1
	dec	waste		; 6 - waste time; (91-96); ok to wait
	bit	tapein
	bpl	pre		; pre pulse (91-102)
	bit	tapein
	bpl	pre		; pre pulse (103-108 cycles)
	bit	tapein
	bpl	rngperr		; ** no man's land ** (109-130 cycles)
	bmi	endcode		; (branch always) low freq signals end of data

rngperr:
	lda	#'P'
	bne	rngkind
rng0err:
	lda	#'0'
rngkind:	
	sta	rngm+2
rangerr:
	jsr	crout
	lda	#<rngm
	ldy	#>rngm
	jmp	prterr

endcode:  
	txa			; write end of file location + 1
	clc
	adc	store+1
	sta	store+1
	bcc	endcheck	; LSB didn't roll over to zero
	inc	store+2		; did roll over to zero, inc MSB
endcheck:			; check for match of expected length
	lda	endload
	cmp	store+1
	bne	error
	lda	endload+1
	cmp	store+2
	bne	error
	jsr	ok
sumcheck:
	jsr	crout
	lda	#<chkm
	ldy	#>chkm
	jsr	print
	lda	chksum
	bne	error
	jmp	ok		; return to caller
prterr:
	jsr	print
error:
	lda	#<errm
	ldy	#>errm
	jsr	print
	jmp	warm	
ok:
	lda	#<okm
	ldy	#>okm
print:
	sta	pointer
	sty	pointer+1
	ldy	#0
	lda	(pointer),y	; load initial char
print1:	ora	#$80
	jsr	cout
	iny
	lda	(pointer),y
	bne	print1
	rts

chkm:	.asciiz	"CHKSUM "
rngm:	.asciiz	"RG  "
okm:	.asciiz	"OK"
errm:	.asciiz	"ERROR"
waste:	.byte	0
end:
