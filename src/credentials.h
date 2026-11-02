#pragma once

struct Credentials
{
    char wifiSsid[33];
    char wifiPassword[64];
};

void credentialsLoad();
const Credentials& credentialsCurrent();
bool credentialsSave(const Credentials& credentials);
