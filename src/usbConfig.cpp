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
static constexpr const char* STATUS_FILE_PATH = "status.txt";
static constexpr const char* UTF8_BYTE_ORDER_MARK = "\xEF\xBB\xBF";
static constexpr size_t MAX_CONFIG_FILE_BYTES = 4096;
static constexpr unsigned int IMPORT_RESULT_DISPLAY_MS = 2000;
static constexpr size_t MIN_WIFI_PASSWORD_BYTES = 8;
static constexpr ImportOutcome LINE_ACCEPTED = ImportOutcome::Saved;

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

static void copyValue(char* destination, size_t capacity, const String& value)
{
    memset(destination, 0, capacity);
    memcpy(destination, value.c_str(), value.length());
}

static ImportOutcome applyWifiSsid(Credentials& credentials, const String& value)
{
    const bool lengthIsValid = value.length() > 0 && value.length() < sizeof(credentials.wifiSsid);
    if (!lengthIsValid) return ImportOutcome::InvalidSsid;

    copyValue(credentials.wifiSsid, sizeof(credentials.wifiSsid), value);
    return LINE_ACCEPTED;
}

static ImportOutcome applyWifiPassword(Credentials& credentials, const String& value)
{
    const bool isOpenNetwork = value.length() == 0;
    const bool lengthIsValid = value.length() >= MIN_WIFI_PASSWORD_BYTES && value.length() < sizeof(credentials.wifiPassword);
    if (!isOpenNetwork && !lengthIsValid) return ImportOutcome::InvalidPassword;

    copyValue(credentials.wifiPassword, sizeof(credentials.wifiPassword), value);
    return LINE_ACCEPTED;
}

static ImportOutcome applySpotifyValue(char* destination, size_t capacity, const String& value)
{
    const bool lengthIsValid = value.length() > 0 && value.length() < capacity;
    if (!lengthIsValid) return ImportOutcome::InvalidSpotifyValue;

    copyValue(destination, capacity, value);
    return LINE_ACCEPTED;
}

static ImportOutcome applyConfigLine(Credentials& credentials, const String& line)
{
    const int separator = line.indexOf('=');
    if (separator < 0) return ImportOutcome::MalformedLine;

    const String key = line.substring(0, separator);
    const String value = line.substring(separator + 1);

    if (key == "WIFI_SSID") return applyWifiSsid(credentials, value);
    if (key == "WIFI_PASS") return applyWifiPassword(credentials, value);
    if (key == "SPOTIFY_CLIENT_ID") return applySpotifyValue(credentials.spotifyClientId, sizeof(credentials.spotifyClientId), value);
    if (key == "SPOTIFY_CLIENT_SECRET") return applySpotifyValue(credentials.spotifyClientSecret, sizeof(credentials.spotifyClientSecret), value);
    if (key == "SPOTIFY_REFRESH_TOKEN") return applySpotifyValue(credentials.spotifyRefreshToken, sizeof(credentials.spotifyRefreshToken), value);
    return ImportOutcome::UnknownKey;
}

static void removeLineFormatting(String& line, bool isFirstLine)
{
    if (isFirstLine && line.startsWith(UTF8_BYTE_ORDER_MARK))
    {
        line.remove(0, strlen(UTF8_BYTE_ORDER_MARK));
    }

    if (line.endsWith("\r"))
    {
        line.remove(line.length() - 1);
    }
}

static ImportResult mergeConfigFile(File& configFile, Credentials& credentials)
{
    uint16_t lineNumber = 0;
    int acceptedLines = 0;

    while (configFile.available())
    {
        String line = configFile.readStringUntil('\n');
        lineNumber++;
        removeLineFormatting(line, lineNumber == 1);

        if (line.length() == 0) continue;

        const ImportOutcome lineOutcome = applyConfigLine(credentials, line);
        if (lineOutcome != LINE_ACCEPTED) return {lineOutcome, lineNumber};
        acceptedLines++;
    }

    if (acceptedLines == 0) return {ImportOutcome::NoRecognisedKeys, 0};
    if (credentials.wifiSsid[0] == '\0') return {ImportOutcome::InvalidSsid, 0};
    return {ImportOutcome::Saved, 0};
}

