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

RWTSDLYL	:= $46		; Spin-up/track switch delay counter LSB
				; NB: DOS 3.3 RWTS does *not* initialize it; 0 after first call.
				;    The first RWTS call after entering Monitor thus has a
				;    near-random delay component.
RWTSDLYH	:= $47		; Spin-up/track switch delay counter MSB
RWTSIOBPTR	:= $48		; IOB pointer while in RWTS. Rumor has it this should be cleared
				;   to 0 after calling RWTS, and before reentering Mon.
; TODO: more

; DOS page 3 vectors
DOSP3VECS	:= $03D0	; DOS warm start thunk
DOSWARM		:= $03D0	; DOS warm start thunk
DOSCOLD		:= $03D3	; DOS cold start thunk
DOSFMCALL	:= $03D6	; DOS File Manager thunk entry point
DOSRWTSCALL	:= $03D9	; DOS RWTS thunk entry point
DOSFMPRMLIST	:= $03DC	; DOS get File Manager paramlist pointer
DOSRWTSIOB	:= $03E3	; DOS get RWTS IOB pointer

DOSP3VECSIZE	= $30		; All page 3 vectors installed by DOS, all through IRQ vec

; DOS RWTS locations at highest load range
DOSDEFIOB	:= $B7E8	; Default RWTS IOB; can be used by anyone; returned by DOSRWTSIOB
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
