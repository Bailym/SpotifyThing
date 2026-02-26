#pragma once
#include <cstdint>

void displayInit();
void displayMessage(const char* line1, const char* line2 = nullptr);
void displaySetPlaying(bool isPlaying);
void displaySetProgress(uint32_t progressMs, uint32_t durationMs);
void displaySetVolume(int8_t volume);
void displayEnterClockMode();
void displayTick();
