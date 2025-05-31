; diskload15k.s

.include "apple2.inc"
.include "diskload1.inc"
.include "diskload2.inc"
.include "diskload3.inc"
.include "dosrwts.inc"

tapein	=	TAPEIN		; read tape interface
warm	=	MONZ		; back to monitor
lastin	=	LASTIN		; tapein last polarity (overloaded by DOS RWTSRDVOLN)
curval	=	$2E		; byte read from tape; reuses Mon MASK/CHKSUM/FORMAT scratch var
endbas	=	$80C

; zero page parameters

begload	=	diskload1_zp+0	; begin load location LSB/MSB
endload	=	diskload1_zp+2	; end load location LSB/MSB
chksum	=	diskload1_zp+4	; checksum location
pointer	=	$0C		; LSB/MSB pointer

; 15k constants
defeot	=	18		; end of transmission sync wait time
syncval	=	$FF		; value used for auto-sync
calrng	=	14		; calibration branch range

start:
        .org	endbas
move:				; end of BASIC, move code to readtape addr
	ldx	#0
move1:
	lda	moved,x
        sta	readtape,x
	lda	moved+256,x	; 2 pages to move
	sta	readtape+256,x
	inx
	bne	move1
phase1:
	jsr	CROUT		; print LOADING...
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

	; this will cause a range error if diskload1_msg is incorrectly positioned
	.res	diskload1_msg-*, 0	; align to fixed start of loadm
loadm:	
	.asciiz	"LOADING..."		; overwritten by audio builder
	.res	diskload1_mlen-11, 0	; reserved message space

moved:
	.org	diskload1_org	; $9000 for now
readtape:
	lda	begload		; load begin LSB location
	sta	store+1		; store it
	lda	begload+1	; load begin MSB location
	sta	store+2		; store it

	lda	#$ff		; init checksum
	sta	chksum

	; signal start (auto-sync) detection
	; will eat input priming cycles
	lda	#145		; wait up to 2.5 bytes (25*2.5*21/9)
	sta	readeot		; .. for sync signal
synwait:
	ldx	#4-1		; 4 sync bytes minimum
synloop:
	jsr	readbyte	; 6 - read byte from tape
				; NB: no stop signal detection until we are synch'ed
	lda	curval		; 3 - the value we read
	cmp	#syncval	; 2 - sync value?
	bne	synoth		; 3 (branch) nope, another value
	dex			; 2 - got one more good sync byte
	bcs	synloop		; 3 (branch; carry=1 by CMP) keep going until signal
synoth:				; not a sync value
	cpx	#0		; 2 - saw enough sync values?
	bpl	synwait		; 3 (branch) nope, restart and keep waiting
				; [*] +44 cycles

	cmp	#$00		; 2 - data start?
	beq	rddata		; 3 (branch); or unrecognized byte; err
				; [*] +47 cycle exit
	jmp	syncerr

rddata:
	lda	#defeot		; 2 - normal end of transmission
	sta	readeot		; 4 - .. wait threshold

	ldx	#0		; 2 - init offset
dtloop:
	jsr	readbyte	; 6 - read byte from tape
	bcs	rddatend	; 2 (no branch); or stop signal received

	lda	curval		; 3 - the value we read
store:	sta	store,x		; 5 - store data byte

	eor	chksum		; 3 - add data byte to
	sta	chksum		; 3 - .. the checksum

	inx			; 2 - got one more byte
	bne	dtloop		; 3 (branch) more to go
				; [*] +52 cycles

	inc	store+2		; 6 - increment data page
	bne	dtloop		; 3 (branch) loop
				; [*] +60 cycles
rddatend:
	jmp	endcode		; go check data (not reachable by branch)

readeot:			; end of transmission threshold
	.byte	defeot		; default is normal eot wait

readbyte:			; [*] +66 cycles long way
;	ldy	#0		; 2 - sync delay measurement; plain 9-cycle detection loop
	ldy	#5		; 2 - sync delay measurement; 5 iterations unrolled

	bit	lastin		; 3 - check last polarity ($80 -> N)
	bmi	waitp		; 3 (branch) / 2 (no branch)
				; [*] +74 cycles long way [safe: 21*4=84]

	; NB: In testing on UA741 with 9-cycle wait loops, the typical executions were:
	;   * waitn 2-3 iterations
	;   * waitp 4-5 iterations
	;  -- a difference of ~16 cpu cycles.
	; The waitn/waitp loops are unrolled for the most time-critical parts: the first 6*8=48 cpu cycles.
	; This buys us another +/-2 cycles of sync precision. Beyond that, the timing is relaxed.

