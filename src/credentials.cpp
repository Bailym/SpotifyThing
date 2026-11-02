#include "credentials.h"
#include "secrets.h"
#include <Arduino.h>
#include <EEPROM.h>

static constexpr uint32_t RECORD_MAGIC = 0x43524431;
static constexpr uint32_t CRC32_INITIAL = 0xFFFFFFFF;
static constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;
static constexpr int BITS_PER_BYTE = 8;
static constexpr int RECORD_EEPROM_ADDRESS = 0;

struct CredentialsRecord
{
    uint32_t magic;
    uint32_t crc;
    Credentials credentials;
};

static Credentials currentCredentials;

static uint32_t calculateCrc32(const Credentials& credentials)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&credentials);
    uint32_t crc = CRC32_INITIAL;

    for (size_t i = 0; i < sizeof(Credentials); i++)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < BITS_PER_BYTE; bit++)
        {
            const bool lowestBitSet = crc & 1;
            crc >>= 1;
            if (lowestBitSet) crc ^= CRC32_POLYNOMIAL;
        }
    }

    return ~crc;
}

static bool isRecordValid(const CredentialsRecord& record)
{
    return record.magic == RECORD_MAGIC && record.crc == calculateCrc32(record.credentials);
}

static void loadFirmwareDefaults()
{
    memset(&currentCredentials, 0, sizeof(currentCredentials));
    strncpy(currentCredentials.wifiSsid, WIFI_SSID, sizeof(currentCredentials.wifiSsid) - 1);
    strncpy(currentCredentials.wifiPassword, WIFI_PASS, sizeof(currentCredentials.wifiPassword) - 1);
}

void credentialsLoad()
{
    EEPROM.begin(sizeof(CredentialsRecord));

    CredentialsRecord record;
    EEPROM.get(RECORD_EEPROM_ADDRESS, record);

    if (isRecordValid(record))
    {
        currentCredentials = record.credentials;
    }
    else
    {
        loadFirmwareDefaults();
    }
}

const Credentials& credentialsCurrent()
{
    return currentCredentials;
}

bool credentialsSave(const Credentials& credentials)
{
    CredentialsRecord record;
    record.magic = RECORD_MAGIC;
    record.crc = calculateCrc32(credentials);
    record.credentials = credentials;

    EEPROM.put(RECORD_EEPROM_ADDRESS, record);
    return EEPROM.commit();
}
