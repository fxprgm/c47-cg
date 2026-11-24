# README screenshot: the main screen after a few calculations, with the CPX menu open.
wait 3000
# 20 x! -> 20! as an exact long integer (2 432 902 008 176 640 000)
key 2 0 SHIFT_F INV
# pi (f Rv), to 34 digits
key SHIFT_F RDN
# square root of 2
key 2 SQRT
# 3 ENTER 4 f ENTER (CPX) -> the complex number 3+4i
key 3 ENTER 4 SHIFT_F ENTER
# g SQRT (>POL): show it in polar form, magnitude 5 at angle 53.13...
key SHIFT_G SQRT
# g ENTER opens the CPX menu; g F6 = POLAR, the display mode for complex results
key SHIFT_G ENTER
key SHIFT_G F6
wait 500
shot out/readme_stack.ppm
