# README screenshot: a damped oscillation, y = sin(2x)*exp(-x/4), graphed from the equation editor.
wait 3000
# g CHS (TRG menu), F2 = RAD: radians for sin(), then EXIT the menu
key SHIFT_G CHS F2 EXIT
# f MUL opens the EQN menu, F1 = NEW starts the equation editor (alpha mode, lower case)
key SHIFT_F MUL
wait 300
key F1
wait 300
# type  sin(2x x) x exp(-x/4)   (letters: s=5 i=RDN n=7 x=3 e=LOG p=9; g+key: digits and
# the operators x - / ; f F5 / f F6 are the ( and ) softkeys)
key 5 RDN 7 SHIFT_F F5 SHIFT_G 2 SHIFT_G MUL 3 SHIFT_F F6
key SHIFT_G MUL LOG 3 9 SHIFT_F F5 SHIFT_G SUB 3 SHIFT_G RS SHIFT_G 4 SHIFT_F F6
# ENTER stores it; f F6 = Graph; F5 = Graphs, the window: x from 0 to 10, y from -0.8 to 1
key ENTER
wait 500
key SHIFT_F F6
wait 500
key F5
wait 300
key 0 F1 1 0 F2 DOT 8 CHS F3 1 F4
wait 300
# F5 = Draw
key F5
wait 8000
# the plot menu's third page (DOWN DOWN): f F2 turns the box markers off, a plain line;
# UP UP back to the first page (zoom, x range)
key DOWN DOWN SHIFT_F F2
wait 3000
key UP UP
wait 500
shot out/readme_graph.ppm
