# Samples

Real data sets from the scanner and SexyTopo, kept to test and develop against. They are improved as more data comes in.

Each data set has its own folder:

- `README.md` says when and how it was collected, the versions used and anything odd about it.
- `SexyTopo/` holds the survey as SexyTopo saved it, copied from the phone without the `*.autosave` files.
- `DeviceFiles/` holds the scanner's own files for the scans in that survey, copied byte for byte from the board's flash with the names they have there. `<scan>.txt` is the scan and `<scan>.sketch.json` its drawing, if it has one.

Files are kept as they were found, including any faults, which are listed in the data set's README rather than corrected.

## Licence

The code in this repository is under the GNU GPL v3 (see `LICENSE` at the top). The sample data is under Creative Commons Attribution-ShareAlike 4.0 (CC BY-SA 4.0, https://creativecommons.org/licenses/by-sa/4.0/).
