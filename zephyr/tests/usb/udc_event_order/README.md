# USB UDC Event and Power-State Tests

## Purpose

This multi-DUT Twister suite exercises USB VBUS and bus-state transitions with
a NUCLEO-H755ZI-Q (M7) host and an nRF52840 device. It contains two separate tests:

- `c2usb.usb.udc_event_order` runs on the nRF52840 and uses the native nRF
  USBREG VBUS detector to check VBUS, reset, suspend, resume, and removal events.
- `c2usb.usb.udc_power_state` remains a separate GPIO-based test on the nRF52840
  DK and runs alongside the event-order test with the command below.

The GPIO-configured test verifies GPIO-driven presence and c2usb state handling;
it does not verify the USBREG comparator. The event-order test below uses real
USB VBUS and does not configure `vbus-gpios`.

## Hardware and Wiring

The Nucleo host fixture uses the USB FS Micro-AB connector CN13. On the MB1363
board, the STMPS2141 switch U18 controls connector VBUS from GPIO PD10. Its EN
input is active-low and pulled up by R59, so PD10 high disables VBUS and low
enables it. GPIO PA9 senses connector VBUS and GPIO PG7 reads the active-low
overcurrent output. Connect CN13 to the nRF52840 DK USB connector so its native
VBUS detector sees the switched host VBUS. During testing, CN13 must be the only
source driving VBUS on the DK's USB connector.

Both boards' consoles and probes are identified by their stable USB serial paths
in the hardware map. The nRF52840 DK uses J-Link probe `001050254062`.

## Hardware Map

The suite's [`hardware-map.yaml`](hardware-map.yaml) uses the attached probe IDs
and stable VCOM paths. The Nucleo console is ST-LINK VCP; the nRF52840 DK console
is J-Link VCOM. The map selects STM32CubeProgrammer for the Nucleo and J-Link
for the DK.

## Running

Run both scenarios from the c2usb repository root. Set `ZEPHYR_BASE` to the
active Zephyr checkout.

```console
export ZEPHYR_BASE=/path/to/zephyr
export HARDWARE_MAP=zephyr/tests/usb/udc_event_order/hardware-map.yaml

west twister -T zephyr/tests/usb/udc_event_order \
  -p nucleo_h755zi_q/stm32h755xx/m7 -p nrf52840dk/nrf52840 \
  --device-testing --hardware-map "$HARDWARE_MAP" -j 1
```

Twister builds and flashes the Nucleo host application and the required nRF52840
DK device application for each scenario.

## Hardware Run Results

The host initialization timeout was caused by the FS PHY not being selected
before the DWC2 core reset. The generic UHC driver selects it after reset; the
fixture now preselects `GUSBCFG.PHYSEL`. It also applies the STM32H7 FS USB power
sequence and disables low-power sleep for the USB2OTGHSULPI clock.

The schematic also showed that U18's EN polarity was reversed in the fixture.
The host now drives EN high for off and low for on, and configures PA9 as an
input. With the CN13 cable connected, live readings were:

- Off: `EN_RAW=1`, `VBUS_SENSE_RAW=0` before enabling VBUS.
- On: `EN_RAW=0`, `VBUS_SENSE_RAW=1`.
- Off again: `EN_RAW=1`; an early sample remained high, but a later settled check
  read `VBUS_SENSE_RAW=0`.

Wait for PA9 to settle after switching VBUS off before treating an immediate high
sample as evidence that the switch is still enabled.

Both scenarios build and flash, and `host init` succeeds. The latest full
hardware run reports 0 of 2 test cases passing:

- `c2usb.usb.udc_event_order` reaches the first VBUS assertion but records two
  events (`vbus_ready`, `suspend`) where the test expects only `vbus_ready`.
- `c2usb.usb.udc_power_state` starts in the expected `L3_OFF` state, but times
  out after two of the three expected states in the first on/off cycle.

The VBUS polarity and PA9 sensing are validated; the complete event and
power-state sequences still need verification.

## Shell Commands

The Nucleo host fixture provides `host init`, `host vbus on|off`, `host reset`,
`host suspend`, and `host resume`.

The nRF52840 DK native USB fixture provides `events status`, `events clear`, and
`events assert <events...>`. `events status` reports raw USBREG VBUS and READY
state, enabled USB interrupt mask, pending POWER events, and recorded UDC event
history. The separate c2usb GPIO fixture provides `power state`, `power clear`,
and `power assert <states...>`.
