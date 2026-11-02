#include "wifiManager.h"
#include "display.h"
#include "credentials.h"
#include <WiFi.h>
#include <time.h>

static constexpr long GMT_OFFSET_SEC = 0;
static constexpr unsigned int CONNECT_CHECK_MS = 250;
static constexpr unsigned int CONNECTED_DISPLAY_MS = 500;
static constexpr unsigned int MAX_ATTEMPTS = 5;
static constexpr unsigned int CHECKS_PER_ATTEMPT = 5;
static constexpr unsigned int FAILED_DISPLAY_MS = 3000;

void wifiConnect()
{
  for (unsigned int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++)
  {
    String line2 = "Attempt " + String(attempt) + "/" + String(MAX_ATTEMPTS);
    displayMessage(("Connecting to " + String(credentialsCurrent().wifiSsid)).c_str(), line2.c_str());

    WiFi.begin(credentialsCurrent().wifiSsid, credentialsCurrent().wifiPassword);

    for (unsigned int check = 0; check < CHECKS_PER_ATTEMPT; check++)
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        WiFi.noLowPowerMode();
        configTime(5000, GMT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
        displayMessage("Connected!");
        delay(CONNECTED_DISPLAY_MS);
        return;
      }
      delay(CONNECT_CHECK_MS);
    }

    WiFi.disconnect();
  }

  displayMessage("WiFi failed", "Check credentials");
  delay(FAILED_DISPLAY_MS);
}
