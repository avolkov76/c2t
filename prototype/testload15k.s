; testload15k.s
; Experimental cassette port transfer at 15.3Kbps
; Receives HGR screens ($2000 bytes) into hi-res screen 1

.include "apple2.inc"

lastin	=	LASTIN		; tapein last polarity
tapein	=	TAPEIN		; read tape interface
curval	=	$2E		; byte read from tape; reuses Mon MASK/CHKSUM/FORMAT scratch var

normc	=	$80		; normal char flag
invc	=	$40		; inverse char flag

chksum	=	$05		; XXX: for now; avoid Mon vars
prtptr	=	$06
target	=	$2000
datalen	=	$2000

defeot	=	18		; end of transmission sync wait time
syncval	=	$FF		; value used for auto-sync
calrng	=	24		; calibration branch range

origin	=	$800

	.org	origin

start:
	jsr	HOME		; clear screen

	lda	#0		; col 0
	sta	CH
	lda	#22		; row 22
	jsr	TABV		; move cursor
	lda	#<startm
	ldy	#>startm
	jsr	print

	lda	MIXSET		; set mixed mode
	lda	HIRES		; set hires gfx
	lda	LOWSCR		; set screen 1 ($2000)
	lda	GFXSET		; set gfx mode

loadgfx:
	lda	#$ff		; set initial
	sta	chksum		; .. checksum value

	lda	ld_beg		; setup ptr to target
	sta	store+1
	lda	ld_beg+1
	sta	store+2

	lda	#'.'+normc	; draw current state
	ldy	#0		; .. at col 0
	sta	(BASL),y	; .. current line

	lda	#2+13		; pos branch target ofs; +2 for neg BMI
	sta	dlyp+1		; patch pos branch target
	lda	#6		; neg branch target ofs; +1 for pos BPL
	sta	dlyn+1		; patch neg branch target

	lda	#7		; set col 7 for
	sta	CH		; .. offsets display
	lda	dlyp+1		; get pos branch offset
	jsr	PRBYTE		; print it
	inc	CH		; space between
	lda	dlyn+1		; get neg branch offset
	jsr	PRBYTE		; print it

	lda	#14		; offset all messages horizontally
	sta	CH		; .. to keep space for progress flags

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
				; [*] +X cycles
synoth:				; not a sync value
	cpx	#0		; 2 - saw enough sync values?
	bpl	synwait		; 3 (branch) nope, restart and keep waiting
				; [*] +44 cycles

	cmp	#$00		; 2 - data start?
	bne	syncerr		; 2 (no branch); or unrecognized byte; err
				; [*] +47 cycle exit

	lda	#'S'+normc	; 2 - draw current state
	ldy	#1		; 2 - .. at col 1
	sta	(BASL),y	; 6 - .. current line

	jmp	dsyndone	; 3 - go for data

syncerr:
	lda	#<syncm
	ldy	#>syncm
	jsr	print
	jmp	error

dsyndone:
	lda	#'D'+normc	; 2 - draw current state
	ldy	#3		; 2 - .. at col 3
	sta	(BASL),y	; 6 - .. current line

rddata:
	lda	#defeot		; 2 - normal end of transmission
	sta	readeot		; 4 - .. wait threshold

	ldx	#0		; 2 - init offset
dtloop:
	jsr	readbyte	; 6 - read byte from tape
	bcs	endcode		; 2 (no branch); or stop signal received

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

endcode:  
	txa			; write end of file location + 1
	clc
	adc	store+1
	sta	store+1
	bcc	endcheck
	inc	store+2
endcheck:
	lda	#'E' + normc	; draw current state
	ldy	#4		; .. at col 4
	sta	(BASL),y	; .. current line

	lda	ld_end
	cmp	store+1
	bne	error
	lda	ld_end+1
	cmp	store+2
	bne	error
sumcheck:
	lda	#<chkm
	ldy	#>chkm
	jsr	print

	lda	chksum
	bne	sumerror

	lda	#<okm
	ldy	#>okm
	jsr	print
again:
	lda	#100		; wait ~25 msecs
	jsr	WAIT		; .. for signal to go idle
	jmp	loadgfx		; and go again

sumerror:
	jsr	PRBYTE		; print checksum
	lda	#<errm
	ldy	#>errm
	jsr	print
	jmp	again

error:
	lda	#<errm
	ldy	#>errm
	jsr	print
	jmp	MONZ

print:
	sta	prtptr
	sty	prtptr+1
	ldy	#0
	lda	(prtptr),y		; load initial char
print1:	ora	#$80
	jsr	COUT
	iny
	lda	(prtptr),y
	bne	print1
	rts

chkm:	.asciiz	"CHKSUM "
okm:	.byte	"OK", $0D, 0
syncm:	.asciiz	"SYNC"
errm:	.byte	" ERROR", $0D, 0
startm:	.byte	"START 15K TEST", $0D, 0

ld_beg:	.word	(target)
ld_end:	.word	(target + datalen + 1)	; +1 for checksum

;.res	(origin + $200 - *)		; pad to $200

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
;	lda	tapein		; 4 - read tape port
;	bmi	waitnbr		; 2 (no branch)
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
;	lda	tapein		; 4 - read tape port
;	bpl	waitpbr		; 2 (no branch)
waitpl:
	iny			; 2 - measure the delay
	lda	tapein		; 4 - read tape port
	bmi	waitpl		; 3 (branch) wait for polarity change
				; [*] 9 cycle wait
waitpbr:			; unrolled loop exit target
				; [!] 2 cycles shorter than waitn (add'l no-branch)
waitend:
	; XXX: useful during development to see the actual waits
;savey:	sty	$A000		; 3 - save wait stats (circular buf)
;	inc	savey+1		; 6 - advance stat index

	cpy	readeot		; 4 - end of transmission wait?
	bcs	readexit	; 2 (fall through); or exit when wait >= threshold

	tay			; 2 - stash sync polarity
	lda	#$01		; 2 - prepare the sentinel bit
	sta	curval		; 3 - .. for the 8-bit loop

	; NB.1: neg transition is usually detected earlier due to 741-R20-LS251 combined bias,
	;  so the delay is generally ~8-16 cycles longer for the neg side.

	tya			; 2 - unstash sync polarity
				; [*] +22 cycles from sync detection
dlyp:	bpl	dlytbl+13	; 3 (branch) / 2 (no branch) variable delay for pos [patched]
dlyn:	bmi	dlytbl+6	; 3 (branch) variable delay for neg [patched]
				; NB: calibration branch target table; NOP is 1-byte opcode $EA
dlytbl:	.res	calrng-13, $EA	; NOP opcodes, 2 cycles each
				; 12 NOPs below are also part of the branch table

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
	nop			; 2 - wait [-12]
	nop			; 2 - wait [-14]
	nop			; 2 - wait [-16]
	nop			; 2 - wait [-18]
	nop			; 2 - wait [-20]
	nop			; 2 - wait [-22]
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

.res	(origin + $200 - *)		; pad to $200
;.res	(origin + $300 - *)		; pad to $300
