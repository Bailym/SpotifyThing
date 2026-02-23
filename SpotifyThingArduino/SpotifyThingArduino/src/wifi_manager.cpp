#include "wifi_manager.h"
#include "display.h"
#include "secrets.h"
#include <WiFi.h>

static constexpr unsigned long CONNECT_TIMEOUT_MS   = 30000;
static constexpr unsigned int  CONNECT_CHECK_MS      = 500;
static constexpr unsigned int  CONNECTED_DISPLAY_MS  = 500;

void wifiConnect() {
  displayMessage("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMs >= CONNECT_TIMEOUT_MS) {
      displayMessage("WiFi timeout");
      return;
    }
    delay(CONNECT_CHECK_MS);
  }

  WiFi.noLowPowerMode();
  displayMessage("Connected!");
  delay(CONNECTED_DISPLAY_MS);
}