static ImportResult readConfigFile(Credentials& merged)
{
    File configFile = FatFS.open(CONFIG_FILE_PATH, "r");
    if (!configFile) return {ImportOutcome::FileUnreadable, 0};

    if (configFile.size() > MAX_CONFIG_FILE_BYTES)
    {
        configFile.close();
        return {ImportOutcome::FileTooLarge, 0};
    }

    const ImportResult result = mergeConfigFile(configFile, merged);
    configFile.close();
    return result;
}

static bool importConfigFile()
{
    Credentials merged = credentialsCurrent();
    const ImportResult result = readConfigFile(merged);

    if (result.outcome != ImportOutcome::Saved)
    {
        credentialsSaveRejectedImport(result);
        return false;
    }

    if (!credentialsSaveImport(merged))
    {
        credentialsSaveRejectedImport({ImportOutcome::StorageWriteFailed, 0});
        return false;
    }

    return true;
}

static const char* describeImportOutcome(ImportOutcome outcome)
{
    switch (outcome)
    {
        case ImportOutcome::None:               return "none";
        case ImportOutcome::Saved:              return "saved";
        case ImportOutcome::FileUnreadable:     return "rejected: file could not be read";
        case ImportOutcome::FileTooLarge:       return "rejected: file is larger than 4 KB";
        case ImportOutcome::MalformedLine:      return "rejected: expected KEY=VALUE";
        case ImportOutcome::UnknownKey:         return "rejected: unknown key";
        case ImportOutcome::InvalidSsid:        return "rejected: WIFI_SSID must be 1-32 bytes";
        case ImportOutcome::InvalidPassword:    return "rejected: WIFI_PASS must be empty or 8-63 bytes";
        case ImportOutcome::NoRecognisedKeys:   return "rejected: no settings found";
        case ImportOutcome::StorageWriteFailed: return "rejected: could not save settings";
        case ImportOutcome::InvalidSpotifyValue: return "rejected: Spotify value is empty or too long";
    }
    return "unknown";
}

static String describeSecretState(const char* value)
{
    return value[0] != '\0' ? "set" : "not set";
}

static String buildStatusText()
{
    const Credentials& credentials = credentialsCurrent();
    const ImportResult lastImport = credentialsLastImportResult();

    String text = "WIFI_SSID: " + String(credentials.wifiSsid) + "\r\n";
    text += "WIFI_PASS: " + describeSecretState(credentials.wifiPassword) + "\r\n";
    text += "SPOTIFY_CLIENT_ID: " + describeSecretState(credentials.spotifyClientId) + "\r\n";
    text += "SPOTIFY_CLIENT_SECRET: " + describeSecretState(credentials.spotifyClientSecret) + "\r\n";
    text += "SPOTIFY_REFRESH_TOKEN: " + describeSecretState(credentials.spotifyRefreshToken) + "\r\n";
    text += "Credentials from: " + String(credentialsAreStored() ? "config.txt" : "firmware") + "\r\n";
    text += "Last import: " + String(describeImportOutcome(lastImport.outcome));
    if (lastImport.lineNumber > 0)
    {
        text += " (line " + String(lastImport.lineNumber) + ")";
    }
    text += "\r\n";
    return text;
}

static String readStatusFile()
{
    File statusFile = FatFS.open(STATUS_FILE_PATH, "r");
    if (!statusFile) return "";

    const String text = statusFile.readString();
    statusFile.close();
    return text;
}

static void writeStatusFileIfChanged()
{
    const String text = buildStatusText();
    if (readStatusFile() == text) return;

    File statusFile = FatFS.open(STATUS_FILE_PATH, "w");
    if (!statusFile) return;

    statusFile.print(text);
    statusFile.close();
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
    writeStatusFileIfChanged();
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
