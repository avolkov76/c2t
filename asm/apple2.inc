; apple2.inc
; Apple II Monitor entry points, ZP locations, and I/O

; ZP Monitor vars
CH		:= $24	; Cursor horizontal position
CV		:= $25	; Cursor vertical position
BASL		:= $28	; Text base address low
BASH		:= $29	; Text base address high
INVFLG		:= $32	; Normal/inverse(/flash)
PROMPT		:= $33	; Used by GETLN 
CSW		:= $36	; Character out soft-link
KSW		:= $38	; Keyboard in soft-link
ACC		:= $45	; Monitor BRK A register save location
XREG		:= $46	; Monitor BRK X register save location
YREG		:= $47	; Monitor BRK Y register save location
STATUS		:= $48	; Monitor BRK MPU status (P) register save location 
SPNT		:= $49	; Monitor BRK stack pointer (S) register save location

; Page 3 stuff
BRKV            := $03F0	; Break (BRK) vector
SOFTEV          := $03F2	; Vector for warm start
PWREDUP         := $03F4	; This must be = EOR #$A5 of SOFTEV+1
AMPERV          := $03F5	; Thunk vector for & commands
USRADR          := $03F8	; Thunk vector for Monitor user commands
NMI             := $03FB	; NMI handler thunk
IRQLOC          := $03FE	; IRQ handler vector

; I/O addresses
TAPEIN		:= $C060	; read tape interface

; Monitor routines
COUT		:= $FDED	; character out sub
CROUT		:= $FD8E	; CR out sub
CLREOP		:= $FC42	; clear to end of screen
CR		:= $FC58	; clear screen
TABV		:= $FB5B	; move cursor to ch,a
PRBYTE		:= $FDDA	; print a byte in hex

RESET		:= $FA62	; reset Mon routine
PWRUP		:= $FAA6	; cold-start reset routine
BELL1		:= $FBD9	; ding
BELL2		:= $FBDD	; ding
RDKEY		:= $FD0C	; read key
READ		:= $FEFD	; read a block from tape
MON     	:= $FF65	; Monitor EP with a bell
MONZ		:= $FF69	; Monitor EP w/o a bell
