# XScan link with SexyTopo

Protocol 1, written 2026-11-23 (PLAN.md, Link stages, 3a). The scanner (XScan) and SexyTopo both follow this, and tests/link holds example frames that both ends' tests must read the same way. tools/xscan_link.py is the reference for framing, used to make those frames and, later, to talk to either end from the computer.

## Bluetooth

BLE, with XScan as the peripheral and SexyTopo as the central.

- XScan advertises the name XScan- followed by the last 6 hex digits of the board's MAC address, e.g. XScan-A1B2C3. SexyTopo's ScannerType recognises it by the XScan- prefix.
- One service in the Nordic UART Service layout, so nRF Connect on a phone or bleak on the computer can stand in for either end.

| | UUID | Properties | Direction |
|---|---|---|---|
| Service | 6E400001-B5A3-F393-E0A9-E50E24DCCA9E | | |
| RX | 6E400002-B5A3-F393-E0A9-E50E24DCCA9E | write, write without response | SexyTopo to XScan |
| TX | 6E400003-B5A3-F393-E0A9-E50E24DCCA9E | notify | XScan to SexyTopo |

- SexyTopo asks for an MTU of 517 on connecting. Each write or notification carries at most MTU - 3 bytes, and a frame is split over as many as it needs. Writes and notifications are sent in order, one frame finished before the next starts.

## Frames

Each message is one JSON object, sent as a frame.

```
XS1 <length> <crc>\n<json>
```

- XS1 is the frame version. A new one only if the framing itself changes.
- length is the number of bytes of the JSON in UTF-8, in decimal.
- crc is the CRC-32 of those bytes (as zlib.crc32 and java.util.zip.CRC32), 8 lower-case hex digits.
- The JSON follows the newline straight away, with nothing after it. The next frame starts with the next byte.
- The JSON is written on one line, without spaces, as Python's json.dumps(..., separators=(",", ":")) writes it, though a reader must take any valid JSON.

A frame is at most 262144 bytes of JSON (a scan with its drawing is under 30 KB).

### Reading

The receiver keeps the bytes it has been sent and takes frames from the front.

- A header line is the bytes up to the first newline. It must be XS1, a space, the length, a space and the CRC, and no more than 32 bytes. If it isn't, or the length is over the limit, the bytes up to the next "XS1 " are dropped and reading goes on from there.
- Once length bytes of JSON have arrived, their CRC is checked. If it doesn't match, or they aren't a JSON object with a type, the frame is dropped. A dropped frame from XScan gets a reply with result error and reason bad_checksum (or unknown_format), with no scan, so XScan sends it again.
- Until all length bytes have arrived the frame waits. It is dropped if nothing more comes for 10 seconds or the connection ends.

## Messages

Every message has a type. A message with a type the receiver doesn't know is ignored, so later versions can add types. Fields a receiver doesn't know are ignored too.

### Starting

1. SexyTopo connects, asks for the MTU, turns on TX notifications, and sends hello.
2. XScan answers with its own hello.
3. If the protocols differ, both ends show it and SexyTopo disconnects.
4. SexyTopo sends survey.
5. XScan sends each section that hasn't had an ok, one at a time, waiting for the reply before the next.

### hello (both ways)

```json
{"type":"hello","protocol":2,"app":"SexyTopo 1.15.0"}
{"type":"hello","protocol":2,"firmware":"0.7","board":"A1B2C3"}
```

### survey (SexyTopo to XScan)

On connecting, and again whenever another survey is opened.

```json
{"type":"survey","name":"Trip1","stations":["1","2","3","5a"],"scans":[{"scan":"b0004-001","station":"5a"}]}
```

- stations are oldest first. XScan offers the newest first.
- scans are every scan SexyTopo holds on a station, with its station's current name. A scan counts as on its station only while the station has a cross-section, so one whose section was undone or deleted is left out. XScan uses them to bring its own stations up to date, so a rename made while apart reaches it too. When the survey name is the one XScan already has, an acknowledged scan left out loses its station on XScan. Scans waiting to be sent, or from another survey, are left alone.

### station_added, station_renamed, station_deleted (SexyTopo to XScan)

```json
{"type":"station_added","name":"6"}
{"type":"station_renamed","from":"5","to":"5a"}
{"type":"station_deleted","name":"6"}
```

XScan changes the station on any scan that has it. A deleted station's scan loses its station and stays on XScan.

### scan_station (SexyTopo to XScan)

A scan SexyTopo holds has moved to another station, or off its station when its cross-section is undone or deleted (no station given). Sent after the station messages, whenever the survey changes.

```json
{"type":"scan_station","scan":"b0004-001","station":"6"}
{"type":"scan_station","scan":"b0004-001"}
```

