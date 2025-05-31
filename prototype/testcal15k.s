; testcal15k.s
; Experimental cassette port transfer at 15.3Kbps
; Receives HGR screens ($2000 bytes) into hi-res screen 1
; Auto-calibration test variant

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
calval	=	$55		; value used for calibration
calspo	=	7		; calibration samples per offset
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

	lda	#0		; reset calib data
	ldx	#(calcnt-caltbl)	; cal bytes to clear
clrcal:
	sta	caltbl,x	; clear cal byte
	dex			; next
	bpl	clrcal		; until X < 0

	lda	#'.'+normc	; draw current state
	ldy	#0		; .. at col 0
	sta	(BASL),y	; .. current line

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
				; [*] +43 cycle exit

	lda	#'S'+normc	; 2 - draw current state
	ldy	#1		; 2 - .. at col 1
	sta	(BASL),y	; 6 - .. current line

	lda	skipcal		; 4 - check skip flag
	beq	calib		; 3 (branch) no flag, go calibrate
	jmp	dsyndone	; 3 - skip calibration, go for data

calib:
	lda	#defeot+4	; 2 - extended sync detection
	sta	readeot		; 4 - .. wait threshold

	ldx	#0		; 2 - reset the
	stx	readly+0	; 4 - .. pos delay offset
	stx	readly+1	; 4 - .. neg delay offset
				; [*] +76 cycles from sync
calnext:
	lda	#calspo*2	; 2 - pos + neg samples
	sta	calcnt		; 4 - preset counter
caloop:				;
	; NB: during calibration we venture into timing areas where the last polarity
	;   remembered by readbyte will be wrong. Need to reset it now.
	lda	tapein		; 4 - get current polarity
	sta	lastin		; 3 - .. and reset it for readbyte
	sta	calpol		; 4 - .. and save for later
				; [!] +93 cycles from sync
				; [*] +101 cycles long way
	jsr	readbyte	; 6 - read byte from tape
	bcs	calerr		; 2 (no branch) read ok; or loss of sync?

	lda	curval		; 3 - the value we read
	cmp	#calval		; 2 - good cal value?
	bne	cabadval	; 3 (branch) bad value, wait then keep going
				; NB: waits are needed to keep timing aligned
	
	lda	#$01		; 2 - speculative pos increment
	bit	calpol		; 4 - get previous polarity
	bmi	skipneg		; 3 (branch) go handle as pos (polarity reverses)
	lda	#$10		; 2 - neg increment
skipneg:
	clc			; 2 - prep for add
	adc	caltbl,x	; 4 - A=caltbl[x] + pos/neg increment
	sta	caltbl,x	; 5 - caltbl[x] += pos/neg increment
skipinc:
	dec	calcnt		; 6 - counter
	bne	caloop		; 2 (no branch); or more to go for this offset

	inx			; 2 - bump current offset
	stx	readly+0	; 4 - .. pos offset
	stx	readly+1	; 4 - .. neg offset
	cpx	#calrng		; 2 - all done?
	bcc	calnext		; 3 (branch) until entire range covered
				; [*] +X cycles
	bcs	calcalc		; (always) done collecting, go for calcs

	; NB: this wait is needed to keep timing aligned with skipped processing
	;   and keep signal polarity predictable.
cabadval:			; 21 cycle wait to align processing
	jsr	calwait12	; 12 - waste time
	nop			; 2 - waste time
	nop			; 2 - waste time
	clc			; 2 - prep the branch
	bcc	skipinc		; 3 - return
calwait12:
	rts

calerr:
	lda	#<calibm
	ldy	#>calibm
	jsr	print
	jmp	error

	; scan the calibration table and calculate center points for branch offsets
	; ~800*2=1600 cycles
calcalc:
	ldy	#1		; polarity index; 1 for neg, 0 for pos
calcloop:
	ldx	#0		; start of table
callolp:			; find the first complete offset
	lda	caltbl,x	; get cal count
	clc			; continuity test
	adc	caltbl+1,x	; add next cal count
	and	calmask,y	; .. for polarity
	cmp	calgcnt,y	; good count?
	bcs	calstlo		; yes, >= calspo; go check and save it
	inx			; next offset
	cpx	#calrng-1	; reached the end?
	bcc	callolp		; more to go
	bcs	calerr		; reached the end and found nothing
calstlo:
	txa			; need to save Y-indexed
	sta	callofs,y	; save low offset
	inx			; next offset
calhilp:			; find the last complete offset
	lda	caltbl,x	; get cal count
	clc			; continuity test
	adc	caltbl-1,x	; add prev cal count
	and	calmask,y	; .. for polarity
	cmp	calgcnt,y	; good count?
	bcc	calnoshi	; no, < calspo; skip save
	txa			; need to save Y-indexed
	sta	calhofs,y	; save high offset
calnoshi:
	inx			; next offset
	cpx	#calrng		; reached the end?
	bcc	calhilp		; more to go
	
	; calculate center point
	lda	calhofs,y	; A=high offset
	beq	calerr		; never found the end -- err
	sec			; ceiling bias
	adc	callofs,y	; A = high-ofs + low-ofs + 1
	lsr	a		; A = A/2
	sta	readly,y	; set branch offset (pos or neg)
	
	dey			; next polarity
	bpl	calcloop	; until Y < 0

caldone:
;	jmp	MONZ		; XXX: for dev/test
	; XXX: override for limits testing
;	lda	#16		; hard-coded delay
;	sta	readly+0	; .. branch offsets
;	lda	#8		;
;	sta	readly+1	; 

	lda	#7		; set col 7 for
	sta	CH		; .. offsets display
	lda	readly+0	; get pos branch offset
	jsr	PRBYTE		; print it
	inc	CH		; space between
	lda	readly+1	; get neg branch offset
	jsr	PRBYTE		; print it

	lda	#14		; restore col 14 for
	sta	CH		; .. other messages

	lda	#'C'+normc	; 2 - draw current state
	ldy	#2		; 2 - .. at col 2
	sta	(BASL),y	; 6 - .. current line

	jmp	dsync		; go for data sync

caltbl:	.res	calrng, 0	; calibration table; pos | (neg << 4)
calpol:	.byte	0		; polarity
calcnt:	.byte	0		; remaining sample count for offset
calmask:			; masks for caltbl values; pos, neg
	.byte	$0F, $F0
calgcnt:			; expected counts wrt/ calmask; pos, neg
	.byte	2*calspo, $10*2*calspo
;	.byte	2*calspo-1, $10*2*calspo-$10	; -1 for softer margins
callofs:
	.byte	0, 0		; calibration low offsets; pos, neg
calhofs:
	.byte	0, 0		; calibration high offsets; pos, neg
skipcal:
	.byte	0		; skip calibration flag

dsynerr:
	lda	#<syncm
	ldy	#>syncm
	jsr	print
	jmp	error

dsync:
	lda	#145		; wait up to 2.5 bytes (25*2.5*21/9)
	sta	readeot		; .. for sync signal
dsynwait:
	ldx	#3-1		; 2 - 3 sync bytes minimum
dsynloop:
	jsr	readbyte	; 6 - read byte from tape
	bcs	dsynerr		; 2 (no branch); or loss of sync?
	
	lda	curval		; 3 - the value we read
	cmp	#syncval	; 2 - sync value?
	bne	dsynoth		; 3 (branch) another value?
	dex			; 2 - got one more good sync byte
	bcs	dsynloop	; 3 (branch; carry=1 by CMP) keep going until data signal

dsynoth:			; not a sync value
	cpx	#0		; 2 - saw enough sync values?
	bpl	dsynwait	; 3 (branch) nope, restart and keep waiting

	cmp	#$00		; 2 - data start?
	bne	dsynerr		; 2 (no branch); or unrecognized byte; err
				; [*] +X cycles

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
calibm:	.asciiz	"CALIB"
errm:	.byte	" ERROR", $0D, 0
startm:	.byte	"START 15K TEST", $0D, 0

ld_beg:	.word	(target)
ld_end:	.word	(target + datalen + 1)	; +1 for checksum

;.res	(origin + $200 - *)		; pad to $200

readly:				; bit start timing branch offsets
	.byte	16, 8		; defaults; pos, neg
readeot:			; end of transmission threshold
	.byte	defeot		; default is normal eot wait

readbyte:			; [*] +66 cycles long way
	ldy	#0		; 2 - delay measurement

	bit	lastin		; 3 - check last polarity ($80 -> N)
	bmi	waitp		; 3 (branch) / 2 (no branch)
				; [*] +74 cycles long way [safe: 21*4=84]

	; NB: In testing on UA741 with 9-cycle wait loops, the typical executions were:
	;   * waitn 2-3 iterations
	;   * waitp 4-5 iterations
	;  -- a difference of ~16 cpu cycles.

waitn:	
	iny			; 2 - measure the delay
	lda	tapein		; 4 - read tape port
	bpl	waitn		; 3 (branch) wait for polarity change
				; [*] 9 cycle wait
	lda	readly+1	; 4 - get neg delay
	jmp	waitend		; 3 - continue

waitp:	
	iny			; 2 - measure the delay
	lda	tapein		; 4 - read tape port
	bmi	waitp		; 3 (branch) wait for polarity change
				; [*] 9 cycle wait
				; [!] 2 cycles shorter than waitn (add'l no-branch)
	lda	readly+0	; 4 - get pos delay

waitend:
	sta	dlybrn+1	; 4 - patch branch offset

	; XXX: useful during development to see the actual waits
;savey:	sty	$A000		; 3 - save wait stats (circular buf)
;	inc	savey+1		; 6 - advance stat index

	cpy	readeot		; 4 - end of transmission wait?
	bcs	readexit	; 2 (fall through); or exit when wait >= threshold

	lda	#$01		; 2 - prepare the sentinel bit
	sta	curval		; 3 - .. for the 8-bit loop

	; NB.1: neg transition is usually detected earlier due to 741-R20-LS251 combined bias,
	;  so the delay is generally ~8-16 cycles longer for the neg side.
				; [*] +25 cycles from sync detection
dlybrn:	bne	dlytbl		; 3 (always) variable delay for pos/neg [patched]
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

.res	(origin + $300 - *)		; pad to $300
