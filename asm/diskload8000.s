;diskload8000.s

.include "apple2.inc"
.include "diskload1.inc"
.include "diskload2.inc"
.include "diskload3.inc"
.include "dosrwts.inc"

cout	=	COUT		; character out sub
crout	=	CROUT		; CR out sub
prbyte	=	PRBYTE 		; print byte in hex
tapein	=	TAPEIN		; read tape interface
warm	=	MONZ		; back to monitor
clear	=	CR		; clear screen
endbas	=	$80C

; zero page parameters

begload	=	diskload1_zp+0	; begin load location LSB/MSB
endload	=	diskload1_zp+2	; end load location LSB/MSB
chksum	=	diskload1_zp+4	; checksum location
pointer	=	$0C		; LSB/MSB pointer

start:
        .org	endbas
move:				; end of BASIC, move code to readtape addr
	ldx	#0
move1:
	lda	moved,x
        sta	readtape,x
;	lda	moved+256,x
;	sta	readtape+256,x
	inx
	bne	move1
phase1:
	jsr	crout		; print LOADING...
	lda	#<loadm
	ldy	#>loadm
	jsr	print
				; diskload2 ORG
	lda	#<diskload2_org	; store begin location LSB
	sta	begload
	lda	#>diskload2_org	; store begin location MSB
	sta	begload+1
				; end of DOS + 1 for comparison
	lda	#<(dosrwts_end+1)  ; store end location LSB
	sta	endload
	lda	#>(dosrwts_end+1)  ; store end location MSB
	sta	endload+1

	jsr	readtape	; get the code
	jmp	diskload2	; run it
loadm:
	.byte	"LOADING INSTA-DISK, ETA "
loadsec:			; 10 bytes for "XX SEC. ",$00
	.byte	0,0,0,0,0,0,0,0,0,0
moved:
	.org	diskload1_org	; $9000 for now
readtape:
	lda	begload		; load begin LSB location
	sta	store+1		; store it
	lda	begload+1	; load begin MSB location
	sta	store+2		; store it

	lda	#$ff		; initial
	sta	chksum		; .. checksum value

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
okm:	.asciiz	"OK"
errm:	.asciiz	"ERROR"
end:

.assert	* <= inflate_data, warning, "diskload1 too large; overruns inflate data segment"
