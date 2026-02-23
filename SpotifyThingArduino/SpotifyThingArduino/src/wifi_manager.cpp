#include "wifi_manager.h"
#include "display.h"
#include "secrets.h"
#include <WiFi.h>

void wifiConnect() {
  displayMessage("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  WiFi.noLowPowerMode();

  displayMessage("Connected!");
  delay(500);
}