waitn:
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bmi	waitnbr		; 2 (no branch)
waitnl:
	iny			; 2 - measure the delay
	lda	tapein		; 4 - read tape port
	bpl	waitnl		; 3 (branch) wait for polarity change
				; [*] 9 cycle wait
waitnbr:			; unrolled loop exit target
	bmi	waitend		; 3 (branch)

waitp:	
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
	lda	tapein		; 4 - read tape port
	bpl	waitpbr		; 2 (no branch)
waitpl:
	iny			; 2 - measure the delay
	lda	tapein		; 4 - read tape port
	bmi	waitpl		; 3 (branch) wait for polarity change
				; [*] 9 cycle wait
waitpbr:			; unrolled loop exit target
				; [!] 2 cycles shorter than waitn (add'l no-branch)
waitend:
	cpy	readeot		; 4 - end of transmission wait?
	bcs	readexit	; 2 (fall through); or exit when wait >= threshold

	tay			; 2 - stash sync polarity
	lda	#$01		; 2 - prepare the sentinel bit
	sta	curval		; 3 - .. for the 8-bit loop

	; NB.1: neg transition is usually detected earlier due to 741-R20-LS251 combined bias,
	;  so the delay is generally ~8-16 cycles longer for the neg side.

	tya			; 2 - unstash sync polarity
				; [*] +22 cycles from sync detection
	; NB.2: branches here are calibrated experimentally for the center of the worst case tested:
	;    enhanced IIe with UA741TC (1982 KOREA) variant, Rockwell R65C02P4 (2019), and sketchy PSU.
	;    The worst case has +/-8 cycle margins from center. Other test cases have wider margins (+/-12)
	;    and offset centers but all are supersets of the worst case comfortably.
dlyp:	bpl	dlytbl+9	; 3 (branch) / 2 (no branch) variable delay for pos [calibrated]
dlyn:	bmi	dlytbl+2	; 3 (branch) variable delay for neg [calibrated]
				; NB: calibration branch target table; NOP is 1-byte opcode $EA
dlytbl:	.res	calrng-7, $EA	; NOP opcodes, 2 cycles each
				; 6 NOPs below are also part of the branch table

	; A bit in the audio stream is ~53.15 cpu cycles long
	; The read loop is 53 cycles, and accumulates a 1.05 cycle error over 8 bits.
	; The waits are calibrated naturally for the center, halving the error to +/-0.53 cycles.
readbit0:			;
	nop			; 2 - wait [ 0]
	nop			; 2 - wait [-2]
	nop			; 2 - wait [-4]
	nop			; 2 - wait [-6]
	nop			; 2 - wait [-8]
	nop			; 2 - wait [-10]
	jsr	readend		; 12 - waste time [-12]
dlyend:	jsr	readend		; 12 - waste time [-24]
				; [*] 36 cycle delay
	lda	tapein		; 4 - read data bit
	eor	lastin		; 3 - adjust bit polarity
	asl	a		; 2 - bit ->> carry
	rol	curval		; 5 - data <<- bit, sentinel ->> carry
	bcc	readbit0	; 3 (branch) - until 8 bits done
				; [*] 53 cycle loop
				; [*] 16 cycle exit

	clc			; 2 - valid read
readexit:
	lda	tapein		; 4 - remember the
	sta	lastin		; 3 - .. last polarity
readend:
	rts			; 6 - return
				; [*] 31 cycle exit

.assert	>(readend) = >(readbyte), warning, "readbyte routine should not cross page boundaries"

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
	lda	#<chkm
	ldy	#>chkm
	jsr	print
	lda	chksum
	bne	error
	jmp	ok		; return to caller

syncerr:
	lda	#<syncm
	ldy	#>syncm
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
	jsr	COUT
	iny
	lda	(pointer),y
	bne	print1
	rts

syncm:	.byte	$0D, "SYNC ", 0
chkm:	.byte	$0D, "CHKSUM ", 0
okm:	.asciiz	"OK"
errm:	.asciiz	"ERROR"

end:

.assert	* <= inflate_data, warning, "diskload1 too large; overruns inflate data segment"
