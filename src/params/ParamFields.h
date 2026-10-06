#pragma once

/** Field identifiers shared by slice edits (CmdSetSliceParam) and sample edits (CmdSetSampleParam).

    IntersectProcessor inherits this so existing code keeps writing IntersectProcessor::FieldBpm.
    The numeric values are also the keys of the per-sample parameter block in saved state
    (extension v7), so they must never be renumbered: append new fields at the end only.
*/
struct ParamFieldIds
{
    enum SliceParamField
    {
        FieldBpm = 0,
        FieldPitch,
        FieldAlgorithm,
        FieldRepitchMode,
        FieldAttack,
        FieldDecay,
        FieldSustain,
        FieldRelease,
        FieldMuteGroup,
        FieldMidiNote,
        FieldStretchEnabled,
        FieldTonality,
        FieldFormant,
        FieldFormantComp,
        FieldGrainMode,
        FieldVolume,
        FieldReleaseTail,
        FieldReverse,
        FieldOutputBus,
        FieldLoop,
        FieldOneShot,
        FieldCentsDetune,
        FieldFilterEnabled,
        FieldFilterType,
        FieldFilterSlope,
        FieldFilterCutoff,
        FieldFilterReso,
        FieldFilterDrive,
        FieldFilterKeyTrack,
        FieldFilterEnvAttack,
        FieldFilterEnvDecay,
        FieldFilterEnvSustain,
        FieldFilterEnvRelease,
        FieldFilterEnvAmount,
        FieldFilterAsym,
        FieldCrossfade,
        FieldLoopStart,
        FieldLoopLength,
        FieldHighNote,
        FieldSliceRootNote,
    };
};
