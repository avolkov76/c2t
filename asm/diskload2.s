;diskload2.s

.include "apple2.inc"
.include "disk2.inc"
.include "dos33.inc"
.include "diskload1.inc"
.include "diskload2.inc"
.include "diskload3.inc"

cout	=	COUT		; character out sub
crout	=	CROUT		; CR out sub
prbyte	=	PRBYTE 		; print byte in hex
warm	=	MONZ		; back to monitor
clear	=	CR		; clear screen
movecur	=	TABV		; move cursor to ch,a
cleos	=	CLREOP		; clear to end of screen (page)
reboot	=	PWRUP		; reboot machine
bell	=	BELL2		; ding
rdkey	=	RDKEY		; read key

rwts	=	DOSRWTSEP	; RWTS direct entry point

; my vectors

readtape=	diskload1

; zero page parameters

begload	=	diskload1_zp+0	; begin load location LSB/MSB
endload	=	diskload1_zp+2	; end load location LSB/MSB
chksum	=	diskload1_zp+4	; checksum location
; TODO: continue diskload1_zp+X allocations here?
secnum	=	$05		; loop var
trknum	=	$06		; loop var
segcnt	=	$07		; loop var
buffer	=	$08		; MSB of RWTS buffer
trkcnt	=	$09		; track counter (0-6)
iobptr	=	$0A		; RWTS IOB pointer LSB/MSB
romptr	=	$0A		; (overload) CX00 slot ROM ptr for Disk II detection
prtptr	=	$0C		; pointer LSB/MSB
fmptr	=	$0E		; file manager pointer
;inf_zp	=	$10		; inflate vars (10); see diskload3.inc
temp	=	$1E		; temp var
ch	=	CH		; cursor horizontal
preg	=	STATUS		; mon p reg

; other vars

invsp	=	$60		; inverse space for draw
data	=	diskload2_data	; 7 track dump from inflate
cmpbuf	=	$9200		; buffer for sector check
count	=	$900

defd2sl	=	6		; default Disk II slot (not detected)
d2drvno	=	1		; for now, presume drive 1
volnum	=	254		; volume number we will use (unlikely to ever change)

	.org	diskload2_org

start:
	jsr	clear		; clear screen
	lda	#<title		; print title
	ldy	#>title
	jsr	inv
				; TRACK
	lda	#19		; col 20
	sta	ch
	lda	#0		; row 0
	jsr	movecur
	lda	#<track		; print track
	ldy	#>track
	jsr	print

	lda	#<header	; print header
	ldy	#>header
	jsr	print
	ldx	#35		; length of line
	jsr	line

	lda	#<left		; print left side of grid
	ldy	#>left
	jsr	print

detectdisk:			; detect Disk II controller
	lda	#0		; using IIe firmware protocol
	sta	romptr		; init the slot ROM pointer
	ldx	#0-1		; first slot index; -1 for INX
dslotloop:
	inx			; next slot in order
 	lda	d2sltord,x	; get next slot#
	beq	notdetected	; the end; controller not found
	ora	#>IOBASE	; to slot ROM page
	sta	romptr+1	; set the ROM ptr page
	ldy	#8-1		; 8-byte ROM sig, last byte
dsigloop:
	lda	(romptr),y	; read slot ROM byte
	cmp	d2romsig,y	; compare to ROM signature
	bne	dslotloop	; mismatch, try next slot
	dey
	dey			; compare every other byte
	bpl	dsigloop	; until y<0
detected:
	lda	d2sltord,x	; get detected slot#
	sta	d2detect	; detection flag
	bne	saveslot	; always; and "slot 0" guard for free
notdetected:
	lda	#defd2sl	; use default slot#
saveslot:
	sta	d2slot		; save slot#
	asl			; A*=$10
	asl			; convert to slot I/O ofs
	asl
	asl
	sta	d2slotio	; save slot I/O ofs

printdisk:			; display disk target
	lda	#12		; col 12 (0-based)
	sta	ch
	lda	#20		; row 20 (0-based)
	jsr	movecur
	lda	#<diskm		; print "DISK:"
	ldy	#>diskm
	jsr	print

	lda	d2slot		; inject slot# into "Sx,Dy" msg
	clc
	adc	#'0'		; to ascii digit
	sta	diskm2+1	; inject digit
	lda	#d2drvno	; inject drive# into "Sx,Dy" msg
	adc	#'0'		; to ascii digit
	sta	diskm2+4	; inject digit
	lda	#<diskm2	; print "Sx,Dy"
	ldy	#>diskm2
	jsr	inv

	lda	d2detect	; Disk II detection flag
	beq	prdsknot	; was not detected
	lda	#<diskdetm	; print "DETECTED"
	ldy	#>diskdetm
	bne	prdskdet	; always (y=page)
