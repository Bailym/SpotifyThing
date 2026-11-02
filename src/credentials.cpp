#include "credentials.h"
#include <Arduino.h>
#include <EEPROM.h>

static constexpr uint32_t RECORD_MAGIC = 0x43524432;
static constexpr uint32_t CRC32_INITIAL = 0xFFFFFFFF;
static constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;
static constexpr int BITS_PER_BYTE = 8;
static constexpr int RECORD_EEPROM_ADDRESS = 0;
static constexpr uint32_t IMPORT_RESULT_MAGIC = 0x494D5031;
static constexpr int IMPORT_RESULT_EEPROM_ADDRESS = 1008;
static constexpr size_t EEPROM_BYTES = 1024;

struct CredentialsRecord
{
    uint32_t magic;
    uint32_t crc;
    Credentials credentials;
};

struct ImportResultRecord
{
    uint32_t magic;
    ImportResult result;
};

static Credentials currentCredentials;
static bool credentialsStored = false;
static ImportResult lastImportResult = {ImportOutcome::None, 0};

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

static void loadLastImportResult()
{
    ImportResultRecord record;
    EEPROM.get(IMPORT_RESULT_EEPROM_ADDRESS, record);
    if (record.magic == IMPORT_RESULT_MAGIC)
    {
        lastImportResult = record.result;
    }
}

static void stageImportResult(const ImportResult& result)
{
    ImportResultRecord record;
    record.magic = IMPORT_RESULT_MAGIC;
    record.result = result;
    EEPROM.put(IMPORT_RESULT_EEPROM_ADDRESS, record);
}

void credentialsLoad()
{
    EEPROM.begin(EEPROM_BYTES);

    CredentialsRecord record;
    EEPROM.get(RECORD_EEPROM_ADDRESS, record);

    credentialsStored = isRecordValid(record);
    if (credentialsStored)
    {
        currentCredentials = record.credentials;
    }
    else
    {
        memset(&currentCredentials, 0, sizeof(currentCredentials));
    }

    loadLastImportResult();
}

const Credentials& credentialsCurrent()
{
    return currentCredentials;
}

bool credentialsAreStored()
{
    return credentialsStored;
}

bool credentialsHaveWifi()
{
    return currentCredentials.wifiSsid[0] != '\0';
}

bool credentialsHaveSpotify()
{
    return currentCredentials.spotifyClientId[0] != '\0'
        && currentCredentials.spotifyClientSecret[0] != '\0'
        && currentCredentials.spotifyRefreshToken[0] != '\0';
}

ImportResult credentialsLastImportResult()
{
    return lastImportResult;
}

bool credentialsSaveImport(const Credentials& credentials)
{
    CredentialsRecord record;
    record.magic = RECORD_MAGIC;
    record.crc = calculateCrc32(credentials);
    record.credentials = credentials;

    EEPROM.put(RECORD_EEPROM_ADDRESS, record);
    stageImportResult({ImportOutcome::Saved, 0});
    return EEPROM.commit();
}

void credentialsSaveRejectedImport(const ImportResult& result)
{
    stageImportResult(result);
    EEPROM.commit();
}
