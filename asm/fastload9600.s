;fastload9600.s

org	=	$BE80		; should be $BE80
cout	=	$FDED		; character out sub
crout	=	$FD8E		; CR out sub
prbyte	=	$FDDA 
warm	=	$FF69		; back to monitor
tapein	=	$C060
pointer	=	$06
endbas	=	$80C
;target	=	$1000
target	=	$801
chksum	=	$00
inflate	=	$BA00
inf_zp	=	$0

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
	jmp	fast
moved:
	.org	org
fast:
	lda	#<loadm
	ldy	#>loadm
	jsr	print

	lda	#$ff
	sta	chksum

	lda	ld_beg		; setup point to target
	sta	store+1
	lda	ld_beg+1
	sta	store+2

wait:	bit	tapein
	bpl	wait
waithi:	bit	tapein		; Wait for input to go high.
	bmi	waithi
pre:	lda	#1		; Load sentinel bit
	ldx	#0		; Clear data index
	clc			; Clear carry (byte complete flag)
data:	bcc	waitlo		; Skip if byte not complete
store:	sta	store,x		; Store data byte

	eor	chksum
	sta	chksum

	lda	#1		; Re-load sentinel bit
waitlo:	bit	tapein		; 4 - Wait for input to go low
	bpl	waitlo		; 2 (fall through); 3 (for branch)
	nop			; 2 - waste time for a cleaner 0/1 break
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

poll13:	bit	tapein
	bpl	zero		; ** no man's land ** (13-18 cycles)
poll19:	bit	tapein		
	bpl	zero		; ** no man's land ** (19-24 cycles)
poll25:	bit	tapein
	bpl	zero		; ** no man's land ** (25-30 cycles)
poll31:	bit	tapein
	bpl	zero		; ** no man's land ** (31-36 cycles)
	bit	tapein
	bpl	zero		; zero bit (37-42 cycles)
	bit	tapein
	bpl	zero		; zero bit (43-48 cycles)
	bit	tapein
	bpl	zero		; zero bit (49-54 cycles)
	bit	tapein
	bpl	zero		; zero bit (55-60 cycles)
	bit	tapein
	bpl	zero		; zero bit (61-66 cycles)
	bit	tapein
	; NB: This is the 0/1 point break: 67 cycles with a +/-4 margin. The margin is slim!
	;   The point is centered by two NOPs after waitlo loop.
	;   Add/remove the NOPs above (+/-2) to experiment.
	bpl	one		; one bit (67-72 cycles)
	bit	tapein
	bpl	one		; one bit (73-78 cycles)
	bit	tapein
	bpl	one		; one bit (79-84 cycles)
	bit	tapein
	bpl	one		; ** no man's land ** (85-90 cycles); closer to bit 1 than to pre
	bit	tapein
	bpl	pre		; pre pulse (91-96 cycles)
	bit	tapein
	bpl	pre		; pre pulse (97-102 cycles)
	bit	tapein
	bpl	pre		; pre pulse (103-108 cycles)

endcode:  
	txa			; write end of file location + 1
	clc
	adc	store+1
	sta	store+1
	bcc	endcheck
	inc	store+2
endcheck:
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
	sta	inf_zp+0
	lda	inf_src+1	;src msb
	sta	inf_zp+1
	lda	inf_dst		;dst lsb
	sta	inf_zp+2
	lda	inf_dst+1	;dst msb
	sta	inf_zp+3

	jsr	inflate

	lda	inf_end		;dst end +1 lsb
	cmp	inf_zp+2
	bne	error
	lda	inf_end+1	;dst end +1 msb
	cmp	inf_zp+3
	bne	error
runit:
	lda	warm_flag	; if warm_flag = 1 warm boot
	bne	warmit
	jmp	(runcode)
warmit:
	jmp	warm		; run it
;rangerr:
;	jsr	crout
;	lda	#<rngm
;	ldy	#>rngm
;	jmp	prterr
sumerror:
	jsr	crout
	lda	#<chkm
	ldy	#>chkm
prterr:
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
;rngm:	.asciiz	"RG "
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
loadm:	
	;.asciiz	"LOADING "

end:

