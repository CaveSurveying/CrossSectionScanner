# CrossSectionScanner plan

Started 2026-11-19. Plan stage only, no code yet.

## Aim

A hand-held device that takes a single cave passage cross section with a 2D lidar when a button on the touchscreen is pressed. The user then draws the passage outline round the scan on the screen. Later an IMU tells the user when the scan plane is truly vertical (or horizontal) and its orientation is recorded with the scan.

The device should talk to SexyTopo and other cave surveying software. For now this is only something to keep in mind when making decisions (data formats, radio, storage).

## Hardware

### Display and controller

"Cheap Yellow Display" ESP32-2432S028 (CYD).

- ESP32-WROOM-32, dual core 240 MHz, 520 KB RAM, 4 MB flash, no PSRAM
- 2.8" 320x240 TFT. Ours has micro USB and USB-C. The Home Assistant setup (~/Projects/PlayDisplay-touch.yaml) found it works as an ILI9342 panel (native landscape, ESPHome preset ESP32-2432S028-9342, colours not inverted). The ID test (tests/display_id) read ID 81 81 B3 on 2026-11-19, with the ILI ID register empty, so the controller is an ST7789 revision. The ILI9342 settings probably worked because the two share most basic commands. The project firmware will use an ST7789 driver. Checked by eye with LovyanGFX Panel_ST7789, colours not inverted, RGB order correct, rotation 0 is portrait 240x320 the right way up with the USB at the bottom (rotations 1 and 3 give landscape if wanted).
- Pins in use, from that setup. Display SPI CLK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 21. Touch on its own SPI bus CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36. SD card SPI 5, 18, 19, 23.
- XPT2046 resistive touch, good with a stylus, works through a thin bag and with wet or gloved fingers better than capacitive
- micro SD slot, RGB LED, LDR, speaker connector
- Bluetooth Classic and BLE, Wi-Fi
- Few free GPIOs. Free on connectors are GPIO 35 (input only) and 22 on P3, 22 and 27 on CN1. GPIO 21 on P3 is the backlight. P1 is GPIO 1/3, shared with USB. GPIO 26 (speaker) and 4, 16, 17 (RGB LED) can be taken over if needed.
- Lidar lead (Slamtec C1 datasheet v1.1), XH2.54 5-pin socket, red VCC 5 V (4.8 to 5.2 V, 230 mA running, 800 mA at start-up), yellow TX (lidar out, 3.3 V), green RX (lidar in), black GND.
- Old CYD walk-about wiring (proposed, pin order on the board connectors still to read off the silkscreen). Red to P1 VIN, black to P1 GND, yellow to CN1 IO27, green to CN1 IO22. Needs two board plugs. The earlier idea was lidar TX to GPIO 35, lidar RX from GPIO 22 (UART2). That leaves 27 plus 26 or the LED pins for the IMU (I2C needs two, a BNO085 in UART-RVC mode needs one).

### Lidar

Slamtec RPLIDAR C1, label C1M1-R2 (photo in this folder). Kit includes a USB adapter board (CP2102N, shows as /dev/ttyUSB1 here, the CYD is the CH340 on /dev/ttyUSB0). Checked 2026-11-19 over USB, GET_INFO gives model 0x41, firmware 1.02, hardware 18, serial matching the label, GET_HEALTH good. The USB board also lets a desktop script grab scans for testing before the ESP32 code exists. Figures from the Slamtec datasheet (v1.0/v1.1).

- DTOF, range 0.05 to 12 m, accuracy about ±30 mm
- 5 kHz sample rate, 8 to 12 Hz scan (10 Hz typical), so about 500 points per revolution at 0.72°
- 5 V supply, about 230 mA, motor and ranging from the same supply
- TTL UART 460800 baud 8N1, 3.3 V logic so it can go straight to the ESP32
- Slamtec RPLIDAR serial protocol (commands such as SCAN, STOP, GET_INFO, GET_HEALTH), Slamtec's open source SDK documents it
- IP54 housing, rated for 40,000 lux ambient light

To find out by testing

- whether the ESP32 keeps up with 460800 baud while drawing (UART2 with a large receive buffer should)
- behaviour on wet, dark or shiny rock and in mist
- motor start-up current and how long it takes to settle after a STOP and restart

### IMU (later)

Candidates with on-chip fusion, which saves ESP32 work and gives a stable tilt and heading

- BNO085 (good fusion, I2C or UART)
- BNO055 (older, simple)
- ICM-20948 with software fusion (cheaper, more work)

The lidar motor and its magnet sit close to the compass, so heading needs calibration in the final layout and a check of how much the motor disturbs it. Tilt (from the accelerometer) is much less affected.

### Power and case (later)

- LiPo with charger and 5 V boost (the CYD has no battery charging)
- The E32R24P has a 2-pin battery connector with a TP4054 charger from USB-C (about 300 mA, R27 3.3 kΩ), automatic switch to battery when USB is removed, and battery voltage on IO34. On battery the board runs from the cell through its 3.3 V regulator, and P2's 5 V (USB only) is off, so the lidar needs a 5 V boost converter from the battery (at least 1 A for the 800 mA start-up). Route for the finished device is one cell, board charging over USB-C, boost to the lidar, battery level on screen. Check battery lead polarity against the board before plugging in.
- waterproof, mudproof case with a clear window for the lidar, condensation is a known lidar problem in caves
- a physical button as well as the screen button is worth considering for muddy hands

## How a scan works

