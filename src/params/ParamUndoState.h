#pragma once

/** Plugin-level APVTS values captured for undo. Sample-level values are per sample and live in
    UndoManager::Snapshot::sampleParams instead. */
struct ParamUndoState
{
    float maxVoices = 16.0f;
};
