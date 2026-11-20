# Lidar to E32R24P wiring

RPLIDAR C1 (C1M1-R2) to the LCDwiki E32R24P 2.4" board. Written 2026-11-19 from the Slamtec C1 datasheet v1.1 and the E32R24P user manual (both in this folder).

Two board plugs, P2 for power and the SPI connector for data. This uses two of the SD card's SPI pins, so **leave the SD slot empty**. Scans are stored in the board's own flash.

## Lidar lead

XH2.54 5-pin socket on the lidar lead, 4 pins used, in this order.

| Colour | Signal | Direction | Level |
|---|---|---|---|
| Red | VCC | power in | 5 V (4.8 to 5.2 V), 230 mA running, up to 800 mA at start-up |
| Yellow | TX | lidar out | 3.3 V |
| Green | RX | lidar in | 3.3 V |
| Black | GND | | |

## Board connectors used

Both are 4-pin 1.25 mm pitch.

| Connector | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|
| P2 serial | 5 V (from USB-C) | GND | TX0 | RX0 |
| SPI | MOSI IO23 | MISO IO19 | CLK IO18 | CS IO27 |

## Wiring

| Lidar wire | Board connector | Pin | Board signal |
|---|---|---|---|
| Red (5 V) | P2 serial | 1 | 5 V |
| Black (GND) | P2 serial | 2 | GND |
| Yellow (lidar TX) | SPI | 2 | IO19, board receives |
| Green (lidar RX) | SPI | 4 | IO27, board sends |

Leave P2 pins 3 and 4 and SPI pins 1 and 3 empty.

## Checks before plugging in

1. Find pin 1 on each board connector from the silkscreen and check every signal name against the table above.
2. With the board powered from USB-C and nothing plugged in, measure P2 pin 1 to pin 2. It should read about 5 V.
3. Red goes only to P2 pin 1. Nothing else should see 5 V.
4. Yellow and green must not be swapped. Lidar TX (yellow) goes to the board's receive pin IO19 (SPI pin 2).
5. No SD card in the slot.

## Notes

- Power comes in through the USB-C socket, from the computer or a power bank. P2's 5 V is taken from USB, so a battery on the board's battery connector alone will not run the lidar (see the power section of PLAN.md).
- The SPI connector has no GND pin. The lidar's ground goes to P2 pin 2.
- P2's TX0 and RX0 are the USB serial lines, left free for flashing and debugging.
- The firmware for this wiring is built with the `e32r24p` environment (`firmware/platformio.ini`).
- Earlier option, not used. Three plugs with lidar TX on the expansion connector (IO35) kept the SD card free.
