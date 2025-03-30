; diskload3.inc
; Diskload Inflate object config

diskload3_org	= $B500			; origin; 2 pages; DOS begins at $B700
diskload3_zp	= $10

;--END-OF-H   ; ends C header file

inflate		= diskload3_org		; Entry point
inflate_zp	= diskload3_zp
inflate_data	= $AE00			; 3 pages (rounded up); diskload2 at $B100
