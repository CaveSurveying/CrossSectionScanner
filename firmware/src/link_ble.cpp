#include "link_ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace link_ble {
namespace {
const char *SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char *RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // SexyTopo to XScan
const char *TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // XScan to SexyTopo
constexpr uint16_t MTU = 517;
constexpr int CHUNKS_PER_PUMP = 8;

NimBLEServer *server = nullptr;
NimBLECharacteristic *tx = nullptr;
SemaphoreHandle_t lock;  // NimBLE calls back from its own task
std::string received;
std::string outgoing;
size_t sentUpTo = 0;
std::string id;
volatile bool isConnected = false;
volatile uint16_t connection = 0;
volatile int connects = 0, disconnects = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
    connection = desc->conn_handle;
    isConnected = true;
    connects++;
  }
  void onDisconnect(NimBLEServer *) override {
    isConnected = false;
    disconnects++;
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    const std::string value = c->getValue();
    xSemaphoreTake(lock, portMAX_DELAY);
    received += value;
    xSemaphoreGive(lock);
  }
};
}  // namespace

void begin() {
  lock = xSemaphoreCreateMutex();
  char hex[7];
  const uint64_t mac = ESP.getEfuseMac();  // the bytes in reverse, so the last three are the lowest
  snprintf(hex, sizeof hex, "%02X%02X%02X", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  id = hex;
  NimBLEDevice::init("XScan-" + id);
  NimBLEDevice::setMTU(MTU);
  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService *service = server->createService(SERVICE);
  NimBLECharacteristic *rx = service->createCharacteristic(RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  tx = service->createCharacteristic(TX, NIMBLE_PROPERTY::NOTIFY);
  service->start();
  // the name only, as with the service's 128-bit UUID as well it doesn't fit; SexyTopo goes by the name
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setName("XScan-" + id);
  advertising->start();
}

const std::string &boardId() { return id; }

bool connected() { return isConnected; }

int takeConnectionChange() {
  if (disconnects) {
    disconnects--;
    xSemaphoreTake(lock, portMAX_DELAY);
    received.clear();
    xSemaphoreGive(lock);
    outgoing.clear();
    sentUpTo = 0;
    return -1;
  }
  if (connects) {
    connects--;
    return 1;
  }
  return 0;
}

std::string takeReceived() {
  xSemaphoreTake(lock, portMAX_DELAY);
  std::string out;
  out.swap(received);
  xSemaphoreGive(lock);
  return out;
}

void send(const std::string &bytes) {
  if (isConnected) outgoing += bytes;
}

void pump() {
  if (!isConnected || sentUpTo >= outgoing.size() || !tx->getSubscribedCount()) return;
  const size_t chunk = server->getPeerMTU(connection) - 3;
  for (int i = 0; i < CHUNKS_PER_PUMP && sentUpTo < outgoing.size(); i++) {
    const size_t n = std::min(chunk, outgoing.size() - sentUpTo);
    os_mbuf *om = ble_hs_mbuf_from_flat(outgoing.data() + sentUpTo, n);
    // the stack takes om either way, and is out of buffers if it fails, so try again next time
    if (!om || ble_gattc_notify_custom(connection, tx->getHandle(), om) != 0) return;
    sentUpTo += n;
  }
  if (sentUpTo >= outgoing.size()) {
    outgoing.clear();
    sentUpTo = 0;
  }
}

bool sending() { return sentUpTo < outgoing.size(); }
}  // namespace link_ble
