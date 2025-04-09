; formrite.s
; Format tracks with final data in one pass.
; Much of this code is adapted from DOS 3.3 RWTS.

; zero page parameters
inptr	=	frtinp		; input pointer (reuses diskload1 space)
outptr	=	frtoutp		; output pointer (reuses diskload1 space)

hibptr	=	RWTSTMP3	; HI nibble output ptr (work register; reuses DOS scratch regs)
lobptr	=	RWTSDCTPTR	; LO nibble output ptr (work register; reuses DOS DCT ptr)
tmpreg1	=	RWTSTMP1	; scratch register 1 (reuses DOS scratch reg)
tmpreg2	=	RWTSTMP2	; scratch register 2 (reuses DOS scratch reg)

rwtsiobp=	RWTSIOBPTR	; RWTS standard IOB pointer; reused
;isecpad=	RWTSSECPAD	; Inter-sector sync padding size while RWTS is formatting (aka gap 3)
				; NB: Padding is (mostly) determined during track 0 format and
				;   must be preserved track to track. It *might* be subsequently
				;   reduced by other tracks (i.e. drive speed changes).

; other vars
lonbuf	=	DOSNIBLOBUF	; LO nibble encoding space (RWTS secondary buffer; safe to use)


; * Nibblize a track worth of data. Converts 16 secors to encoded
; *   HI/LO nibble buffers semi-ready for writing to disk.
; * HI buffers: outptr, $1000 bytes (16 sectors, $100 each)
; * LO buffers: outptr + $1000, $800 bytes (16 sectors, $80 each)
; * In: inptr=data pointer; outptr=HI/LO buffers pointer
; * Out: nothing
; * Destroys: A,X,Y,flags,inptr
.proc	nibbtrack
	lda	#0
	sta	hibptr		; buffer ptr LSB (assume page-aligned)
	sta	tmpreg1		; encode sectors 0-15

secloop:
	tax			; sector index
	lda	DOSINTLTBL,x	; xlat logical sector# to disk sector
	sta	tmpreg2		; disk sector#
	clc			; hibptr = outptr + sector# * $100
	adc	outptr+1	; calc HI buf output MSB
	sta	hibptr+1	; store HI buf output MSB

	lda	#2		; lobptr = outptr + $1000 + sector# * $80 + 1
				; NB: +1 so that disk writing code does not cross page boundaries
	sta	lobptr		; 2 -> LO buffer ptr LSB (will be shifted by 1)
	lda	tmpreg2		; disk sector#
	lsr	a		; A=sector# / 2; odd sector# -> carry
	ror	lobptr		; odd sector# -> $80 LSB; carry=0 after
	adc	#16		; A = $10 (LO offset) + sector# / 2
	adc	outptr+1	; A = outptr MSB + $10 + sector# / 2
	sta	lobptr+1	; store LO buf output MSB

	jsr	nibbsector	; nibblize sector

	inc	inptr+1		; next sector page
	inc	tmpreg1
	lda	tmpreg1
	cmp	#16		; 0-15 done?
	bcc	secloop		; ... more to go
	rts
.endproc

; * Nibblize sector routine. Converts 256 bytes of data to encoded 6-bit
; *   HI/LO nibble buffers semi-ready for writing to disk.
; * In: inptr=data; hibptr=HI buffer pointer; lobptr=LO buffer pointer
; * Out: nothing
; * Destroys: A,X,Y,flags,RWTS LO buffer
.proc	nibbsector
	; split 256 data bytes into HI bits ($100 bytes) and LO bits ($56 bytes)
	; two low bits are shifted out from each of the data bytes and shifted into
	; the circular LO buffer.
	; resulting data is nibblized 6 bits per byte ($00..3F)
	ldx	#0		; secondary buffer index
	ldy	#2		; user data index. start with data byte	$01 (y immediately decremented).
				; mind-bending trick: $56*3=102, starving the secondary	buffer by 2*2 bits.
				; to use all of	the secondary buffer bits, and make disk nybble encoding deterministic
				; (one to one with data), lower	bit pairs of data bytes	$00.01 are shifted into	secondary buffer twice.
				; the first of the two shifted bit pairs is never used when reading.
splitloop:
	dey			; proceed backwards; $01, $00, $ff, $fe...
	lda	(inptr),y	; get next data byte
	lsr	a		; lower bit -> carry
	rol	lonbuf,x	; carry -> LO buffer
	lsr	a		; lower bit -> carry
	rol	lonbuf,x	; carry -> LO buffer
	sta	(hibptr),y	; 6 upper bits -> HI buffer
	inx			; next LO index
	cpx	#$56		; circular; rolls over $56 -> $00
	bcc	splitloop	; ... not yet
	ldx	#0		; continue LO bits from start
	tya			; until	indexes	line up: x=y=0
	bne	splitloop	; ... not yet

	; $3F mask pass over the LO buffer
	; discard 2 upper bits from LO buffer (because it was not inited)
	; and tranfer to the output
	ldy	#$55		; LO buf index, $55..00
maskloop:
	lda	lonbuf,y
	and	#$3F		; clear 2 upper bits
	sta	(lobptr),y	; and store in the output
	dey			; next nibble
	bpl	maskloop	; while y>=0

	rts
.endproc


; * Track formriting entry point. Writes an entire track while formatting it.
; * In: A/Y=IOB pointer (like RWTS); IOB::bufptr=nibblized track data (see nibbtrack)
; * Out: carry 0=ok, 1=error
; * Destroys: A,X,Y,flags,several DOS ZP locations
.proc	formritep
	php			; save flags
	sei			; .. and disable interrupts
	jsr	formrite_act	; call actual
	bcs	failure
success:
	plp			; restore flags, incl. interrupts
	clc			; 0 -> carry, success
	rts
failure:
	plp			; restore flags, incl. interrupts
	sec			; 1 -> carry, failure
	rts
.endproc

; * Track formriting actual. Do not call directly. See formritep.
; * Sets up DOS ZP vars like standard RWTS.
; * Out: carry 0=ok, 1=error
; * Destroys: everything; sets drive on fire
.proc	formrite_act
	sty	rwtsiobp	; store passed in
	sta	rwtsiobp+1	; .. IOB pointer
	ldy	#IOB::slotio	; slot I/O offset: slot# * $10
	lda	(rwtsiobp),y	; get slot I/O offset
	tax
	
	; not necessary atm
	;stx	RWTSSLOTIO1	; save slot I/O offset in temp

	; here, assume that slot/drive switching, volume, motor spin-up delay, etc,
	; were all taken care of by the prior RWTS seek call.
	; just verify it is still the same slot.
	ldy	#IOB::lstslot	; last-accessed	slot I/O offset
	cmp	(rwtsiobp),y
	beq	slotok		; same slot, good to go

	lda	#$20		; pretend it is a volume mismatch
	bne	failure		; (always) go fail

slotok:	
	lda	motoron,x	; keep motor on

	; for safety, check the command
	ldy	#IOB::command	; command in IOB
	lda	(rwtsiobp),y	; get the command
	cmp	#IOBCMD::frmrite
	beq	frmrcmd		; yep, go ahead

	lda	#$08		; init error
	bne	failure		; (always) go fail

frmrcmd:
	jsr	formtrack	; go formrite!
	lda	#$08		; init error (speculative)
	bcs	failure		; fail when error (carry=1)
success:
	clc			; all good
	bcc	return		; go return ok
failure:
	sec			; error indicator
	ldy	#IOB::result
	sta	(rwtsiobp),y	; return error code
return:
	lda	motoroff,x	; turn motor off, done
	rts
.endproc	; formrite_act


; * Track formriting routine. Writes an entire track while formatting it.
; * This is a modified DOS 3.3 RWTS track format routine ($BF0D).
; * In: Standard RWTS IOB; X=slot I/O offset
; *    IOB::bufptr=nibblized track data (see nibbtrack)
; * Out: carry 0=ok, 1=error
; * Modifies: $45 (RWTSSECPAD)
; * Destroys: A,Y,flags,$27,$3E,$3F,$41,$44
.proc	formtrack
	; init volume/track/sector values needed by DOSWRSECADR routine
	ldy	#IOB::volnum	; volume# in IOB
	lda	(rwtsiobp),y	; get volume# requested
	sta	RWTSWRVOLN	; store volume# for write proc (NB: DOSWRSECADR needs this)

	ldy	#IOB::trknum	; track# in IOB
	lda	(rwtsiobp),y	; get track# requested
	sta	RWTSWRTRKN	; store track# for write proc (NB: DOSWRSECADR needs this)
	bne	nextgap1	; when not track 0, leave the inter-sector padding alone

	lda	#40		; initial inter-sector sync padding; will be reduced in	case of	overrun
	sta	RWTSSECPAD	; store initial inter-sector sync padding
				; (determined mostly during track 0 format; might be reduced by other tracks)
nextgap1:
	lda	#0
	sta	RWTSWRSECN	; 0 -> current sector# (NB: DOSWRSECADR needs this)

	; initialize the HI/LO buffer pointers
	sta	hibptr		; 0 -> HI ptr LSB (assume page-aligned)
	sta	lobptr		; 0 -> LO ptr LSB (starts page-aligned)
	ldy	#IOB::bufptr+1	; nybblized track data MSB
	lda	(rwtsiobp),y	; get track data MSB
	sta	hibptr+1	; HI ptr MSB (starts at buffer)
	clc
	adc	#16		; A = data MSB + $10 (LO offset)
	sta	lobptr+1	; LO ptr MSB (data + $1000)

	lda	#$AA		; sector address even mask
	sta	RWTSTMP8	; even mask ($AA) -> $3E
				; NB: DOSWRSECADR needs this to make ORA a 3-cycle instruction
	
	; TODO: write 200+ sync nybbles for slower drives? Should be safe to do so.
	ldy	#128		; initial 129 sync/AGC nybbles (gap 1), mostly overwritten by last sector
	bne	initgap1	; (always) gap 1 padding

secloop:
	; DOS timing spec:
	;  * 67=32+32+3 cycles from last sector addr outro nybble to first data sync nybble
	;  * 67=32+32+3 cycles from last sector data outro nybble to first inter-sector sync nybble
	; For timing accuracy, secloop should not cross page boundaries.
	; NB: here, wrsecdata has advanced the HI/LO buffer pointers for next sector

	ldy	RWTSSECPAD	; 3 - get current inter-sector padding
initgap1:
				; now: +47 cycles from sector data outro nybble
	jsr	DOSWRSECADR	; 6 - write gap (Y+1 sync nybbles), then sector address
				; NB: performs write-prot check.
				; now: +31 cycles from last outro nybble
	bcs	return		; 2 (ok); bail when carry=1 (error)
	jsr	wrsecdata	; 6 - write 5 sync nybbles, then nybblized sector data
				; now: +29 cycles from last outro nybble
	bcs	return		; 2 (ok); bail when carry=1 (error)
	inc	RWTSWRSECN	; 5 - next sector
	lda	RWTSWRSECN	; 3 - current sector#
	cmp	#16		; 2 - all sectors done?
	bcc	secloop		; 3 - not yet, do next
endsecloop:

	ldy	#15		; reset	sector counter for verify
	sty	RWTSWRSECN	; sector count to verify
	
	lda	#48		; standard number of attempts
	sta	RWTSRETRIES

clrvfyloop:			; clear sector verify table
	sta	DOSVFYTBL,y	; clear sector
	dey
	bpl	clrvfyloop	; until Y<0

	; ensure padding between sectors 15 and 0 (gap 1) is of sufficient size --
	; bigger than inter-sector (gap 3)
	; XXX: I do not know why DOS insists that the gap between sectors 15 and 0
	;   must be 25% larger than the rest. Perhaps some timing requirement for inter-leave?
	ldy	RWTSSECPAD	; wait padding * 50 cycles
waitgap1:
	jsr	return		; 12 - waste time
	jsr	return		; 12 - waste time
	jsr	return		; 12 - waste time
	pha			; 3 - waste time
	pla			; 4 - waste time
	nop			; 2 - waste time
	dey			; 2 - count
	bne	waitgap1	; 3 - branch; 50 cycles	* sector spacing

	jsr	DOSRDSECADR	; find next sector address
	bcs	findsec15	; not found in time; try sector 15
	lda	RWTSRDSECN	; sector addr found; get sector#
	beq	foundsec	; found	sector 0; not overrun and in good location; good gap size
	lda	#16
	cmp	RWTSSECPAD	; carry=0 when padding > 16
	lda	RWTSSECPAD
	sbc	#1		; reduce padding by 2 when > 16, otherwise by 1
	sta	RWTSSECPAD	; save new padding size
	cmp	#5		; when padding <= 5 ...
	bcs	findsec15	; (padding >= 5: ok)
	sec			; ... fail format
	rts

vfysecloop:			; verify all 15 sectors
	jsr	DOSRDSECADR	; find and read next sector addr
	bcs	nextsec
foundsec:			; sector addr found, check data
	jsr	DOSRDSECDAT	; verify sector data
	bcc	goodsec		; good, go mark it
nextsec:			; keep trying
	dec	RWTSRETRIES	; until	retries	exceeded
	bne	vfysecloop
findsec15:			; locate sector 15
	jsr	DOSRDSECADR	; find and read next sector addr
	bcs	retry15
	lda	RWTSRDSECN	; which sector# did we find?
	cmp	#15		; is it	sector 15?
	bne	retry15
	jsr	DOSRDSECDAT	; verify sector data
	bcs	retry15		; could not read, retry
	
	jmp	nextgap1	; sector 15 is good; padding was too large -- redo
				;  (padding was already adjusted)
retry15:			; retry locating sector 15
	dec	RWTSRETRIES
	bne	findsec15	; until	retries	exceeded
	sec			; indicate error
return:
	rts
goodsec:
	ldy	RWTSRDSECN	; last found sector# (from sec addr)
	lda	DOSVFYTBL,y	; did we find it once before?
	bmi	nextsec		; found	it before; does	not count
	lda	#$FF		; sector verified flag
	sta	DOSVFYTBL,y	; mark sector verified
	dec	RWTSWRSECN	; one less sector to verify
	bpl	vfysecloop	; until	all 16 sectors are found

	; see if we need to adjust the inter-sector padding
	lda	RWTSWRTRKN	; current track#
	bne	success		; not track 0; do not mess with padding
	lda	RWTSSECPAD	; current inter-sector sync padding
	cmp	#16		; NB: formatting track 0 (mostly) determines the sector spacing
				;   (inter-sector padding) for the entire disk.
	bcc	return		; When the experimentally-picked padding is less than 16 nybbles
				; (rpms too high), keep it as is. (carry=0 - return ok)
	dec	RWTSSECPAD	; When >=16, reduce the padding by 2.
	dec	RWTSSECPAD	; .. just in case, because it is safe to do so.
success:
	clc			; all good
	rts
.assert	>(endsecloop-1) = >(secloop), warning, "formtrack.secloop should not cross page boundaries"
.endproc	; formtrack


; * Write nibblized sector data routine.
; * (!) Time-critical; MUST NOT cross page boundaries.
; * This is a modified DOS 3.3 RWTS sector data write routine ($B82A).
; * In: Standard DOS RWTS tables; hibptr/lobptr setup; X=slot I/O offset
; * Out: carry 0=ok, 1=error
; * Modifies: hibptr/lobptr pointers
; * Destroys: A,Y,flags,$27
; alternative 1:  .align  $100			; do not cross page boundaries
; alternative 2:  .org  diskload2_org + $600	; force page alignment
	.res	<($100-(<*))	; ca65 ".align $100" without silly warnings
.proc	wrsecdata
	; DOS spec: +39 cycles since last sector address nybble write
	sec			; 2 - preemptive error indicator
	stx	RWTSTMP2	; 3 - save I/O offset for later 3-cycle reload

	lda	wrshifter,x	; 4 - write-prot test
	lda	wrprot,x	; 4 - read write-prot; sequencer to idle
	bpl	wrtok		; 3 (branch); ok to write
	jmp	return		; abort on write-prot (BMI cannot reach it now)

wrtok:
	; hack to reproduce the number of silence cycles between sectors per DOS spec:
	;   use time-wasted code locations to copy pointers
	lda	lobptr		; 3 - waste time
	lda	lobptr		; 3 - copy LO buf ptr LSB
	sta	loa1+1		; 4 - .. to abs access location

	; write 5 auto-sync nybbles
	; auto-sync nybbles are $FF (32 cycles) followed by two 0 bits (8 cycles)
	lda	#$FF		; 2 - (sync/AGC-norm nybble)
	sta	wrreq,x		; 5 - write mode; load shifter
				; ([!] 67=32+32+3 cycles since last sector address outro nybble; DOS spec)
	ora	sequencer,x	; 4 - shift out (A=FF; ORA only effects N/Z flags)

	; use time-wasted code locations to copy pointers (hack)
	lda	lobptr+1	; 3 - copy LO buf ptr MSB
	sta	loa1+2		; 4 - .. to abs access location
	lda	#$FF		; 2 - reload sync nybble

	ldy	#4		; 2 - (4 more sync nybbles)
syncloop:
	jsr	wr14nyb15	; 35=6+14+15 - write sync nybble
				; (32+8 cycles to/per sync nybble)
	dey			; 2 - next sync nybble
	bne	syncloop	; 3 (branch) or 2 (fall through)

	; use time-wasted code locations to copy pointers (hack)
	lda	hibptr+1	; 3 - copy HI buf ptr MSB
	sta	hia1+2		; 4 - .. to abs access location

	; write sector data intro sequence
	lda	#$D5		; 2 - intro nybble
	jsr	wr2nyb15	; 23=6+2+15 - write intro nybble
				; [!] (32+4 cycles to first intro nybble; DOS spec)
	lda	#$AA		; 2 - intro nybble
	jsr	wr9nyb15	; 30=6+9+15 - write intro nybble
				; (32 cycles to nybble)
	lda	#$AD		; 2 - intro nybble
	jsr	wr9nyb15	; 30=6+9+15 - write intro nybble
				; (32 cycles to nybble)

	; write LO buffer; $56 nybbles
	; the buffers were pre-nibblized by nibbsector.
	tya			; 2 - A=0, EOR chain seed
	ldy	#$56		; 2 - $56 LO nybbles
				; now: +19 cycles from intro nybble

	; NB: this wait is now artificial to reproduce [+4] DOS spec.
	;   disks would work fine without it (with perfect 32-cycle)
	nop			; 2 - waste time (for +4 nybble DOS spec)
	nop			; 2 - waste time (for +4 nybble DOS spec)

lonloop:			; (loop: +19 cycles from nybble)
loa1:	eor	$2000,y		; 4 (no page cross) - chain to next LO nibble
				; XXX: the address will be patched (code above)
	tax			; 2
	lda	DOSENYBTBL,x	; 4 - 6-bit -> disk nybble
	ldx	RWTSTMP2	; 3 - reload I/O offset
	sta	wrshifter,x	; 5 - load for write
				; [!] (32+4 cycles to first data nybble; DOS spec)
				; (32 cycles per nybble)
	lda	sequencer,x	; 4 - shift out
	lda	(lobptr),y	; 5 (no page cross) - get current LO nibble
	dey			; 2 - next, in reverse order
	bne	lonloop		; 3 (branch) or	2 (fall	through)
	
	; write HI buffer; $100 nybbles; last nybble is checksum
	; the buffers were pre-encoded for disk by nybbsector.
