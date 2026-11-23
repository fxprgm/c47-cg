# c47-cg

A port of the [C47](https://gitlab.com/rpncalculators/c43) RPN calculator firmware to the
Casio fx-CG50 (and fx-CG50 family) as a gint add-in.

Status: just starting. Nothing to install yet.

## Plan

- Build the upstream C47 core unchanged with sh-elf-gcc, on top of a small platform layer
  (screen, keys, timers, files, clock) written on gint.
- Use the same fonts as upstream, generated from its TrueType files with its own tool.
- Fit the 400×240 layout to the CG50's 396×224 screen with a small set of patches kept on top
  of upstream: status bar and soft menus unchanged, stack lines 36 → 32 px.

## License

GPL-3.0, as the upstream firmware. This is an independent community project, not affiliated
with SwissMicros.
