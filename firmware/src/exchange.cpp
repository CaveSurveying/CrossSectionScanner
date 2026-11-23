#include "exchange.h"

#include <sstream>
#include <vector>

namespace exchange {
namespace {
std::vector<std::string> words(const std::string &text) {
  std::istringstream in(text);
  std::vector<std::string> out;
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

// A number as written in the text, without trailing zeros, so 1234.00 is 1234 as board_link.py has it
std::string trimmed(std::string number) {
  if (number.find('.') == std::string::npos) return number;
  number.erase(number.find_last_not_of('0') + 1);
  if (number.back() == '.') number.pop_back();
  return number;
}

std::string quoted(const std::string &text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}
}  // namespace

std::string line(const std::string &scanText, const std::string &station, bool horizontal, bool back) {
  std::string name, board, lidar, samples, points;
  int turns = 0, returns = 0;
  std::istringstream in(scanText);
  for (std::string row; std::getline(in, row);) {
    const std::vector<std::string> w = words(row);
    if (w.empty()) continue;
    if (w[0] == "NAME" && w.size() > 1) {
      name = w[1];
    } else if (w[0] == "BOOT" && w.size() > 3) {
      board = "{\"start_up\":" + w[1] + ",\"scan\":" + w[2] + ",\"ms_since_start_up\":" + w[3] + "}";
    } else if (w[0] == "LIDAR" && w.size() > 4) {
      lidar = "{\"model\":" + w[1] + ",\"firmware\":" + quoted(w[2]) + ",\"hardware\":" + w[3] +
              ",\"serial\":" + quoted(w[4]) + "}";
    } else if (w[0] == "T") {
      if (++turns == 1 && w.size() > 1) samples = w[1];
    } else if (w[0] == "P" && turns == 1 && w.size() > 3) {
      if (returns++) points += ",";
      points += "[" + trimmed(w[1]) + "," + trimmed(w[2]) + "," + w[3] + "]";
    }
  }
  if (name.empty() || !turns) return "";
  std::string out = "{\"format\":\"XScan scan 1\",\"scan\":" + quoted(name);
  if (!station.empty()) out += ",\"station\":" + quoted(station);
  out += std::string(",\"orientation\":\"") + (horizontal ? "horizontal" : "vertical") + "\"";
  out += std::string(",\"facing\":\"") + (back ? "back" : "on") + "\"";
  if (!board.empty()) out += ",\"board\":" + board;
  if (!lidar.empty()) out += ",\"lidar\":" + lidar;
  if (!samples.empty()) out += ",\"samples\":" + samples;
  out += ",\"returns\":" + std::to_string(returns) + ",\"points\":[" + points + "]}";
  return out;
}
}  // namespace exchange