hinloop:			; (loop: +18 cycles from nybble)
	eor	(hibptr),y	; 5 (no page cross) - chain to next HI nibble
	tax			; 2
	lda	DOSENYBTBL,x	; 4 - 6-bit -> disk nybble
	ldx	RWTSTMP2	; 3 - reload I/O offset
	sta	wrshifter,x	; 5 - load for write
				; (32 cycles to/per nybble)
	lda	sequencer,x	; 4 - shift out
hia1:	lda	$1000,y		; 4 (no page cross) - get current HI nibble
				; XXX: the address will be patched (code above)
	iny			; 2 - next, in forward order
	bne	hinloop		; 3 (branch) or 2 (fall through)

	; write data checksum
	tax			; 2 - checksum to index
	lda	DOSENYBTBL,x	; 4 - 6-bit -> disk nybble
	ldx	RWTSTMP2	; 3 - reload I/O offset (X was destroyed)
	jsr	wr0nyb15	; 21=6+0+15 - write checksum nybble
				; (32 cycles to	nybble)

	; NB: at this point, the HI/LO pointers have served their purpose
	;   and can now be staged for the next sector
	; use time-wasted code locations to advance pointers (constant-time math)
	inc	hibptr+1	; 5 - next sector HI page
	lda	lobptr		; 3 - get LO ptr LSB
	tay			; 2 - .. and stash in Y (now free)
	eor	#$80		; 2 - advance one half page
	sta	lobptr		; 3 - LO ptr LSB += $80

	; write sector data outro sequence
	lda	#$DE		; 2 - outro nybble
	sta	wrshifter,x	; 5 - load for write
				; (32 cycles to nybble)
	lda	sequencer,x	; 4 - shift out

	; use time-wasted code locations to advance pointers (constant-time math)
	tya			; 2 - unstash old LO ptr LSB
	asl	a		; 2 - $80 -> carry, 0 -> A
	adc	lobptr+1	; 3 - A = LO ptr MSB + carry
	sta	lobptr+1	; 3 - LO ptr MSB += carry

	lda	#$AA		; 2 - outro nybble
	jsr	wr5nyb15	; 26=6+5+15 - write outro nybble
				; (32 cycles to nybble)
	lda	#$EB		; 2 - outro nybble
	jsr	wr9nyb15	; 30=6+9+15 - write outro nybble
				; (32 cycles to nybble)
	lda	#$FF		; 2 - post-outro sync nybble
	jsr	wr9nyb15	; 30=6+9+15 - write outro sync nybble
				; (32 cycles to nybble)

	lda	rdmode,x	; 4 - read mode; sequencer to idle

return:
	lda	sequencer,x	; 4 - reset shifter
	rts			; 6

wr14nyb15:
	clc			; 2 - waste time + clear carry for BCC
	bcc	wr9nyb15	; 3 - branch
wr9nyb15:
	clc			; 2 - waste time / ok for last write and rts
wr7nyb15:
	nop			; 2 - waste time
wr5nyb15:
	bit	$FF		; 3 - waste time; kills N/Z/V flags
wr2nyb15:
	nop			; 2 - waste time
wr0nyb15:
	sta	wrshifter,x	; 5 - load for write
	ora	sequencer,x	; 4 - shift out; 
				; NB: opcode sets N/Z flags depending on loaded value;
	rts			; 6
.endproc	; wrsecdata
.assert	>(*-1) = >wrsecdata, warning, "wrsecdata routine is time-critical and must not cross page boundaries"
