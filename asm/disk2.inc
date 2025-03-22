; disk2.inc
; Disk II controller and drive I/O

; $C080 - C087 are the 4 stepper phases
phase0clr	:= $C080
phase0set	:= $C081
phase1clr	:= $C082
phase1set	:= $C083
phase2clr	:= $C084
phase2set	:= $C085
phase3clr	:= $C086
phase3set	:= $C087

motoroff	:= $C088		; Turn drive motor off
motoron		:= $C089		; Turn drive motor on
drv1sel		:= $C08A		; Select drive 1
drv2sel		:= $C08B		; Select drive 2
sequencer	:= $C08C		; Activate sequencer
rdshifter	:= $C08C		; Read shifter
wrshifter	:= $C08D		; Load shifter for write
wrprot		:= $C08E		; Read write-protected status
rdmode		:= $C08E		; Set read mode / sequencer to idle
wrreq		:= $C08F		; Set write mode / load shifter for write
