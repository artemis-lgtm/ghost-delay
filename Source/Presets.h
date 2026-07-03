#pragma once
#include <vector>
#include <utility>

/**
 * Ghost Audio — HAUNTED LOVE factory presets (rebuilt 2026-07-02).
 * The previous bank was the Ghost Machine reverb bank riding along in the
 * shell copy — 51 hall/plate/room presets driving warp parameters they were
 * never written for. This bank is native to the warp engine.
 *
 * Values are plain (denormalized) parameter values. Anything not listed
 * stays at the parameter default. Loaded via setCurrentProgram().
 *
 * Param IDs (real names, post 2026-07-02 wiring audit):
 *   amount - AMOUNT 0..1, default 0.4   (wow/flutter warp depth, in cents)
 *   rate   - RATE   0..1, default 0.3   (wow/flutter speed)
 *   filter - FILTER 0..1, default 0.6   (LPF base cutoff + breathing depth)
 *   mix    - MIX    0..1, default 0.35  (equal-power dry/wet)
 *   crush  - CRUSH  0..1, default 0.0   (dithered bit/rate crush)
 *   noise  - NOISE  0..1, default 0.2   (pink hiss bed, signal-modulated)
 *   width  - WIDTH  0..1, default 0.667 (M/S width 0..150%)
 *   drive  - DRIVE  0..1, default 0.3   (asymmetric tape saturation)
 */
struct FactoryPreset
{
    const char* category;
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "BASIC", "Init", {} },

        // ============================== TAPE ================================
        { "TAPE", "Gentle Cassette",   { { "amount", 0.18f }, { "rate", 0.20f }, { "filter", 0.62f }, { "mix", 0.45f }, { "noise", 0.12f }, { "drive", 0.25f } } },
        { "TAPE", "Worn Cassette",     { { "amount", 0.38f }, { "rate", 0.30f }, { "filter", 0.48f }, { "mix", 0.55f }, { "noise", 0.30f }, { "drive", 0.45f } } },
        { "TAPE", "Sun-Warped Reel",   { { "amount", 0.55f }, { "rate", 0.14f }, { "filter", 0.55f }, { "mix", 0.60f }, { "noise", 0.18f }, { "drive", 0.38f } } },
        { "TAPE", "Hot Deck",          { { "amount", 0.22f }, { "rate", 0.26f }, { "filter", 0.70f }, { "mix", 0.50f }, { "noise", 0.10f }, { "drive", 0.72f } } },

        // ============================== VHS =================================
        { "VHS", "Rental Copy",        { { "amount", 0.34f }, { "rate", 0.42f }, { "filter", 0.42f }, { "mix", 0.65f }, { "noise", 0.35f }, { "crush", 0.18f }, { "drive", 0.35f } } },
        { "VHS", "Tracking Error",     { { "amount", 0.62f }, { "rate", 0.55f }, { "filter", 0.38f }, { "mix", 0.75f }, { "noise", 0.42f }, { "crush", 0.28f } } },
        { "VHS", "Late-Night Rerun",   { { "amount", 0.28f }, { "rate", 0.35f }, { "filter", 0.34f }, { "mix", 0.58f }, { "noise", 0.48f }, { "width", 0.45f } } },

        // ============================== RADIO ===============================
        { "RADIO", "AM Drift",         { { "amount", 0.30f }, { "rate", 0.24f }, { "filter", 0.22f }, { "mix", 0.80f }, { "noise", 0.40f }, { "width", 0.20f }, { "drive", 0.50f } } },
        { "RADIO", "Numbers Station",  { { "amount", 0.45f }, { "rate", 0.60f }, { "filter", 0.26f }, { "mix", 0.90f }, { "noise", 0.55f }, { "crush", 0.35f }, { "width", 0.15f } } },

        // ============================== BROKEN ==============================
        { "BROKEN", "Dying Walkman",   { { "amount", 0.78f }, { "rate", 0.48f }, { "filter", 0.45f }, { "mix", 0.70f }, { "noise", 0.25f }, { "drive", 0.55f } } },
        { "BROKEN", "Chewed Tape",     { { "amount", 0.92f }, { "rate", 0.68f }, { "filter", 0.40f }, { "mix", 0.85f }, { "noise", 0.35f }, { "crush", 0.40f }, { "drive", 0.60f } } },
        { "BROKEN", "Haunted Console", { { "amount", 0.70f }, { "rate", 0.82f }, { "filter", 0.30f }, { "mix", 0.95f }, { "noise", 0.50f }, { "crush", 0.55f }, { "width", 0.90f } } },

        // ============================== SPECIAL =============================
        { "SPECIAL", "Dream Sequence", { { "amount", 0.48f }, { "rate", 0.16f }, { "filter", 0.58f }, { "mix", 0.65f }, { "noise", 0.08f }, { "width", 1.00f } } },
        { "SPECIAL", "Haunted Love",   { { "amount", 0.52f }, { "rate", 0.36f }, { "filter", 0.50f }, { "mix", 0.68f }, { "noise", 0.22f }, { "crush", 0.12f }, { "width", 0.85f }, { "drive", 0.42f } } },
        { "SPECIAL", "Lo-Fi Bed",      { { "amount", 0.26f }, { "rate", 0.28f }, { "filter", 0.36f }, { "mix", 0.55f }, { "crush", 0.30f }, { "noise", 0.28f }, { "width", 0.55f }, { "drive", 0.40f } } },
    };
    return presets;
}
