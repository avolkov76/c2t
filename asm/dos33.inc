; dos33.inc
; DOS 3.3 vectors, routines, ZP locations, etc.

; DOS ZP locations
RWTSTMP1	:= $26		; RWTS scratch register; used all over
RWTSTMP3	:= $2A		; Current track scratch register while RWTS is seeking
RWTSTMP4	:= $2B		; Current slot I/O ofs scratch register while RWTS is seeking
RWTSTMP6	:= $35		; Current drive scratch register while in RWTS
RWTSDCTPTR	:= $3C		; Device Charateristics Table (DCT) pointer while in RWTS
RWTSUSRBUF	:= $3E		; 256-byte user buffer for sector read/write while in RWTS
RWTSTMP8	:= $3E		; Sector addr mask scratch register while RWTS is formatting

RWTSDLY		:= $46		; Spin-up/track switch delay counter LSB
				; NB: DOS 3.3 RWTS does *not* initialize it; 0 after first wait.
				;    The first RWTS call after entering Monitor thus has a
				;    near-random delay component.
RWTSDLYH	:= $47		; Spin-up/track switch delay counter MSB
RWTSIOBPTR	:= $48		; IOB pointer while in RWTS. Rumor has it this should be cleared
				;   to 0 after calling RWTS, and before reentering Mon.
; TODO: more

; DOS page 3 vectors
DOSP3VECS	:= $03D0	; DOS vectors location in page 3
DOSWARM		:= $03D0	; DOS warm start thunk
DOSCOLD		:= $03D3	; DOS cold start thunk
DOSFMCALL	:= $03D6	; DOS File Manager thunk entry point
DOSRWTSCALL	:= $03D9	; DOS RWTS thunk entry point
DOSFMPRMLIST	:= $03DC	; DOS get File Manager paramlist pointer
DOSRWTSIOB	:= $03E3	; DOS get RWTS IOB pointer

DOSP3VECSIZE	= $30		; All page 3 vectors installed by DOS, all through IRQ vec

; DOS RWTS video hole steals (belongs to slots)
RWTSCURTRK	:= $0478	; RWTS: current head position (steals slot 0 hole)
RWTSD1CURTRK	:= $0478	; RWTS: + slot# (starting at 1); current head position on slot/drive 1
RWTSMAXSEEK	:= $04F8	; RWTS: max track seek retries (steals slot 0 hole)
RWTSD2CURTRK	:= $04F8	; RWTS: + slot# (starting at 1); current head position on slot/drive 2
RWTSRETRIES	:= $0578	; RWTS: max number of I/O retries (steals slot 0 hole)
RWTSSLOTIO1	:= $05F8	; RWTS: temp I/O offset (slot# * $10) of current slot (steals slot 0 hole)
RWTSSLOTIO2	:= $0578	; RWTS WRDATA: temp I/O offset (slot# * $10) of current slot (steals slot 0 hole)
				; NB: non-ZP location used for timing: produces 4-cycle load instruction
RWTSMAXRECAL	:= $06F8	; RWTS: max recalibration retries (steals slot 0 hole)


; DOS locations at highest load range
DOSBOOT2HI	:= $9D00	; Boot 2 high load location

; DOS RWTS locations at highest load range
DOSFMPRMPTR	:= $9D0E	; File Manager Paramlist pointer (see FMPL); returned by DOSFMPRMLIST
DOSFMPRMBUF	:= $B5BB	; File Manager Paramlist actual (see FMPL); pointed to by DOSFMPRMPTR
DOSFMEP		:= $AAFD	; File Manager entry point (has X=0 check)
DOSIOBPTR	:= $AAC1	; RWTS default IOB pointer (see IOB); returned by DOSRWTSIOB
DOSDEFIOB	:= $B7E8	; Default RWTS IOB (see IOB); can be used by anyone; pointed to by DOSIOBPTR
DOSDEFDCT	:= $B7FB	; Default RWTS DCT; can be used by anyone; referenced by DOSDEFIOB
DOSRWTSEP	:= $B7B5	; RWTS entry point; disables interrupts
DOSRDSECDATA	:= $B8DC	; RWTS read sector data routine; waits up to $20 nybbles for sector data
DOSRDSECADDR	:= $B944	; RWTS read sector address routine; waits up to $304 nybbles for sector addr
DOSWRSECDATA	:= $BC56	; RWTS write sector data routine; writes prenibblized buffers
DOSWRSECADDR	:= $BC56	; RWTS write sector address routine; only used when formatting
DOSRWTSNAKED	:= $BD00	; Actual RWTS; naked and alone
DOSSETTRK	:= $BE5A	; RWTS set track routine
DOSFMTDSK	:= $BEAF	; RWTS format disk routine
DOSFMTTRK	:= $BF0D	; RWTS format track routine
; TODO: more

; DOS structures and enums

.struct IOB	; RWTS I/O Control Block
	ver	.byte		; 00: must be $01
	slotio	.byte		; 01: Slot I/O offset: slot# * $10
	drvnum	.byte		; 02: drive number, 1 or 2
	volnum	.byte		; 03: volume number expected; $00 = any
	trknum	.byte		; 04: Track number; 0-35
	secnum	.byte		; 05: Sector number; 0-15
	dctptr	.word		; 06: Device Characteristics Table pointer
	bufptr	.word		; 08: Pointer to 256 bytes of data for read/write
	_resrv1	.res	1	; 0A: Reserved; not used, set to 0
	secsize	.byte		; 0B: $00 for 256-byte sector
	command	.byte		; 0C: See IOBCMD; 0=seek, 1=read, 2=write, 4=format
	result	.byte		; 0D: Return code: $00=OK, $08=init error, $10=write protected,
				;   $20=volume mismatch, $40=drive error, $80=read error
	lstvol	.byte		; 0E: Last-accessed volume number (must init or RWTS will barf)
	lstslot	.byte		; 0F: Last-accessed slot I/O offset (must init for RWTS)
	lstdrv	.byte		; 10: Last-accessed drive number (must init for RWTS)
				; NB: Last slot and drive *must* be inited before the first
				;   RWTS call. Unpredictable RWTS slot/drive switch behavior
				;   may result if left uninited (e.g. hangs).
.endstruct
.assert	.sizeof(IOB) = $11, error, "DOS IOB must be $11 bytes long"

.enum IOBCMD	; RWTS I/O Control Block command
	seek	= 0
	read	= 1
	write	= 2
	format	= 4
.endenum

.struct DCT	; RWTS Device Characteristics Table
	devtyp	.byte		; 00: Device type; 0 for Disk II
	trkphc	.byte		; 01: Phases per track - 1; 1 for Disk II
	_ignr1	.byte		; 02: DOS 3.3 RWTS does not use this (set to $EF)
	mtrdly	.byte		; 03: Motor spin-up delay count; $D8 for Disk II
.endstruct
.assert	.sizeof(DCT) = 4, error, "DOS DCT must be 4 bytes long"

.struct FMPL	; File Manager Parameter List
	command	.byte		; 00: See FMPLCMD
	cmdprm	.byte		; 01: Command parameter for read/write
	params	.res	8	; 02-09: Command-specific params
	result	.byte		; 0A: Return code: $00=OK, $02=bad call, $04=write protected,
				;   $06=file not found, $07=volume mismatch, $08=disk i/o error, etc.
	_resrv1	.res	1	; 0B: Reserved; not used
	wrkaptr	.word		; 0C: FileMan workarea buffer (45 bytes) pointer; preserve between calls
	tslptr	.word		; 0E: Track/Sector List buffer (256 bytes) pointer; preserve between calls
	bufptr	.word		; 10: Sector data buffer (256 bytes) pointer
.endstruct
.assert	.sizeof(FMPL) = $12, error, "DOS FMPL must be $12 bytes long"

.enum FMPLCMD	; FileMan Parameter List command
	noop	= 0
	open	= 1
	close	= 2
	read	= 3
	write	= 4
	delete	= 5
	catalog	= 6
	lock	= 7
	unlock	= 8
	rename	= 9
	position= 10
	init	= 11		; Disk init: format, write catalog, write DOS image
	verify	= 12
.endenum
