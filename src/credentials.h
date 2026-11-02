#pragma once
#include <cstdint>

struct Credentials
{
    char wifiSsid[33];
    char wifiPassword[64];
};

enum class ImportOutcome : uint8_t
{
    None,
    Saved,
    FileUnreadable,
    FileTooLarge,
    MalformedLine,
    UnknownKey,
    InvalidSsid,
    InvalidPassword,
    NoRecognisedKeys,
    StorageWriteFailed
};

struct ImportResult
{
    ImportOutcome outcome;
    uint16_t lineNumber;
};

void credentialsLoad();
const Credentials& credentialsCurrent();
bool credentialsAreStored();
ImportResult credentialsLastImportResult();
bool credentialsSaveImport(const Credentials& credentials);
void credentialsSaveRejectedImport(const ImportResult& result);
