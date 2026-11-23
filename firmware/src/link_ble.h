// Bluetooth LE for the link with SexyTopo (docs/LINK.md), NimBLE in the Nordic UART Service layout.
// The board advertises as XScan- and the last 6 hex digits of its MAC address. Received bytes are
// kept until taken, and bytes to send go out as notifications of at most MTU - 3 bytes from pump().
#pragma once
#include <string>

namespace link_ble {
void begin();
const std::string &boardId();  // the 6 hex digits in the name
bool connected();
// Changes since last asked: +1 connected, -1 disconnected, 0 none (a quick drop and reconnect
// shows as -1, the next call +1)
int takeConnectionChange();
std::string takeReceived();
void send(const std::string &bytes);  // queued behind anything not yet sent
void pump();                          // call often from loop()
bool sending();                       // bytes still queued
}  // namespace link_ble
