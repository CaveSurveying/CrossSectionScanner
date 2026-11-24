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

### Next screen, LCDwiki E32R35T

Bought 2026-11-22, as the 2.4" E32R24P is small. Same LCDwiki family and ESP32-32E module, from its product page (lcdwiki.com/3.5inch_ESP32-32E_Display, the schematic and user manual still to read for connector pin order before wiring).

- 3.5" 480x320 TN panel (not IPS, so colours shift more off angle), ST7796 on 4-wire SPI (IO15 CS, IO2 DC, IO14 CLK, IO13 MOSI, IO12 MISO, IO27 backlight)
- XPT2046 resistive touch on the same SPI bus (CS IO33, interrupt IO36). E32N35T is the same board with no touch.
- 2-pin battery socket with a TP4054 charger from USB-C (about 290 mA), battery voltage on IO34
- SPI plug IO21, IO18, IO19, IO23 (shared with the SD slot, left empty), for the lidar's serial as on the E32R24P
- I2C plug, SCL IO25, SDA IO32, for the GY-85
- IO35 and IO39 input only, free, e.g. for a physical button
- Firmware port: a new board in cyd_display, touch calibration, and a screen layout that scales with the screen size (the layout now assumes 240 px wide)

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

GY-85, chosen 2026-11-22 (the user has one). Raw sensors on I2C, no fusion on board. ADXL345 accelerometer (tilt, about 0.25 degree resolution), ITG-3205 gyro, and an HMC5883L compass, though many boards have a QMC5883L clone instead (different address and registers, read the chip marking, L883 is HMC, DA 5883 is QMC). No clock stretching, so it works on the ESP32's I2C.

