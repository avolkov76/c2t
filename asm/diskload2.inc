; diskload2.inc
; Diskload object config

diskload2_org	= $9700		        	; origin, should be $9700 for now

diskload2	= diskload2_org			; Entry point
dos33vecs_len	= $30				; DOS vectors are $30 bytes at $3D0
dos33vecs	= diskload2_org - dos33vecs_len	; $30 bytes of DOS vectors c2t will prepend