1. User stands at (or a known offset from) a survey station and holds the device with the scan plane across the passage.
2. With the IMU, a level indicator on the screen shows tilt from vertical, and capture is allowed or flagged when within a set tolerance.
3. Press the on-screen button. One full revolution (or a few, averaged) is captured with the IMU reading at that moment.
4. The points are shown on the screen, scaled to fit, with the device's position marked and up shown as up.
5. The user draws round the scan with a stylus, closing the outline where the lidar could not see (behind boulders, deep water, beyond range).
6. The user enters or picks the station name and the direction the section looks. Save to SD.

## Data to record per section

- raw points (angle, distance, quality) as received
- time, station name, offset from the station if any
- IMU orientation (tilt, roll, heading) and calibration state
- drawn outline as a polyline in the same coordinates as the points
- device and firmware version

Raw data is always kept, so processing can be redone later off the device.

## Links to surveying software (keep in mind now, build later)

- Each lidar point with the IMU heading and tilt becomes a splay (distance, compass, clino) from the station. This is the most portable form, since SexyTopo, Therion, Survex and others all take splays.
- SexyTopo already takes shots over Bluetooth from DistoX type instruments (Bluetooth Classic SPP) and several BLE instruments. Sending a section as a burst of splays through one of those protocols may be possible, but several hundred splays per section may be too many, so a reduced set may be needed.
- The drawn outline suits a Therion cross section scrap (th2) or SexyTopo sketch, or a simple SVG/JSON exchange file.
- File export from SD (CSV of splays, JSON with everything) is the first step and needs no radio work.

The data model should be designed so all these outputs come from the same saved record.

## Software

- PlatformIO with the Arduino framework on the ESP32
- LovyanGFX or TFT_eSPI for the display and touch, LVGL only if the UI grows
- lidar decoding in its own module, display and touch in another, storage in another, so the IMU and radio can be added without rework
- a small desktop script to read the saved files and plot them for checking

## Stages

0. On the computer over the lidar's USB board, tools/scan_once.py takes one revolution, saves raw JSON to scans/ and plots it. First scan 2026-11-19 gave 436 points (0.83° spacing, about 11.5 Hz), 384 with a return. Checked against the room. 0° points directly away from the cable, angles run clockwise seen from the top of the lidar, and plotting x = d sin θ, y = d cos θ gives a true (not mirrored) picture viewed from above.
1. Show a scan on the screen before wiring. firmware/ (PlatformIO, shared display settings in firmware/lib/cyd_display) takes a scan over USB from tools/send_to_screen.py and draws it like the computer plot. Opening the CH340 port resets the ESP32 even with DTR/RTS held off, and the board drops data unless the sender pauses for an OK every 100 lines.
   SCAN button on screen, with send_to_screen.py --listen taking the scan on the computer. Touch calibration on the board (cyd_display), five crosses, four corners fitted by least squares and the centre as a check (8 px), each press held and averaged, saved in flash. Runs if nothing is stored, if the screen is held at power-on, or from a Cal button held for 1 s.
   Touch fault found 2026-11-19. Calibration failed its centre check (60 to 86 px out). The touch map test (tests/touch_map, tools/touch_map.py, results in tests/touch_map/results) showed raw x is linear and steady, but raw y is crushed towards the bottom, worst on the right (right column reads 906, 3365, 3419, 3606, 3884 from top to bottom against 300 to 3741 on the left). Same with the protective film removed, so the touch layer of this board is faulty. A replacement board is needed for drawing round scans. Stay with resistive touch (works with gloves, stylus, wet fingers).
   Replacement in use, LCDwiki E32R24P (2.4" IPS ST7789, resistive XPT2046, ESP32-32E, one USB-C, CH340C, battery voltage on IO34). Same display and touch pins as the CYD, build with env e32r24p (-DBOARD_E32R24P). Touch map 2026-11-19, straight line fit at most 4 px out, mean 2 px. Five cross calibration passed (centre 4.1 px), colours right with invert on, SCAN button tested with 9 scans. Free pins IO27, IO35 (input only), IO1/3 (USB serial), 18/19/23 if SD and SPI connector unused, RGB LED on 22/16/17.
   Lidar on the board, written 2026-11-19 (firmware/src/lidar.cpp), wiring in docs/WIRING.md (E32R24P, two plugs, lidar TX to IO19 and RX from IO27 on the SPI connector, 5 V and GND from P2, SD slot left empty so scans will go in internal flash with LittleFS). SCAN scans on the board when the lidar answers and sends the raw scan to send_to_screen.py --listen, which saves it with source board. With no lidar it falls back to asking the computer. Not yet tested with the lidar wired.
   Then wire the lidar to the CYD, read and decode a revolution on the ESP32, show points on the screen.
2. Capture button, freeze and display a single scan, save raw data to SD.
3. Draw round the scan with the stylus, edit (undo, clear), save the outline.
4. Station name entry and export (CSV splays, JSON).
5. Add the IMU, level indicator, record orientation.
6. Bluetooth link to SexyTopo (protocol to be chosen).
7. Battery and case, cave trials.

## Open questions

- Is the CYD in PlayDisplay-touch.yaml this same board (assumed yes)?
- "Horizontal" sections, is this for plan slices at a station, e.g. for a passage running vertically like a shaft?
- Should a section be one revolution or several averaged? Five-turn tests 2026-11-19 gave 400 to 456 points a turn with about 12% no return, mostly at the same angles each turn. The motor slows over the first few seconds, but the user says there is no need to wait for it, and 400 points is already far more than needed. A thinned set for export and splays, raw kept. How to thin is to be decided from what we get in the cave.
- Which IMU, and is heading needed or only tilt?
- Where is the device held relative to the station, and how is that offset recorded?
