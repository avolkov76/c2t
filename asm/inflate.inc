; inflate.inc
; Autoload Inflate object config

autoload3_org	= $BA00		; origin, should be $BA00 for now
autoload3_zp	= $0

;--END-OF-H   ; ends C header file

inflate		= autoload3_org
inflate_zp	= autoload3_zp
inflate_data	= $BC00
