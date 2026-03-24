#pragma once

constexpr int kMinSliceLengthSamples = 64;
constexpr int kMidiNoteCount = 128;
constexpr int kMaxMidiNote = kMidiNoteCount - 1;
constexpr int kDefaultRootNote = 36;
constexpr int kMaxOutputBuses = 16;
constexpr int kMaxMuteGroups = 32;

constexpr float kMinFilterCutoffHz = 20.0f;
constexpr float kMaxFilterCutoffHz = 20000.0f;