- The device is held still for a scan, so tilt comes from the accelerometer and heading from the tilt-compensated compass. The gyro is only for smoothing if needed.
- Calibration in the finished case, as survey instruments (DistoX) do, from readings in many orientations. It solves two things at once. Sensor calibration (compass hard and soft iron from the motor magnet, screws and battery cans, accelerometer offsets and scale) and alignment (how the board sits against the lidar's 0 degrees and scan plane), which is needed with any IMU. Fixed and repeatable, only changed by recalibrating.
- Interference that changes (motor current, the spinning lidar magnet, battery current, backlight) can't be calibrated out. Mount the GY-85 as far from the motor and battery as the case allows, read the compass with the lidar motor stopped just before the scan, and first log the compass with the motor on and off and the backlight up and down to see how big it is.
- A few degrees of heading is enough for a section. SexyTopo can work the direction out from the legs anyway. Tilt matters more, to know the scan plane is vertical.
- Others considered. BNO085, on-chip fusion and self-calibration, but its I2C clock stretching doesn't work well with the ESP32 (UART or SPI would), and its calibration can't be seen or controlled and adapts in use. BNO055, similar I2C trouble. ICM-20948, raw sensors like the GY-85.

### Power and case (later)

- LiPo with charger and 5 V boost (the CYD has no battery charging)
- The E32R24P has a 2-pin battery connector with a TP4054 charger from USB-C (about 300 mA, R27 3.3 kΩ), automatic switch to battery when USB is removed, and battery voltage on IO34. On battery the board runs from the cell through its 3.3 V regulator, and P2's 5 V (USB only) is off, so the lidar needs a 5 V boost converter from the battery (at least 1 A for the 800 mA start-up). Route for the finished device is one cell, board charging over USB-C, boost to the lidar, battery level on screen. Check battery lead polarity against the board before plugging in.
- waterproof, mudproof case with a clear window for the lidar, condensation is a known lidar problem in caves
- a physical button as well as the screen button is worth considering for muddy hands
- Decided 2026-11-24. Start with one 18650 (2800 mAh), the box sized for two in parallel later. Parallel only, never series (the board's battery socket and TP4054 are for one cell). Not protected cells, the user considers their protection circuits a fire risk. The 3.5" E32R35T is 55.5 × 101.5 mm, 5.8 mm deep at the screen (LCDwiki), more behind for the ESP32 module and plugs, so measure before buying a box.
- Push button, wanted 2026-11-24. Long press turns the unit on and off, short press scans when on. Needs a soft power circuit (or deep sleep with the boost converter switched off) and a free GPIO, still to design.

## How a scan works

1. User stands at (or a known offset from) a survey station and holds the device with the scan plane across the passage.
2. With the IMU, a level indicator on the screen shows tilt from vertical, and capture is allowed or flagged when within a set tolerance.
3. Press the on-screen button. One full revolution (or a few, averaged) is captured with the IMU reading at that moment.
4. The points are shown on the screen, scaled to fit, with the device's position marked and up shown as up.
5. The user draws round the scan with a stylus, closing the outline where the lidar could not see (behind boulders, deep water, beyond range).
6. The user accepts the scan, giving the station (picked from the list SexyTopo sent, or typed when used alone), vertical or horizontal, and for now whether they faced on or back along the survey. Saved in the board's flash, and sent to SexyTopo when connected.

## Data to record per section

- raw points (angle, distance, quality) as received
- time, station name, offset from the station if any
- vertical or horizontal, facing on or back (until the IMU gives heading)
- IMU orientation (tilt, roll, heading) and calibration state
- drawn outline as a polyline in the same coordinates as the points
- device and firmware version

Raw data is always kept, so processing can be redone later off the device.

## Link with SexyTopo

Outline agreed 2026-11-20. XScan is the working name for the scanner. It must work on its own, and the SexyTopo link is an addition. The SexyTopo side is an upstream change, worked on in the user's fork.

- Two way, over BLE (chosen 2026-11-23). NimBLE on the ESP32 is much lighter than Classic and BLE together on a board with no PSRAM, a scan is only a few KB, and SexyTopo's newer instruments already use BLE (Nordic BLE library). Each message carries a length and a checksum.
- SexyTopo holds only one instrument connection (SexyTopoActivity comms), so XScan needs a second connection beside the DistoX rather than a new InstrumentType.
- Off by default in SexyTopo, so nothing changes for others. A Cross-section scanner switch in the Instruments settings (preferences_instruments.xml, read through GeneralPreferences with default false, as pref_auto_reconnect). When off, there is no scanner section in the device screen, no scanner menu items, no connecting to a scanner and no points drawn behind sections. An existing <survey>.xscan.jsonl must survive saving with it off (still to check whether SexyTopo's save removes files it doesn't know).
- ScannerType in SexyTopo, beside InstrumentType, with its own name prefix and communicator class, so scanners are never taken for instruments, the instrument code is left alone, and other scanners can be added later. One entry (XScan) for now. The instrument list in the device screen leaves scanners out.
- XScan advertises a fixed BLE name prefix, XScan- followed by part of the board's serial, for ScannerType to recognise.
- SexyTopo to XScan. The survey name and its stations on connecting, then each new station as it is created (renames and deletions too). XScan keeps the last list in flash, so stations can still be picked when the link drops, and offers the newest station first. A rename changes the station on its scan, a deletion clears it and the scan stays on XScan.
- One scan per station, as SexyTopo allows one cross section per station, either vertical or horizontal (Sketch.getCrossSectionDetail, create greyed out once there is one).
- Accept on XScan. A scan is sent only once the user accepts it, giving the station, vertical or horizontal, and facing on or back. The station is picked from SexyTopo's list, and typed only when no survey has been received. If the station already has a scan, XScan asks whether to replace it, and the old scan loses its station but is kept. Only scans with a station are sent. Others stay on XScan (board_link.py --fetch still gets them). A typed station SexyTopo doesn't have gets an error, and the scan waits until its station is changed.
- XScan to SexyTopo. A section message with the scan name, station, orientation, facing, raw points, sketch, sketch revision and checksum.
- SexyTopo checks it (format version, checksum, station exists, sketch reads) and replies OK or an error with the reason. XScan shows each scan as new, accepted, sent, acknowledged or error. Anything not acknowledged is sent again on the next connection, and SexyTopo ignores one it already has (same scan name and revision).
- Storage in SexyTopo (changed 2026-11-23 to follow the exchange format). <survey>.xscan.jsonl beside the survey, one line per station with a scan, in the exchange format below. A new scan for a station replaces its line and the file is rewritten (it is small). The drawing goes into that station's cross section in plan.json, as to_sexytopo.py does now, with the revision rule under Sketches. The points are not added to the survey as legs. They are loaded only when the cross section is opened and drawn behind it, so hundreds of points don't slow the survey views. Splays made from them for export can come later.
- With no direction from the scanner, SexyTopo takes a vertical scan as lying in the plane it uses now for that station's cross section (CrossSectioner), turned round if the user faced back. A horizontal scan is a plan slice, but with no heading it can't be put north up, so the SexyTopo user rotates it until the IMU gives heading.
- Sketches. Drawn on XScan, and can be changed in SexyTopo. The user sends a changed sketch again from XScan, with a new revision number and the revision it was based on. SexyTopo replaces its copy only if it hasn't been edited there since that revision. If it has, SexyTopo keeps its own, replies conflict and asks its user whether to replace. Sketches are not sent back to XScan for now. Two-way sketch updates only if this proves not enough.
- File export from the board's flash (board_link.py --fetch) stays, for use without SexyTopo and for checking.
- Exchange format, discussed 2026-11-22. JSONL for SexyTopo and other drawing programs, one scan per line, so many scans can go in one file and be compressed. Each line has the scan name, time, station, orientation (heading, pitch and roll from the IMU, with direction and clino worked out from them), lidar details, the turn's samples and returns, and the points as [degrees, mm, quality] with no-returns left out. Anything not known is left out (not "-"), and the reading program works the direction out from the legs. Drawings stay in their own files, as they are edited and get revisions. The board keeps its own format, one file per scan, as LittleFS deletes the oldest scan when full and saves drawings again on each revision, which in one JSONL file would mean rewriting it all. The station and link state go in each scan's record. It is chosen for flash use and sending speed, and the receiving end (board_link.py now, SexyTopo later) writes the JSONL. board_link.py --fetch (or --jsonl alone) writes every board scan in scans/ to scans/xscan.jsonl as format XScan scan 1 (2026-11-23, tests/board_link/test_jsonl.py), with no station, facing or orientation until stage 4. Until then board_link.py --push SURVEY_DIR STATION=SCAN ... gives each scan its station by hand and writes <survey>.xscan.jsonl in a survey on the phone or emulator over adb, one line per station, after checking the stations are in the survey. Text for now as it is easy to debug. If flash or Bluetooth speed needs it at stage 6, binary points (the lidar's own 1/64 degree, 1/4 mm and quality, 5 bytes a point, about a third of the text) and compact drawing JSON (about 40% smaller).

### SexyTopo side

On the user's fork, branch SectionScanner from upstream main e16dc21a (2026-11-23). Test survey on the emulator's SD card, /storage/0000-0000/Test2Surveys/SectionScanner (a copy of IsoDate, 26 stations), with scans pushed by board_link.py --push.

Found in SexyTopo. Saver writes only the four survey files (SurveyFile.ALL_DATA_TYPES) and deletes nothing, so <survey>.xscan.jsonl survives saving with the setting on or off. Save As writes only those four, so scans are left behind, and lines are found by station name, so a rename loses its scan. A station's cross section can be in the plan or the elevation sketch (CrossSectionActivity), and the scan belongs to the station, so it is drawn in either. The cross section view is in metres from the station, x right, y down, as the scanner's drawings.

Link 2a, show a station's scan behind its cross section, reading only (agreed 2026-11-23).

1. Setting. Cross-section scanner switch in preferences_instruments.xml, pref_section_scanner, default off, read by GeneralPreferences.isSectionScannerOn(), translated into de, fr, es, it, pl, pt.
2. File type. SexyTopoConstants.SCANS_EXTENSION xscan.jsonl and SurveyFile.SCANS, left out of ALL_DATA_TYPES so save, autosave and load are unchanged.
3. Model. Immutable SectionScan in model/scan (scan name, station, facing, points as degrees, mm, quality, samples and returns left until needed), giving its points in the section's frame, x = -d sin θ, y = -d cos θ in metres (scan_view.cpp addPoint, y turned to down), x mirrored when facing back.
4. Reader. ScanJsonlTranslater reads the file into station name to scan. Unknown format or bad JSON lines are logged and skipped, unknown fields ignored, the last line for a station wins.
5. CrossSectionActivity reads the file when a section opens and the setting is on. No file, no points. A file that can't be read is logged and the section opens anyway.
6. CrossSectionView has its own onDraw, so SketchView is left alone. It draws the station's points as small grey dots of fixed screen size, and auto-fit uses them when there are no splays.
7. Horizontal sections are drawn unrotated until there is heading or a way to turn them by hand.
8. Table-driven unit tests, no mocks. The reader (full line, required fields only, unknown format, bad JSON, a station twice, blank lines, extra fields) and the point frame (0, 90, 180, 270 degrees, facing on and back).
9. Check on the emulator. b0004-001's drawing at station 5 with to_sexytopo.py, sections at 6 and 7 made in SexyTopo. Off changes nothing, on shows the points behind the sections, lined up with the drawing at 5.

Written 2026-11-23 (ScanJsonlTranslater, SectionScan, CrossSectionView.drawScan, 4 new tests, 1056 unit tests pass). Drawings from b0004-001 and b0002-003 added at stations 5 and 6 of the emulator survey, to be checked on the emulator.

Link 2b, keep scans with their stations. Load the file with the survey, hold each scan against its Station, and rewrite the file on save (and Save As) with the current names. Lines for stations that no longer exist are kept as they are.

Written 2026-11-23. Renames keep the Station object (SurveyUpdater.renameStation), so SurveyScans holds scans against it and they follow. Loader reads the file (or its autosave when restoring) after the survey files, and Saver writes it, and its autosave, after them, only if the survey has scans. Kept in step whether the setting is on or off (user 2026-11-23), the setting only shows or hides the points. Each scan keeps the line it was read from and only station is changed on writing, so lidar, board and anything else SexyTopo doesn't read survive. Lines with no matching station, that can't be read, or replaced by a later line for the same station are kept as they are and written first, so a station's own scan stays its last line. A deleted station's scan is written with the name it last had. 9 tests in ScanJsonlTranslaterTest and SectionScanTest, 1061 unit tests pass. Rename and Save As checked on the emulator 2026-11-23. Save As first wrote <survey>.xscan.jsonl.txt, as Android adds .txt to a new text/plain file with a name it doesn't know, so the scans file now uses application/octet-stream as the autosaves do. That workaround is now one constant, SexyTopoConstants.MIME_TYPE_NO_EXTENSION, used by the autosaves, the scans file and Survex export's .espec file (which had its own copy, SurvexConstants.ESPEC_MIME_TYPE, now removed).

Known limit while board_link.py --push is used. Pushing while the survey is open in SexyTopo is lost on its next save, and restoring an autosave uses the scans' autosave. Push with the survey closed, or reopen it after pushing. Goes away with the Bluetooth link, as SexyTopo is then the only writer.

Station renames, to be settled with the link (2026-11-23). SexyTopo is the authority on station names, so it updates the scanner rather than the other way round. The scan name is the lasting identity, as SexyTopo has no station ids. SexyTopo renames in its own file (2b), sends a rename while connected, and on connecting sends the scan name and current station of every scan it holds, so a rename made while apart reaches the scanner too. A scan accepted on the scanner but not yet sent when its station is renamed gets "no such station" and the user picks the station again.

### Link stages

Planned 2026-11-23, each with its own go-ahead. Named Link 2a to Link 5 so they aren't confused with the scanner's own stages 0 to 7 below. Link 2a and 2b SexyTopo showing and keeping scans (above), Link 3a the protocol spec, Link 3b SexyTopo's message handling in plain Java, Link 4 the Bluetooth connection in SexyTopo (ScannerType, second connection, conflict question to the user), Link 5 the firmware (NimBLE, accepting a scan with its station, send queue and scan states, which is the scanner's stage 4 and part of its stage 6).

Link 3a, protocol spec in docs/LINK.md.

- BLE service in the Nordic UART Service layout (user agreed 2026-11-23). One write characteristic SexyTopo to XScan and one notify characteristic XScan to SexyTopo, so either end can be tested with nRF Connect on a phone or bleak on the computer before the other exists. XScan is still recognised by its XScan- name.
- Framing (agreed). Each message is one JSON object after a text header line, XS1 <length> <crc32 hex>, then a newline and the JSON. The receiver joins BLE packets until it has length bytes and checks the CRC. MTU asked for at 517, so a 6 KB scan is about 12 packets.
- SexyTopo to XScan. hello (protocol and app version) on connecting. survey (survey name, stations oldest to newest, the scan name and current station of every scan SexyTopo holds) on connecting and when another survey is opened. station_added, station_renamed, station_deleted as they happen. reply (scan, revision, ok, error or conflict, reason) after each section.
- XScan to SexyTopo. hello (protocol, firmware, board serial). section (the scan as an exchange format line with station, vertical or horizontal and facing, the drawing in XScan sketch 1, its revision and the revision it was based on).
- Error reasons bad_checksum, unknown_format, no_station, bad_points, bad_sketch.
- tests/link, good and broken example frames that the Java and C++ tests must both read the same way.

Written 2026-11-23. docs/LINK.md (protocol 1), tools/xscan_link.py (frame and FrameReader, the reference for framing), tests/link/make_frames.py writing 22 example frames and cases.json (what each must give), test_frames.py reading each whole, a byte at a time, in 20-byte and in 514-byte packets (92 checks pass). Details settled in writing. The name is XScan- and the last 6 hex digits of the MAC. JSON up to 256 KB, header up to 32 bytes. A bad header drops bytes up to the next "XS1 ", a bad CRC or JSON with no type drops the frame and XScan's gets a reply error without a scan so it is sent again. A part frame is dropped after 10 s with nothing more. Unknown types and fields are ignored. section always gives orientation and facing, sketch left out with no drawing, revision 0 then.

Link 3b, SexyTopo message handling, no Android or Bluetooth.

1. ScannerFrames builds a message's bytes and joins incoming packets into messages, checking length and CRC.
2. ScannerMessages builds and reads each message type, reusing ScanJsonlTranslater for the scan line and SketchJsonTranslater for the drawing.
3. ScannerSession handles a received section. Check its format, that the station exists and that the points and drawing read. The same scan name and revision already held gets OK and no change. Otherwise the scan goes on the station in SurveyScans, replacing any earlier one, and the drawing goes into the station's cross section.
4. Drawing rules. With no section at the station SexyTopo makes one (user agreed 2026-11-23), a vertical one at CrossSectioner's angle, turned round if the scan faced back, placed beside the station on the plan as to_sexytopo.py does. With a section there, the drawing is replaced only if not edited in SexyTopo since the revision it was based on, otherwise the reply is conflict (asking the user whether to replace is Link 4).
5. Edited since. SexyTopo keeps the revision received and a hash of the drawing as received in its own JSONL line for the scan, and edited means the section's drawing no longer matches the hash, so plan.json's format is unchanged.
6. Table-driven tests, no mocks. Frames (whole, split across packets, two in one packet, bad CRC, too short), each message both ways, and session decisions (new scan, repeat, new revision, unknown station, drawing edited in SexyTopo, horizontal, faced back).

Written 2026-11-23 in SexyTopo's comms/scanner (ScannerFrames, ScannerMessages, ScannerSession). The shared frames are copied into app/src/test/resources/xscan-link with the commit they came from. SexyTopo's record of what it took is a sexytopo object (revision, sketch_hash, a SHA-256 of the drawing as SexyTopo writes it) added to its JSONL line for the scan. A scan sent with a new station moves there. A section with no drawing gets an empty section if the station has none, and leaves an existing drawing alone. A hand-made section with a drawing counts as edited, an empty one doesn't. If the orientation changes the section is replaced at the same place, otherwise only its drawing. A conflict's Outcome holds the change, so Link 4 can apply it if the user chooses the scanner's drawing. SketchJsonTranslater's sub-sketch reader, writer and isSketchEmpty made public for reuse. 24 tests in the scanner classes, 1076 unit tests pass.

Link 4, the Bluetooth connection in SexyTopo, planned 2026-11-23, 4a then 4b (user agreed).

Found in SexyTopo. The instrument is one static comms, and BleCommunicator is tied to DeviceActivity (it updates the instrument switch), so the scanner gets its own small connection class reusing ReconnectionPolicy and the Nordic library as DistoXBleManager does (which uses the same Nordic UART UUIDs, told apart by name). The Nordic library's writeCharacteristic(...).split() splits frames to the MTU. Station changes are only broadcast as SURVEY_UPDATED_EVENT, including another survey being opened (SurveyManager.setCurrentSurvey). The device screen uses Android's discovery, which finds BLE devices too, and remembers non-bonding devices by address in preferences. Readings are applied on the main thread, and sections must be too.

Link 4a, link logic, test scanner and device screen, no real Bluetooth.

1. ScannerType in comms/scanner, XSCAN by the XScan- name, and TEST for the developer test scanner. Scanners kept out of the instrument search and instruments out of the scanner's.
2. StationWatcher keeps a copy of the survey's stations and names, and on each survey update compares. Another survey object sends survey, a new Station station_added, the same Station with a new name station_renamed, a Station gone station_deleted. Comparing Station objects means a rename is never a delete and an add, and the rename and delete code needs no hooks.
3. ScannerLink, owned by SurveyManager for the life of the app, given a connection that sends and receives bytes. Runs LINK.md's start (hello, protocol check, survey), sends station changes, joins frames, passes sections to ScannerSession on the main thread, sends each reply, broadcasts the survey update and autosaves after a change, drops a part frame after 10 s.
4. Conflict (dialog agreed). ScannerLink broadcasts it and whichever SexyTopo screen is open asks whether to keep SexyTopo's drawing or use the scanner's. Using the scanner's applies the change held by the conflict and sends ok.
5. Test scanner, developer mode only, a pretend connection like the test instrument that sends the shared example sections and the survey's own scans, so the emulator runs everything but the radio.
6. Device screen. A Cross-section scanner section, shown only with the setting on, with the scanner's name, Find and Forget, a Connect switch and a status line (connected, firmware, last section). Link events in the device log.
7. Strings translated into de, fr, es, it, pl, pt.
8. Tests. StationWatcher (each change, two at once, another survey) and ScannerLink with a pretend connection (start, protocol mismatch, a section, a dropped frame, a part frame timing out, station changes while connected).

Link 4b, the Bluetooth connection.

1. ScannerBleManager on the Nordic BleManager. Checks the Nordic UART service, asks for MTU 517, turns on notifications, passes bytes to ScannerLink, writes frames with split().
2. Remembered by address with no bond, ReconnectionPolicy for reconnecting, with the existing auto-reconnect setting.
3. Tested on the phone with the board, as this computer has no Bluetooth and the emulator's is virtual. So Link 5 (at least NimBLE, hello, survey and sending a section) comes before 4b can be checked.

Link 4a written 2026-11-23, SexyTopo commit d141b5dd. ScannerLink (protocol over any ScannerConnection), StationWatcher, ScannerType, ScannerService (a singleton beside SurveyManager rather than owned by it, holding the link for the life of the app and listening for survey updates), TestScannerConnection (developer mode), ScannerConflictDialog (started and stopped by SexyTopoActivity, so every screen asks), and a scanner card in the device screen. The remembered scanner is kept by address, with "test" for the test scanner. Tried on the emulator with the test scanner. Hello and survey were sent, a section was taken and placed 3 m from the station with its points and drawing, and a section after an edit in SexyTopo raised the question, with "Use the scanner's" replacing the drawing. 1087 unit tests pass.

Link 4b written 2026-11-23, SexyTopo commit 5098b851. BleScannerConnection with the Nordic library (MTU 517, notifications into the link, writes split to the MTU, ReconnectionPolicy). The link ignores a disconnect from a connection already replaced. Not yet tried with a real scanner.

Link 5, the firmware side, written 2026-11-23 (user agreed to go on to it and flash).

- NimBLE-Arduino 1.4.3 (link_ble), advertising XScan- and the last 6 hex digits of the MAC, the name only as the service UUID doesn't also fit. Received bytes are held behind a mutex, as NimBLE calls back from its own task. Frames go out as notifications of MTU - 3 bytes from loop(), retried next time if the stack is out of buffers.
- link_frame (frames and the reader, as xscan_link.py), link_model (stations, each scan's station, orientation, facing, state and acknowledged revision) and exchange (a saved scan as an XScan scan 1 line, as board_link.py writes it) are plain C++, checked on the computer by tests/firmware_link/run.sh (the shared frames whole and a byte at a time, the model, and the exchange line against make_expected.py's from board_link.py).
- The link state is one file, /link.json, rather than in each scan's record as planned above, so the scan files are never rewritten. Scans overwritten or deleted are dropped from it.
- link (the board's side). Answers hello, keeps the survey's stations and station changes, sends accepted scans one at a time with based_on, and takes each reply (a reply for an older revision changes nothing, a reply with no scan sends again). A section with no reply in 30 s is sent again, and anything sent without a reply goes again on the next connection. A new drawing revision sends the scan again.
- On the screen, Stn opens a station picker over the view, newest first 8 at a time, with Vertical/Horizontal and Facing on/back toggles, Older and No station. A station that already has a scan needs a second tap, and the old scan loses it. The message line shows the scan's station, orientation, facing and state, and whether SexyTopo is linked. LINK over USB prints the state.
- Not yet done. Typing a station when no survey has been received, a protocol mismatch shown on the screen, and the scan states shown in the list of scans.
- Flash 77% used (NimBLE about 0.5 MB), RAM 26% before the heap. Flashed 2026-11-23 and checked over USB (boots, READY, LINK gives the empty state). Bluetooth not yet tried, as this computer has none.

Trying it on the phone (Link 4b and 5 together).

1. SexyTopo built from SectionScanner on the phone, with Cross-section scanner on in the Instruments settings, and a survey open.
2. Device screen, scanner card, Find. The log should show Found scanner XScan-xxxxxx. Then Connect to scanner. The log should show the scanner ready, and the board's message line SexyTopo linked.
3. On the board, a scan, Stn, a station. The board should show sent then acknowledged, and SexyTopo's log the scan taken, with the points and section at that station.
4. Draw on the board and Save. It should be sent again and replace the drawing. Edit the section in SexyTopo, then draw and save on the board again, and SexyTopo should ask which to keep.
5. Rename and delete a station in SexyTopo, and the board's picker and scan should follow.
6. If anything goes wrong, a serial monitor on USB (pio device monitor -b 460800) shows the LINK lines, and LINK prints the state.

Tried on the phone 2026-11-24 (TCL 50 Pro NXTPAPER, SexyTopo β). Found, connected, scan sent and acknowledged, drawing sent, rename followed, deleting a station left the scan with no station. One "Lidar scan failed" on the board, then working, probably a lead or power. Android froze SexyTopo two minutes after it went to the background, which drops the link.

Link 6, from that try (user asked 2026-11-24, written the same day).

1. Station picker. Tapping a station only picks it (filled), No station likewise, and Accept and Cancel take the place of the scan screen buttons, so orientation and facing can be set after the station. Accept with nothing changed just closes.
2. Where sections go. Horizontal sections always go on the elevation, vertical ones on the plan unless the new setting (Instruments, "Put the scanner's vertical sections on", Plan or Elevation, default Plan) says the elevation. A section turned horizontal moves off the plan. Sections already placed by hand stay where they are.
3. A scan moved to another station on the board left its old section in SexyTopo, so it appeared twice. Now the old section is removed too, unless edited in SexyTopo since it came from the board.
4. Undoing or deleting a section in SexyTopo. A scan counts as on its station only while the station has a section. StationWatcher sends scan_station (docs/LINK.md) when that changes, so the board takes the scan off its station, and a redo puts it back. Sketch undo and redo now broadcast a survey update so the link hears of them. The survey message on connecting leaves such scans out, and the board clears acknowledged scans it leaves out.

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
   Lidar on the board, written 2026-11-19 (firmware/src/lidar.cpp), wiring in docs/WIRING.md (E32R24P, two plugs, lidar TX to IO19 and RX from IO27 on the SPI connector, 5 V and GND from P2, SD slot left empty so scans will go in internal flash with LittleFS). First tested wired 2026-11-20, working.
   Stand-alone from 2026-11-20. The computer scan fallback and drawing scans sent from the computer are removed (send_to_screen.py is now tools/board_link.py). SCAN is greyed out as "No lidar" until the lidar answers, and the board looks for it every 2 s so it can be plugged in later. For debugging, board_link.py shows what the board reports and saves each scan it takes (source board), --scan asks for a scan and --status for the lidar details. Opening the port reset the board up to 2026-11-19, but did not on 2026-11-20 (scans kept their start-up number and time).
   The screen shows the scan seen from the lidar bottom, which is how the device is held, so it is mirrored left to right from the computer plot (seen from the top). Saved scans stay raw, and the drawn outline (stage 3) will be in the screen's coordinates, which must be recorded with it.
   Then wire the lidar to the CYD, read and decode a revolution on the ESP32, show points on the screen.
2. Capture button, freeze and display a single scan, save raw data. Written 2026-11-20, not yet tested on the board.
   Each scan is saved straight away in the board's flash (LittleFS on the default spiffs partition, about 1.4 MB, about 130 one-turn scans, fewer once outlines are saved too), one text file per scan in the same format as sent over USB (firmware/src/scan_store.h). Files are named b<start-up>-<scan>, e.g. b0007-003, with the start-up count kept in flash, because the board has no clock. The time since start-up is recorded so scans from one walk keep their order and spacing. The lidar details are recorded with every scan. When the flash is full the oldest scans are overwritten. ‹ and › on the screen step through the saved scans.
   board_link.py --list shows them, --fetch copies them to scans/board-<name>.json (skipping ones already there, time null, received set) and --fetch --clear then deletes each from the board once its size matched and it reads back on the computer.
   FORMAT 2 from 2026-11-22. Samples with no return (distance and quality 0, about a fifth) are no longer saved, as the gap in the angles shows them. The step between samples follows the motor speed (0.75 to 0.84 degrees from scan to scan, 0.58 to 1.06 within one), but the gap divided by the scan's typical step gave the right number missing for all 475 gaps in the scans fetched so far. Each turn's T line gives its samples and returns, and board_link.py refuses a scan whose returns don't match (tests/board_link). FORMAT 1 scans still read.
   Station names are left for stage 4. They will go inside the record, and the file name can stay as it is.
3. Draw round the scan with the stylus, edit (undo, clear), save the outline. Plan 2026-11-20, see "Stage 3 plan" below.
4. Accept a scan. Station (typed for now), vertical or horizontal, facing on or back.
5. Add the IMU, level indicator, record orientation.
6. Two-way Bluetooth link with SexyTopo, see Link with SexyTopo, with the SexyTopo side as an upstream change.
7. Battery and case, cave trials.

## Open questions

- Is the CYD in PlayDisplay-touch.yaml this same board (assumed yes)?
- Horizontal sections are plan slices at a station, as SexyTopo already has. Answered 2026-11-20.
- Should a section be one revolution or several averaged? Five-turn tests 2026-11-19 gave 400 to 456 points a turn with about 12% no return, mostly at the same angles each turn. The motor slows over the first few seconds, but the user says there is no need to wait for it, and 400 points is already far more than needed. A thinned set for export and splays, raw kept. How to thin is to be decided from what we get in the cave.
- Which IMU, and is heading needed or only tilt? GY-85, heading to a few degrees and tilt. Answered 2026-11-22.
- Where is the device held relative to the station, and how is that offset recorded?

## Stage 3 plan, drawing round the scan

Agreed 2026-11-20. The design follows SexyTopo, but the code can't be shared. The ESP32 has no Java runtime, SexyTopo's sketch code is built on Android classes, and no cross-platform language (Flutter, Kotlin Multiplatform, Java to WebAssembly) runs on this board. So the behaviour is copied into C++, and what is kept truly the same is the data, in SexyTopo's own sketch format, so SexyTopo opens and edits what the scanner draws.

Taken from SexyTopo upstream main 26c93a7a (2026-11-18, version 1.15.0), read from an export of ~/StudioProjects/sexytopo, Sexytop/Richsmith/main. Each piece copied names the SexyTopo file and this commit, so upstream changes can be checked.

### What SexyTopo does (as read)

- A cross section (model/sketch/CrossSection.java) is at a station, vertical facing a compass angle or horizontal. Its drawing is a sub-sketch stored in the plan sketch file (<survey>.plan.json) under x-sections, each entry with station-id, location, angle, optional orientation horizontal, and sketch (control/io/basic/SketchJsonTranslater.java).
- The sub-sketch holds paths, areas, labels and symbols. A path is colour, line-type (left out for a plain sketch line) and points as x, y.
- Sub-sketch coordinates are metres from the station. For a vertical section, x is to the right looking in the direction the section faces, and y is down (Projection2D CROSS_SECTION after the splays are turned by minus the angle, so y is minus height).
- Line types (model/sketch/LineType.java). SKETCH (plain, no line type written), WALL, FLOOR_STEP, PIT, CHIMNEY, CEILING_STEP, OVERHANG, SLOPE, ROCK_BORDER, ROCK_EDGE, PRESUMED_WALL, UNDERLYING_WALL. All are wanted on XScan. Plain SKETCH lines come first, as they have no inside to work out.
- Drawing a line (control/sketch/SketchView.java handleLine, model/sketch/Sketch.java). Touch points are collected while the pen is down. On lifting, a line that comes back near its start is closed, wall lines are turned so the passage is on a fixed side (control/util/LineOrienter.java), ends snap to nearby line ends (Sketch.snapActivePath), and the line is simplified by Douglas-Peucker with tolerance = largest size of the line / 500, at least 1 mm (control/util/Space2DUtils.java).
- Undo and redo of each finished step. The eraser cuts lines at its edge.

### Our screen and SexyTopo's

The screen already shows the section seen from the lidar bottom, which is where the user stands, looking the way the section faces. So screen x matches SexyTopo's x, and SexyTopo's y is minus screen up, in metres. Until the IMU (stage 5), up is assumed to be the lidar's 0°, and the device is assumed to be at the station (the offset is still an open question).

### On the device

1. A Draw button on the scan screen opens the drawing screen, showing the same scan view.
2. Tools in the button row are Undo, Clear, Cancel (back to how it was when opened, user 2026-11-20) and Save (Redo later), with a line type picker once the other types come. Undo is enough, no eraser for now. Zoom later if needed.
3. Each stroke is handled as in SexyTopo. Collect points while touched, then on lifting close if near its start, orient walls (only for the wall types), snap ends, simplify. Snap and close distances are a fixed number of screen pixels converted with the current scale, the same idea as SexyTopo's 10 dp.
4. Saved beside the scan as b<start-up>-<scan>.sketch.json in SexyTopo's sub-sketch format (paths, plus empty areas, labels and symbols), with our own header for the scan name, format version, the SexyTopo commit followed, and the view it was drawn in. The raw scan file is never changed.
5. Undo history lives only while drawing. Saving writes the finished set of lines.
6. Opening a saved scan with a drawing shows the lines over the points. Draw again edits them.

### On the computer

- board_link.py --fetch also fetches the .sketch.json files.
- tools/to_sexytopo.py puts a drawing into an existing SexyTopo survey's plan.json as a cross section at a named station, with the angle given or worked out the way SexyTopo does (CrossSectioner). This lets SexyTopo open real scanner drawings now, before the Bluetooth link (stage 6).
- Shared test files. Sketch files saved by SexyTopo that the device's reader must load, and device-written files that SexyTopo must load and show the same. Checked by opening them in SexyTopo, and by a Python check of the format in tools/.

### Steps

1. Drawing screen with plain sketch lines, points collected and drawn, Undo and Clear. No saving. Written 2026-11-20 (firmware/src/sketch.cpp, Draw on the scan screen in place of the Cal button, calibration now only by holding the screen at power-on), to be tried on the board.
2. Finishing a stroke as SexyTopo does (close, simplify, snap). Each copied from the named SexyTopo file. Written 2026-11-20 for plain lines (snap to line ends and close into a loop, joined lines become one, a tap leaves a dot, Douglas-Peucker simplify), with tests/sketch run on the computer (13 checks pass). Clip to Walls and closing outlines come with the other line types. Snap to Lines and its distance are options in SexyTopo, fixed on and 10 px here for now (user 2026-11-20).
3. Save and load the sketch file, show it with the scan, fetch with board_link.py. Written 2026-11-20 (firmware/src/sketch_file.cpp, ArduinoJson 7.4.3 for reading). Saved on Save if changed, revision counted up on each save, removed with its scan, never overwrites its own scan when the flash is full. board_link.py --list shows drawing sizes, --fetch saves scans/board-<name>.sketch.json byte for byte. To be tried on the board.
4. to_sexytopo.py and the shared test files, and a check in SexyTopo on the phone. Written 2026-11-20. tools/to_sexytopo.py adds a drawing to a survey's plan.json as a vertical cross section (angle as CrossSectioner, --facing back, --offset, --replace, backup kept) and --check checks a drawing. tests/sexytopo has a made-up survey and drawing, and test_to_sexytopo.py (16 checks pass). The scan points are not shown in SexyTopo, which needs the SexyTopo side of the link. Checked on the phone 2026-11-22 with a test survey in scans/phone (not in git), both sections in the right place and the right way round.
5. The other line types with a picker, and wall orientation. Written 2026-11-22. All 12 of SexyTopo's line types (firmware/src/sketch.cpp, from LineType.java), saved with SexyTopo's line-type. A Type button between Clear and Cancel opens a picker over the view showing each type with a sample. On the screen walls are wider, rock edges grey, and dashes, ticks, hachures and teeth repeat along the line at a fixed 7 px ornament size (firmware/src/line_style.cpp). Finishing a line follows SketchView.handleLine with Auto-Orientation on. A rock border whose ends nearly meet is closed. Walls are turned by LineOrienter with the station (the scanner, 0, 0) as the only leg, so the passage is always on the side SexyTopo expects. Pit and chimney edges and rock borders drawn round something, alone or closed by a wall, are turned by LoopOrienter. Lines join only lines of the same type, head to tail where direction matters. A typed line under 10 px across is dropped, so a tap leaves a dot only for a plain line. tests/sketch has 25 checks. Clip to Walls (on by default in SexyTopo, a large port of PolygonUtils) and Flip Last Line are left for later. Undo and draw again for now if ticks end up on the wrong side.
6. Redo, then zoom if wanted after use.
