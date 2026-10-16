#pragma once

namespace ids
{
    // Global
    inline constexpr auto inputGain = "global_input_gain";
    inline constexpr auto outputGain = "global_output_gain";
    inline constexpr auto globalWet = "global_wet_dry";
    inline constexpr auto globalBypass = "global_bypass";

    // FX17-style input stage
    inline constexpr auto wahBypass = "fx17_bypass";
    inline constexpr auto wahMode = "fx17_mode";
    inline constexpr auto wahContour = "fx17_contour";
    inline constexpr auto wahPosition = "fx17_position";

    // GE-7-style EQ
    inline constexpr auto eqBypass = "ge7_bypass";
    inline constexpr auto eq100 = "ge7_100";
    inline constexpr auto eq200 = "ge7_200";
    inline constexpr auto eq400 = "ge7_400";
    inline constexpr auto eq800 = "ge7_800";
    inline constexpr auto eq1600 = "ge7_1600";
    inline constexpr auto eq3200 = "ge7_3200";
    inline constexpr auto eq6400 = "ge7_6400";
    inline constexpr auto eqLevel = "ge7_level";

    // Tuner
    inline constexpr auto tunerBypass = "tuner_bypass";
    inline constexpr auto tunerMode = "tuner_mode";
    inline constexpr auto tunerCalibration = "tuner_calibration";
    inline constexpr auto tunerMute = "tuner_mute";

    // PS-6-style pitch / harmony
    inline constexpr auto pitchBypass = "ps6_bypass";
    inline constexpr auto pitchMode = "ps6_mode";
    inline constexpr auto pitchKey = "ps6_key";
    inline constexpr auto pitchScale = "ps6_scale";
    inline constexpr auto pitchInterval = "ps6_interval";
    inline constexpr auto pitchHarmonyVoice = "ps6_harmony_voice";
    inline constexpr auto pitchBalance = "ps6_balance";
    inline constexpr auto pitchRise = "ps6_rise_time";
    inline constexpr auto pitchBend = "ps6_bend_amount";

    // Flashback-style delay
    inline constexpr auto delayBypass = "flashback_bypass";
    inline constexpr auto delayType = "flashback_type";
    inline constexpr auto delayTime = "flashback_time_ms";
    inline constexpr auto delayFeedback = "flashback_feedback";
    inline constexpr auto delayLevel = "flashback_level";
    inline constexpr auto delaySubdivision = "flashback_subdivision";
    inline constexpr auto delayMash = "flashback_mash";

    // Looper
    inline constexpr auto looperBypass = "jamman_bypass";
    inline constexpr auto loopLevel = "jamman_level";
    inline constexpr auto loopRecord = "jamman_record";
    inline constexpr auto loopPlay = "jamman_play";
    inline constexpr auto loopOverdub = "jamman_overdub";
    inline constexpr auto loopStop = "jamman_stop";
    inline constexpr auto loopClear = "jamman_clear";
    inline constexpr auto loopReverse = "jamman_reverse";
    inline constexpr auto loopTempoSync = "jamman_tempo_sync";
    inline constexpr auto loopQuantize = "jamman_quantize";
    inline constexpr auto loopBpm = "jamman_bpm";
    inline constexpr auto loopTracks = "jamman_layers";

    // Holy Grail-style ambience
    inline constexpr auto reverbBypass = "grail_bypass";
    inline constexpr auto reverbType = "grail_algorithm";
    inline constexpr auto reverbAmount = "grail_amount";
}
