; diskload3.inc
; Diskload Inflate object config

diskload3_org	= $9B00  	; origin, should be $9B00 for now

inflate		= diskload3_org	; Entry point
inflate_zp	= $10
inflate_data	= $9200
