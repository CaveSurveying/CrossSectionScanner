// A saved scan (scan_store.h text) as one line of the exchange format, XScan scan 1 (PLAN.md,
// Exchange format), as tools/board_link.py jsonl_line writes it, with the station and how it was
// taken. Only the first turn, as a scan has one. Plain C++ so it can be checked on the computer.
#pragma once
#include <string>

namespace exchange {
// Empty if the text has no name or turn
std::string line(const std::string &scanText, const std::string &station, bool horizontal, bool back);
}  // namespace exchange
