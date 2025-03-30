; diskload2.inc
; Diskload object config

diskload2_org	= $B100			; origin; 4 pages
diskload2_data	= $1000			; sector data start

;--END-OF-H   ; ends C header file

diskload2	= diskload2_org		; Entry point