prdsknot:
	lda	#<disknotm	; print "NOT DETECTED"
	ldy	#>disknotm
prdskdet:
	jsr	print

initdos:
	; Init some non-obvious DOS locations so we start in guaranteed known state,
	; since none of boot 1 or boot 2 executed, and data could be anything.
	ldy	d2slot
	lda	#40*2		; head position just beyond max (track 80)
	sta	RWTSD1CURTRK,y	; init drive 1 head pos for slot
	sta	RWTSD2CURTRK,y	; init drive 2 head pos for slot
	; Init the otherwise untouched RWTS spin-up delay counter LSB to
	; ensure deterministic timing from this point on.
	lda	#1		; delay counter LSB=1
	sta	RWTSDLY		; set delay counter LSB (uninited by RWTS)

setupiob:
	ldy	#<DOSDEFIOB	; Equivalent to DOSRWTSIOB call
	lda	#>DOSDEFIOB
	sty	iobptr		; and save pointer
	sta	iobptr+1

	lda	#1		; IOB version, must be 1
	ldy	#IOB::ver	; offset in IOB
	sta	(iobptr),y	; write it to IOB

	lda	d2slotio	; disk II slot I/O offset (slot# * $10)
	ldy	#IOB::slotio	; slot to access in IOB
	sta	(iobptr),y	; write it to IOB
	; Must init; presume no drive was actually accessed in last 2 seconds.
	; set to the slot we want to use to suppress slot switch logic in RWTS
	ldy	#IOB::lstslot	; last slot accessed in IOB
	sta	(iobptr),y	; write it to IOB

	lda	#d2drvno	; drive number
	ldy	#IOB::drvnum	; drive to access in IOB
	sta	(iobptr),y	; write it to IOB
	; Must init; presume no drive was actually accessed in last 2 seconds.
	; set to the drive we want to use to suppress drive switch logic in RWTS
	ldy	#IOB::lstdrv	; last drive accessed in IOB
	sta	(iobptr),y	; write it to IOB

	lda	#volnum		; volume number
	ldy	#IOB::volnum	; volume to access in IOB
	sta	(iobptr),y	; write it to IOB
	; Must init; presume no drive was actually accessed in last 2 seconds.
	; TODO: last volume number should actually be set by RWTS. When not formatting,
	; the last volume number will be set by issuing a read(1) over track 0.
	ldy	#IOB::lstvol	; last volume accessed in IOB
	sta	(iobptr),y	; write it to IOB

	lda	#0		; 256 bytes/sector
	ldy	#IOB::secsize	; sector size in IOB
	sta	(iobptr),y	; write it to IOB

format:				; format the diskette
	lda	infdata+20	; check noformat flag
	bne	endformat	; if not 0 jump to endformat

	jsr	status
	lda	#<formatm	; print formatting
	ldy	#>formatm
	jsr	print

;;; RWTS format (works here)
	lda	#IOBCMD::format	; format(4) command
	jsr	rwtscall	; do it!
	bcs	formaterror

	; Incur the seek to 0 penalty now instead of when writing first data block
	lda	#0		; track 0
	ldy	#IOB::trknum	; offset in IOB
	sta	(iobptr),y	; write it to IOB

	lda	#IOBCMD::seek	; seek(0) command
	jsr	rwtscall	; invoke RWTS
	bcs	formaterror

	; XXX: I do not know which Apple II models need this STATUS patch.
	; But I am quite certain that IIe does not.
	lda	#0
	sta	preg		; fix p reg so mon is happy
	jmp	endformat
formaterror:
	jmp	diskerror
endformat:

;;;begin segment loop (5)
	lda	#0		; buffer LSB
	ldy	#IOB::bufptr	; offset in IOB
	sta	(iobptr),y	; write it to IOB

	lda	#0
	sta	trknum		; start with track 0
	lda	#5
	sta	segcnt
segloop:

;;; fancy status here
;	jsr	status
;	lda	#<waitm		; print waiting for data
;	ldy	#>waitm
;	jsr	print
;countdown:
;	lda	#<count		; store begin location LSB
;	sta	begload
;	lda	#>count		; store begin location MSB
;	sta	begload+1
;	lda	#<count+4	; store end location LSB
;	sta	endload
;	lda	#>count		; store end location MSB
;	sta	endload+1
;;;; hack readtape, fix later, POC for now
;	lda	#$60		; return without check
;	sta	$9091
;	jsr	readtape	; get the code
;	lda	#$8A		; put TXA back
;	sta	$9091
;;;; end hack
;	lda	#18
;	sta	ch		; horiz
;	lda	#22		; vert
;	jsr	movecur		; move cursor to $24,a; 0 base
;	jsr	cleos
;	lda	#<count		; print count down
;	ldy	#>count
;	jsr	print
;	lda	count
;	cmp	#$B0
;	bne	countdown
;	lda	count+1
;	cmp	#$B0
;	bne	countdown
;;; end fancy stuff

;;; get 7 tracks from tape
load:
	jsr	status
	lda	#<loadm		; print loading data
	ldy	#>loadm
	jsr	flash
	lda	#<loadm2	; print loading data
	ldy	#>loadm2
	jsr	print

	sec
	lda	#5
	sbc	segcnt
	asl
	asl
	tax
	stx	temp

	lda	infdata+2,x	; get sec
	jsr	cout
	lda	infdata+3,x	; get sec
	beq	second
	jsr	cout
second:
	lda	#<secm		; print sec
	ldy	#>secm
	jsr	print

	ldx	temp
	lda	infdata+0,x	; store begin location LSB
	sta	begload
	lda	infdata+1,x	; store begin location MSB
	sta	begload+1

	lda	#<diskload1_org	; store end location LSB
	sta	endload
	lda	#>diskload1_org	; store end location MSB
	sta	endload+1

	jsr	readtape	; get the code
inf:
				; turn motor on to save 1-2 sec
	ldx	d2slotio	; slot# * $10
	lda	motoron,x	; turn it on

	jsr	status
	lda	#<inflatem	; print inflating
	ldy	#>inflatem
	jsr	print

	ldx	temp
	lda	infdata+0,x	;src lsb
	sta	inflate_zp+0
	lda	infdata+1,x	;src msb
	sta	inflate_zp+1
	lda	#<data		;dst lsb
	sta	inflate_zp+2
	lda	#>data		;dst msb
	sta	inflate_zp+3

	jsr	inflate

	lda	#$00		;dst end +1 lsb
	cmp	inflate_zp+2
	bne	error
	lda	#$80		;dst end +1 msb
	cmp	inflate_zp+3
	bne	error

;;;begin track loop (7)
	jsr	status
	lda	#<writem	; print writing
	ldy	#>writem
	jsr	print

	lda	#>data
	sta	buffer
	lda	#7
	sta	trkcnt		; do 7 tracks/segment
trkloop:
	lda	trknum		; track number
	ldy	#IOB::trknum	; offset in IOB
	sta	(iobptr),y	; write it to IOB

;;;begin sector loop (16), backwards is faster, much faster
	lda	#$F
	sta	secnum
secloop:
	;jsr	draw_w		; write sector from buffer to disk
	jsr	draw_s		; write sector from buffer to disk
	lda	secnum		; sector number
	ldy	#IOB::secnum	; offset in IOB
	sta	(iobptr),y	; write it to IOB

	lda	buffer		; buffer MSB
	clc
	adc	secnum
	ldy	#IOB::bufptr+1	; offset in IOB
	sta	(iobptr),y	; write it to IOB

	lda	#IOBCMD::write	; write(2) command
	jsr	rwtscall	; do it!
	bcs	diskerror
	lda	#0
	sta	preg		; fix p reg so mon is happy

	;jsr	draw_r		; read sector from disk to compare addr
	;lda	#>cmpbuf	; compare MSB
	;ldy	#IOB::bufptr+1	; offset in IOB
	;sta	(iobptr),y	; write it to IOB

	;lda	#IOBCMD::read	; read(1) command
	;jsr	rwtscall	; do it!
	;bcs	diskerror
	;lda	#0
	;sta	preg		; fix p reg so mon is happy

	;;; compare code

	;jsr	draw_s		; draw a space in the grid if OK

	dec	secnum
	bpl	secloop
;;;end sector loop

	lda	buffer		; buffer += $10
	clc
	adc	#$10
	sta	buffer

	inc	trknum		; next track
	dec	trkcnt		;
	bne	trkloop		; 0, all done with 7 tracks
;;;end track loop

	dec	segcnt		;
	beq	done		; 0, all done with 5 segments
	jmp	segloop
;;;end segment loop

;;; prompt for data only load?
done:
	jsr	status
	lda	#<donem		; print done
	ldy	#>donem
	jsr	print
	jsr	bell
	jsr	rdkey
	jmp	reboot
error:
				; turn motor off, just in case left on
	ldx	d2slotio	; slot# * $10
	lda	motoroff,x	; turn it off

	lda	#<errorm	; print error
	ldy	#>errorm
	jsr	print
	jmp	warm
diskerror:
	lda	#0
	sta	preg		; fix p reg so mon is happy
	jsr	status
	lda	#<diskerrorm	; print error
	ldy	#>diskerrorm
	jsr	print
	jmp	warm
status:
	lda	#0
	sta	ch		; horiz
	lda	#22		; vert
	jsr	movecur		; move cursor to $24,a; 0 base
	jmp	cleos

rwtscall:			; in: A=RWTS command
	ldy	#IOB::command	; offset in IOB
	sta	(iobptr),y	; write command to IOB
	ldy	iobptr		; load IOB pointer
	lda	iobptr+1	; IOB MSB
	jsr	rwts		; call RWTS
	rts
	
draw_w:				; print a 'W' in the grid
	clc
	lda	#4
	adc	secnum
	tay
	lda	#4
	adc	trknum
	ldx	#'W'
	jmp	draw
draw_r:				; print a 'R' in the grid
	clc
	lda	#4
	adc	secnum
	tay
	lda	#4
	adc	trknum
	ldx	#'R'
	jmp	draw
draw_s:				; print a ' ' in the grid
	clc
	lda	#4
	adc	secnum
	tay
	lda	#4
	adc	trknum
	ldx	#invsp
draw:				; a=horiz, y=vert, x=letter
	sta	ch		; horiz
	tya
	jsr	movecur
	txa
	eor	#$40
	jsr	cout
	rts
line:
	lda	#'-'
	ora	#$80
loop0:
	jsr	cout
	dex
	bne	loop0
	jsr	crout
	rts
inv:
	sta	prtptr
	sty	prtptr+1
	ldy	#0
	lda	(prtptr),y	; load initial char
inv1:	and	#$3F
	jsr	cout
	iny
	lda	(prtptr),y
	bne	inv1
	rts
flash:
	sta	prtptr
	sty	prtptr+1
	ldy	#0
	lda	(prtptr),y	; load initial char
flash1:	ora	#$40
	jsr	cout
	iny
	lda	(prtptr),y
	bne	flash1
	rts
print:
	sta	prtptr
	sty	prtptr+1
	ldy	#0
	lda	(prtptr),y	; load initial char
print1: ora	#$80
	jsr	cout
	iny
	lda	(prtptr),y
	bne	print1
	rts
title:
	.asciiz	"INSTA-DISK"
errorm:
	.asciiz	"ERROR"
diskerrorm:
	.asciiz	"DISK ERROR"
donem:
	.asciiz	"DONE. PRESS [RETURN] TO REBOOT."
inflatem:
	.asciiz	"INFLATING DATA "
loadm:
	.asciiz	"LOADING DATA"
loadm2:
	.asciiz	", ETA "
secm:
	.asciiz	" SEC. "
formatm:
	.asciiz	"FORMATTING DISK "
waitm:
	.asciiz	"WAITING FOR DATA: "
writem:
	.asciiz	"WRITING DATA "
track:
	.byte	"TRACK",$0D,0
header:
	.byte   "              1111111111222222222233333",$0D
	.byte   "    01234567890123456789012345678901234",$0D
	.byte	"    ",0
left:
	.byte	"  0|",$0D
	.byte	"  1|",$0D
	.byte	"  2|",$0D
	.byte	"  3|",$0D
	.byte	"  4|",$0D
	.byte	"S 5|",$0D
	.byte	"E 6|",$0D
	.byte	"C 7|",$0D
	.byte	"T 8|",$0D
	.byte	"O 9|",$0D
	.byte	"R A|",$0D
	.byte	"  B|",$0D
	.byte	"  C|",$0D
	.byte	"  D|",$0D
	.byte	"  E|",$0D
	.byte	"  F|",$0D,0
diskm:
	.asciiz	"DISK: "
diskm2:
	.asciiz	"S6,D1"
diskdetm:
	.asciiz	" (DETECTED)"
disknotm:
	.asciiz	" (ASSUMED)"
d2detect:			; Disk II detection flag
	.byte	0		; presume not detected
d2slot:
	.byte	6		; presume slot 6
d2slotio:
	.byte	6 * $10		; presume slot 6 I/O offset
d2romsig:			; Disk II ROM values used in firmware protocol
				; every other byte, offs 1,3,5,7
	.byte	$FF, $20, $FF, $00, $FF, $03, $FF, $3C
d2sltord:			; Disk II customizable slot scan order
				; XXX: IIe starts at 7; safer to start at 6
	.byte	6, 5, 4		; conservative scan
;	.byte	6, 5, 4, 7	; alternate; 7 scanned last
	.byte	0		; 0-terminated

infdata:
	;.byte	0,0,0,0		; LSB/MSB start, ETA in sec
	;.byte	0,0,0,0		; LSB/MSB start, ETA in sec
	;.byte	0,0,0,0		; LSB/MSB start, ETA in sec
	;.byte	0,0,0,0		; LSB/MSB start, ETA in sec
	;.byte	0,0,0,0		; LSB/MSB start, ETA in sec
	;.byte	0		; format flag, 1 = no format

.assert	* + (4*5 + 1) <= diskload3_org, warning, "diskload2 too large; overruns diskload3"
.assert	inflate_data + $300 <= diskload2_org, warning, "diskload3 inflate_data overruns diskload2"