With no station, XScan takes the scan off its station, as for a deleted station. With a station, XScan puts the scan there as acknowledged, taking the station from any other scan. A scan already at that station is left as it is, so a newer drawing waiting to be sent still goes. Added in firmware 0.6.

### section (XScan to SexyTopo)

A scan accepted with a station, and its drawing.

```json
{"type":"section","scan":{"format":"XScan scan 1","scan":"b0004-001","station":"5","orientation":"vertical","facing":"on","board":{"start_up":4,"scan":1,"ms_since_start_up":54326},"lidar":{"model":65,"firmware":"1.02","hardware":18,"serial":"53AB..."},"samples":428,"returns":320,"points":[[359.312,82.25,53]]},"sketch":{"format":"XScan sketch 1","scan":"b0004-001","revision":2,"sexytopo":"26c93a7a","sketch":{"paths":[]}},"based_on":1}
```

- scan is one line of the exchange format (PLAN.md, Exchange format), with station, orientation (vertical or horizontal) and facing (on or back) always given.
- sketch is the drawing as saved on the board (firmware/src/sketch_file.cpp), left out if there isn't one. Its revision is the section's revision, 0 with no drawing.
- based_on is the agreed revision for this scan, the last one SexyTopo said ok to or XScan took from SexyTopo, 0 if none. SexyTopo replaces its drawing only if it hasn't been edited there since based_on.

### reply (SexyTopo to XScan)

One for each section.

```json
{"type":"reply","scan":"b0004-001","revision":2,"result":"ok"}
{"type":"reply","scan":"b0004-001","revision":2,"result":"error","reason":"no_station"}
{"type":"reply","scan":"b0004-001","revision":2,"result":"conflict"}
```

- ok, SexyTopo has the scan and drawing at this revision. A section it already has (same scan and revision) gets ok again and changes nothing.
- error, not taken. reason is one of bad_checksum, unknown_format, no_station, bad_points, bad_sketch. scan and revision are left out when the frame couldn't be read.
- conflict, the drawing was edited in SexyTopo since based_on. SexyTopo keeps its own and asks its user. If they choose the scanner's, it sends ok for this revision. If they keep SexyTopo's, SexyTopo sends its drawing based on this revision (drawing below), now or on the next connection.

XScan shows each scan as new, accepted, sent, acknowledged (after ok), error or conflict. A sent scan with no reply is sent again on the next connection.

### Drawing revisions (protocol 2)

Either end can edit a scan's drawing, and each sends its whole drawing when it finishes editing (XScan on Save, SexyTopo when its cross-section editor is left). Drawing at both ends at once is unlikely, so a clash is simply asked about, in SexyTopo.

- The agreed revision is the last one both ends have. Each drawing sent says the agreed revision it was based on, and the other end takes it only if its own drawing hasn't changed since.
- XScan's revisions are odd and SexyTopo's even, each the next above the one it starts from, so the two never share a number.
- SexyTopo always does the asking. XScan shows conflict until it is settled.

### drawing (SexyTopo to XScan)

A drawing edited in SexyTopo, sent when its cross-section editor is left and on each connection while it differs from the agreed one. Not sent while SexyTopo's user hasn't settled a conflict for that scan.

```json
{"type":"drawing","scan":"b0004-001","revision":4,"based_on":3,"sketch":{"format":"XScan sketch 1","scan":"b0004-001","revision":4,"sketch":{"paths":[],"areas":[],"labels":[],"symbols":[]}}}
```

- based_on is the agreed revision, or after the user keeps SexyTopo's drawing in a conflict, the scanner's revision that clashed.
- sketch is SexyTopo's whole drawing. XScan keeps colours, areas, labels and symbols as they came and writes them back, shows them, and lets only lines be edited.

### drawing_reply (XScan to SexyTopo)

```json
{"type":"drawing_reply","scan":"b0004-001","revision":4,"result":"ok"}
{"type":"drawing_reply","scan":"b0004-001","revision":4,"result":"conflict"}
{"type":"drawing_reply","scan":"b0004-001","revision":4,"result":"error","reason":"no_scan"}
```

- ok, XScan has the drawing at this revision, which is then agreed at both ends. A drawing it already has gets ok again, so a lost reply does no harm.
- conflict, XScan's drawing changed since based_on. XScan sends its own as a section, and SexyTopo asks its user then.
- error, not taken. reason is no_scan (the scan isn't saved on XScan), bad_sketch or flash. SexyTopo stops sending that drawing.
- If XScan's drawing screen is open on that scan, the reply waits. Cancel takes SexyTopo's drawing, Save sends XScan's, which is a conflict.
