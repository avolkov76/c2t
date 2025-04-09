; diskload2.inc
; Diskload object config

diskload2_org	= $AE00			; origin; 7 pages
diskload2_data	= $1000			; sector data start

; NB: Currently ends at $A9FF, but diskload3 inflate_data segment ($AB00-ADFF) can
;   also be reused. The two buffers are not used at the same time.
nybbdata_org	= $9200			; Nybblized track data
nybbdata_size	= $1800			; 16*1.5=24 pages

;--END-OF-H   ; ends C header file

diskload2	= diskload2_org		; Entry point
