# bl616_usb2uartjtag

Attempt to port bl616_usb2uartjtag from RV-Debugger-BL702 to the BL616

## Compile

- BL616/BL618

```
make CHIP=bl616 BOARD=bl616dk
```


## Flash

```
make flash CHIP=chip_name COMX=xxx # xxx is your com name
```