; diskload1.inc
; Diskload bootstrapper object config

diskload1_org	= $9000			; origin, should be $9000 for now
diskload1_zp	= $00			; object's ZP usage
diskload1_msg	= $83E			; "LOADING" message in low mem
diskload1_mlen	= 34			; "LOADING" message space

;--END-OF-H   ; ends C header file

diskload1	= diskload1_org		; Entry point
