; diskload3.inc
; Diskload Inflate object config

diskload3_org	= $9B00			; origin, should be $9B00 for now
diskload3_zp	= $10

;--END-OF-H   ; ends C header file

inflate		= diskload3_org		; Entry point
inflate_zp	= diskload3_zp
inflate_data	= $9200
