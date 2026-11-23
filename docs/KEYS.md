# Keys

C47 is built with the R47 keyboard. Each CG50 key stands for one R47 key position; what that
key does (and its f and g shifted functions) is the R47's, as printed on the R47 and in the
C47/R47 manual. The table is `keymap` in `src/plat_gint.c`.

| R47 key (position) | CG50 key |
|---|---|
| softkeys (F1..F6) | F1 .. F6 |
| top row (keys 1..6) | X,θ,T · log · ln · sin · cos · tan |
| STO · RCL · R↓ · DRG · **f** · **g** (7..12) | a b/c · F↔D · ( · ) · , · → |
| **f** (11) | SHIFT (as well) |
| **g** (12) | ALPHA (as well) |
| ENTER (13) | EXE |
| x⇄y (14) | x² |
| CHS (15) | (−) |
| EEX (16) | ×10^x |
| ← backspace (17) | DEL |
| XEQ (18) | OPTN |
| ↑ (23) · ↓ (28) | ▲ · ▼ |
| EXIT (33) | EXIT, AC/ON |
| R/S (36) | VARS |
| top-row key 4 (y^x) | ^ (as well) |
| digits, `.`, + − × ÷ | the same keys |

The CG50's own:

| CG50 key | does |
|---|---|
| MENU | saves C47's state and goes to the main menu; C47 is as you left it when you come back |
| SHIFT + AC/ON | saves C47's state and switches the calculator off |

◀ and ▶ do nothing yet.

This is a first mapping, to be tried on the calculator and changed.
