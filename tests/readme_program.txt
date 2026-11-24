# README screenshot: keying in a program, an iterative Fibonacci 'FIB' (n in X -> F(n) in X):
#   LBL 'FIB'  STO 00  1  0  LBL 01  +  LASTx  x<>y  DSE 00  GTO 01  RTN
# then stepping back up into its loop, the cursor on a step.
wait 3000
# f R/S = P/R: program entry mode (it opens the P.FN menu)
key SHIFT_F RS
wait 300
# LBL (P.FN F1), f XEQ = alpha, F I B (the LN, Rv and sqrt keys), ENTER: LBL 'FIB'
key SHIFT_G RS F1
key SHIFT_F XEQ LN RDN SQRT ENTER
# STO 00, then the steps 1 and 0 (ENTER only ends the number, it is not recorded)
key STO 0 0
key 1 ENTER 0
# LBL 01
key SHIFT_G RS F1 0 1
# + LASTx (f x<>y) x<>y
key ADD SHIFT_F SWAP SWAP
# DSE 00 (P.FN, f F5 = LOOP, F1 = DSE)
key SHIFT_G RS SHIFT_F F5 F1 0 0
# GTO 01 (g XEQ)
key SHIFT_G XEQ 0 1
# RTN (P.FN F4)
key SHIFT_G RS F4
wait 300
# UP (BST) three times: the cursor on step 8, the whole loop LBL 01 ... GTO 01 in view
key UP UP UP
wait 500
shot out/readme_program.ppm
