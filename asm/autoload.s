;autoload.s

.include "apple2.inc"
.include "inflate.inc"
.include "autoload.inc"

cout	=	COUT		; character out sub
crout	=	CROUT		; CR out sub
prbyte	=	PRBYTE 		; print byte in hex
warm	=	MONZ		; back to monitor
readblk	=	READ		; read a block from tape

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
        sta	load,x
	inx
	bne	move1		; move 256 bytes
	jmp	load
moved:
	.org	autoload_org
load:
	lda	#<loadm
	ldy	#>loadm
	jsr	print

	lda	ld_beg
	sta	$3C		; starting tape address low
	lda	ld_beg+1
	sta	$3D		; starting tape address high
	lda	ld_end
	sta	$3E		; ending tape address low
	lda	ld_end+1
	sta	$3F		; ending tape address high
	jsr	readblk		; read block from tape

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
loadm:	
	;.asciiz	"LOADING "

end:

