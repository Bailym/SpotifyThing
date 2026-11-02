#include "usbConfig.h"
#include "credentials.h"
#include "display.h"
#include <Arduino.h>
#include <FatFS.h>
#include <FatFSUSB.h>
#include <hardware/flash.h>

extern "C" uint8_t _FS_start;
extern "C" uint8_t _FS_end;

static constexpr const char* CONFIG_FILE_PATH = "config.txt";
static constexpr const char* UTF8_BYTE_ORDER_MARK = "\xEF\xBB\xBF";
static constexpr size_t MAX_CONFIG_FILE_BYTES = 4096;
static constexpr unsigned int IMPORT_RESULT_DISPLAY_MS = 2000;

static volatile bool driveEjectedByHost = false;
static volatile bool driveReservedForImport = false;

static void onDriveEjected(uint32_t)
{
    driveReservedForImport = true;
    driveEjectedByHost = true;
}

static bool isDriveReadyForHost(uint32_t)
{
    return !driveReservedForImport;
}

static bool copyValueIfItFits(char* destination, size_t capacity, const String& value)
{
    if (value.length() >= capacity) return false;
    memset(destination, 0, capacity);
    memcpy(destination, value.c_str(), value.length());
    return true;
}

static bool applyConfigValue(Credentials& credentials, const String& key, const String& value, int& recognisedKeys)
{
    if (key == "WIFI_SSID")
    {
        recognisedKeys++;
        return copyValueIfItFits(credentials.wifiSsid, sizeof(credentials.wifiSsid), value);
    }

    if (key == "WIFI_PASS")
    {
        recognisedKeys++;
        return copyValueIfItFits(credentials.wifiPassword, sizeof(credentials.wifiPassword), value);
    }

    return true;
}

static bool mergeConfigFile(File& configFile, Credentials& credentials)
{
    int recognisedKeys = 0;
    bool isFirstLine = true;

    while (configFile.available())
    {
        String line = configFile.readStringUntil('\n');

        if (isFirstLine && line.startsWith(UTF8_BYTE_ORDER_MARK))
        {
            line.remove(0, strlen(UTF8_BYTE_ORDER_MARK));
        }
        isFirstLine = false;

        if (line.endsWith("\r"))
        {
            line.remove(line.length() - 1);
        }

        const int separator = line.indexOf('=');
        if (separator < 0) continue;

        const String key = line.substring(0, separator);
        const String value = line.substring(separator + 1);
        if (!applyConfigValue(credentials, key, value, recognisedKeys)) return false;
    }

    return recognisedKeys > 0 && credentials.wifiSsid[0] != '\0';
}

static bool importConfigFile()
{
    File configFile = FatFS.open(CONFIG_FILE_PATH, "r");
    if (!configFile || configFile.size() > MAX_CONFIG_FILE_BYTES)
    {
        configFile.close();
        return false;
    }

    Credentials merged = credentialsCurrent();
    const bool isConfigValid = mergeConfigFile(configFile, merged);
    configFile.close();

    return isConfigValid && credentialsSave(merged);
}

static void eraseDriveStorage()
{
    const uint32_t flashOffset = reinterpret_cast<uintptr_t>(&_FS_start) - XIP_BASE;
    const size_t storageBytes = &_FS_end - &_FS_start;

    noInterrupts();
    rp2040.idleOtherCore();
    flash_range_erase(flashOffset, storageBytes);
    rp2040.resumeOtherCore();
    interrupts();
}

static void importConfigAndRestartIfPresent()
{
    if (!FatFS.exists(CONFIG_FILE_PATH)) return;

    const bool imported = importConfigFile();
    FatFS.end();

    displayMessage(imported ? "Config saved" : "Config rejected", "Restarting...");
    delay(IMPORT_RESULT_DISPLAY_MS);

    eraseDriveStorage();
    rp2040.reboot();
}

void usbConfigInit()
{
    if (!FatFS.begin())
    {
        Serial.println("USB drive: filesystem init failed");
        return;
    }

    importConfigAndRestartIfPresent();
    FatFS.end();

    FatFSUSB.onUnplug(onDriveEjected);
    FatFSUSB.driveReady(isDriveReadyForHost);
    FatFSUSB.begin();
}

void usbConfigTick()
{
    if (!driveEjectedByHost) return;
    driveEjectedByHost = false;

    if (FatFS.begin())
    {
        importConfigAndRestartIfPresent();
        FatFS.end();
    }

    driveReservedForImport = false;
}
