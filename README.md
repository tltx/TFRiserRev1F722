# TFRiserRev1F722

> **Fork notice**: This is Tore Lundqvist's fork of
> [arkadiuszmakarenko/TFRiserRev1F722](https://github.com/arkadiuszmakarenko/TFRiserRev1F722).
> Changes here are being submitted upstream as pull requests; this fork
> exists so users can have the changes today. Bug reports about
> behaviour added in this fork belong on this repo, not on upstream.

STM32F722 firmware for the TF CD32 Riser — a board that lets you use
modern USB gamepads, keyboards and mice on an Amiga / CD32.

## Requirements

> **⚠️ This version requires the companion CPLD code to work.**

The riser's CPLD is the external address decoder: it watches the Amiga
bus and pulses the STM32's interrupt line whenever an access falls in the
riser's address window, so the firmware knows when to read/write the bus.
Without the matching CPLD programming the STM32 receives no bus events and
the riser does nothing — the Amiga-side tool will report the board as
"not detected". Flash this firmware **and** program the CPLD with its
corresponding logic before expecting the board to function.

## Contents

- `Core/`, `Drivers/`, `Middlewares/` — the STM32F722 firmware (USB HID
  host + Amiga bus interface).
- `AmigaTools/` — `risergamepad`, the Amiga-side configuration tool, plus
  `risergamepad.readme` (the end-user guide covering the USB feature,
  compatible hardware, and how to map a gamepad).
