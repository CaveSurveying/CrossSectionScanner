# InitialConnect

The first survey made with the scanner linked to SexyTopo over Bluetooth, while the link was being developed (Link 6 to Link 8 in PLAN.md).

- SexyTopo 1.15.0-debug (SexyTopo β, branch SectionScanner), on a TCL 50 Pro.
- Scanner board E32R24P, firmware changing between start-ups 9 and 16 as the link was developed.
- Copied from the phone and the board on 2026-11-25.

## Contents

`SexyTopo/` is the survey, 25 stations (1 to 25), from the phone's `Surveys/CrossSextionScanner/InitialConnect/`.

`DeviceFiles/` are the seven board scans the survey uses.

| Scan      | Station in xscan.jsonl | Drawing on the board |
|-----------|------------------------|----------------------|
| b0009-001 | 19                     | yes                  |
| b0013-001 | 23                     | yes                  |
| b0013-002 | 20                     | no                   |
| b0013-003 | 27 and 18 (see below)  | yes                  |
| b0014-001 | 11                     | yes                  |
| b0015-001 | 22                     | yes                  |
| b0016-001 | 24                     | yes                  |

Cross sections in the survey are vertical at 11, 19, 20, 21, 22 and 24 in the plan, and horizontal at 18 in the extended elevation. Station 21 has a section but no scan, and station 23 has a scan but no section.

## Known faults

- Needs fixing. Scan b0013-003 appears twice in `InitialConnect.xscan.jsonl`, at station 27 and at station 18. A scan belongs to one station, so the old line should have gone when the scan moved. Station 27 is not in the survey, and only the section at 18 exists. Kept as found until it is fixed.
