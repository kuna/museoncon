Muse-on Controller
==================

A revival of muse-on controller to a fully functional IIDX gamepad with LED equipped.

Composed of:
* Model (top board)
* Firmware (RP2350)
* Circuit
* Configurator (web page)

NOTE: This project is still working and under experiment!!

## Model preview

![Model](model/preview.png)

## Circuit preview

![Circuit](circuit/top.png)

## Configurator

`configurator/index.html` changes the output mode (GAMEPAD / KEYBOARD /
COMPOSITE) and shows live input: buttons before and after debounce,
turntable position, sensor levels, and invalid quadrature steps.

Open the file directly in Chrome or Edge on a desktop (Web Serial is not
available in Firefox or Safari), plug in the controller, and press 연결.
No server or install is needed. The protocol is documented in
[firmware/README.md](firmware/README.md#configurator-port).
