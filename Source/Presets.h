#pragma once
#include <vector>
#include <utility>

/**
 * Ghost Audio — HAUNTED LOVE factory presets (bank v2, 2026-07-03: Init + 50).
 * Native to the warp engine; the old Ghost Machine reverb bank is gone.
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
 *
 * Bank logic: within each category presets run subtle -> wrecked.
 * STUDIO is use-case (per-instrument, bus-safe mixes). GHOST is signature.
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
        { "TAPE", "Gentle Cassette",    { { "amount", 0.18f }, { "rate", 0.20f }, { "filter", 0.62f }, { "mix", 0.45f }, { "noise", 0.12f }, { "drive", 0.25f } } },
        { "TAPE", "Four-Track Demo",    { { "amount", 0.24f }, { "rate", 0.25f }, { "filter", 0.55f }, { "mix", 0.50f }, { "noise", 0.22f }, { "width", 0.50f }, { "drive", 0.40f } } },
        { "TAPE", "Cassette 1983",      { { "amount", 0.30f }, { "rate", 0.22f }, { "filter", 0.50f }, { "mix", 0.52f }, { "noise", 0.26f }, { "drive", 0.35f } } },
        { "TAPE", "Worn Cassette",      { { "amount", 0.38f }, { "rate", 0.30f }, { "filter", 0.48f }, { "mix", 0.55f }, { "noise", 0.30f }, { "drive", 0.45f } } },
        { "TAPE", "Hot Deck",           { { "amount", 0.22f }, { "rate", 0.26f }, { "filter", 0.70f }, { "mix", 0.50f }, { "noise", 0.10f }, { "drive", 0.72f } } },
        { "TAPE", "First-Gen Dub",      { { "amount", 0.42f }, { "rate", 0.28f }, { "filter", 0.44f }, { "mix", 0.62f }, { "noise", 0.34f }, { "width", 0.55f }, { "drive", 0.48f } } },
        { "TAPE", "Sun-Warped Reel",    { { "amount", 0.55f }, { "rate", 0.14f }, { "filter", 0.55f }, { "mix", 0.60f }, { "noise", 0.18f }, { "drive", 0.38f } } },
        { "TAPE", "Attic Reel",         { { "amount", 0.60f }, { "rate", 0.20f }, { "filter", 0.38f }, { "mix", 0.70f }, { "noise", 0.38f }, { "width", 0.60f }, { "drive", 0.42f } } },

        // ============================== VHS =================================
        { "VHS", "Camcorder Summer",    { { "amount", 0.26f }, { "rate", 0.38f }, { "filter", 0.46f }, { "mix", 0.55f }, { "noise", 0.28f }, { "crush", 0.12f } } },
        { "VHS", "Rental Copy",         { { "amount", 0.34f }, { "rate", 0.42f }, { "filter", 0.42f }, { "mix", 0.65f }, { "noise", 0.35f }, { "crush", 0.18f }, { "drive", 0.35f } } },
        { "VHS", "Late-Night Rerun",    { { "amount", 0.28f }, { "rate", 0.35f }, { "filter", 0.34f }, { "mix", 0.58f }, { "noise", 0.48f }, { "width", 0.45f } } },
        { "VHS", "Wedding Tape",        { { "amount", 0.40f }, { "rate", 0.32f }, { "filter", 0.40f }, { "mix", 0.68f }, { "noise", 0.40f }, { "crush", 0.20f }, { "width", 0.50f } } },
        { "VHS", "Ex-Rental Horror",    { { "amount", 0.52f }, { "rate", 0.48f }, { "filter", 0.32f }, { "mix", 0.75f }, { "noise", 0.45f }, { "crush", 0.26f }, { "drive", 0.40f } } },
        { "VHS", "Tracking Error",      { { "amount", 0.62f }, { "rate", 0.55f }, { "filter", 0.38f }, { "mix", 0.75f }, { "noise", 0.42f }, { "crush", 0.28f } } },
        { "VHS", "Pause Damage",        { { "amount", 0.74f }, { "rate", 0.60f }, { "filter", 0.35f }, { "mix", 0.82f }, { "noise", 0.46f }, { "crush", 0.34f }, { "width", 0.40f } } },

        // ============================== RADIO ===============================
        { "RADIO", "Clock Radio",       { { "amount", 0.20f }, { "rate", 0.30f }, { "filter", 0.28f }, { "mix", 0.70f }, { "noise", 0.30f }, { "width", 0.25f }, { "drive", 0.42f } } },
        { "RADIO", "AM Drift",          { { "amount", 0.30f }, { "rate", 0.24f }, { "filter", 0.22f }, { "mix", 0.80f }, { "noise", 0.40f }, { "width", 0.20f }, { "drive", 0.50f } } },
        { "RADIO", "Border Blaster",    { { "amount", 0.36f }, { "rate", 0.34f }, { "filter", 0.25f }, { "mix", 0.85f }, { "noise", 0.45f }, { "width", 0.18f }, { "drive", 0.60f } } },
        { "RADIO", "Shortwave Sermon",  { { "amount", 0.42f }, { "rate", 0.50f }, { "filter", 0.20f }, { "mix", 0.88f }, { "noise", 0.52f }, { "crush", 0.22f }, { "width", 0.12f }, { "drive", 0.55f } } },
        { "RADIO", "Numbers Station",   { { "amount", 0.45f }, { "rate", 0.60f }, { "filter", 0.26f }, { "mix", 0.90f }, { "noise", 0.55f }, { "crush", 0.35f }, { "width", 0.15f } } },

        // ============================== DREAM ===============================
        { "DREAM", "Half Asleep",       { { "amount", 0.22f }, { "rate", 0.12f }, { "filter", 0.56f }, { "mix", 0.48f }, { "noise", 0.08f }, { "width", 0.80f } } },
        { "DREAM", "Dream Sequence",    { { "amount", 0.48f }, { "rate", 0.16f }, { "filter", 0.58f }, { "mix", 0.65f }, { "noise", 0.08f }, { "width", 1.00f } } },
        { "DREAM", "Slow Dance Underwater", { { "amount", 0.55f }, { "rate", 0.10f }, { "filter", 0.30f }, { "mix", 0.75f }, { "noise", 0.10f }, { "width", 0.85f } } },
        { "DREAM", "Memory Fade",       { { "amount", 0.44f }, { "rate", 0.18f }, { "filter", 0.42f }, { "mix", 0.70f }, { "noise", 0.16f }, { "width", 0.75f }, { "drive", 0.30f } } },
        { "DREAM", "Lucid",             { { "amount", 0.36f }, { "rate", 0.14f }, { "filter", 0.66f }, { "mix", 0.60f }, { "noise", 0.05f }, { "width", 0.95f }, { "drive", 0.20f } } },
        { "DREAM", "Sleep Paralysis",   { { "amount", 0.68f }, { "rate", 0.22f }, { "filter", 0.34f }, { "mix", 0.85f }, { "noise", 0.20f }, { "width", 0.90f }, { "drive", 0.35f } } },
        { "DREAM", "Astral",            { { "amount", 0.58f }, { "rate", 0.08f }, { "filter", 0.48f }, { "mix", 0.80f }, { "noise", 0.06f }, { "width", 1.00f } } },

        // ============================== LO-FI ===============================
        { "LOFI", "Lo-Fi Bed",          { { "amount", 0.26f }, { "rate", 0.28f }, { "filter", 0.36f }, { "mix", 0.55f }, { "crush", 0.30f }, { "noise", 0.28f }, { "width", 0.55f }, { "drive", 0.40f } } },
        { "LOFI", "Study Beats",        { { "amount", 0.30f }, { "rate", 0.24f }, { "filter", 0.40f }, { "mix", 0.58f }, { "crush", 0.24f }, { "noise", 0.24f }, { "width", 0.60f }, { "drive", 0.35f } } },
        { "LOFI", "Thrift Store Sampler", { { "amount", 0.38f }, { "rate", 0.32f }, { "filter", 0.34f }, { "mix", 0.66f }, { "crush", 0.42f }, { "noise", 0.32f }, { "width", 0.50f }, { "drive", 0.45f } } },
        { "LOFI", "Bitcrushed Heart",   { { "amount", 0.32f }, { "rate", 0.36f }, { "filter", 0.44f }, { "mix", 0.70f }, { "crush", 0.55f }, { "noise", 0.20f }, { "drive", 0.38f } } },
        { "LOFI", "8-Bit Ghost",        { { "amount", 0.28f }, { "rate", 0.44f }, { "filter", 0.52f }, { "mix", 0.78f }, { "crush", 0.72f }, { "noise", 0.15f }, { "width", 0.45f } } },

        // ============================== BROKEN ==============================
        { "BROKEN", "Last Battery",     { { "amount", 0.58f }, { "rate", 0.16f }, { "filter", 0.42f }, { "mix", 0.72f }, { "noise", 0.22f }, { "drive", 0.45f } } },
        { "BROKEN", "Dying Walkman",    { { "amount", 0.78f }, { "rate", 0.48f }, { "filter", 0.45f }, { "mix", 0.70f }, { "noise", 0.25f }, { "drive", 0.55f } } },
        { "BROKEN", "Water Damage",     { { "amount", 0.72f }, { "rate", 0.38f }, { "filter", 0.28f }, { "mix", 0.80f }, { "noise", 0.38f }, { "crush", 0.30f }, { "drive", 0.50f } } },
        { "BROKEN", "Chewed Tape",      { { "amount", 0.92f }, { "rate", 0.68f }, { "filter", 0.40f }, { "mix", 0.85f }, { "noise", 0.35f }, { "crush", 0.40f }, { "drive", 0.60f } } },
        { "BROKEN", "Haunted Console",  { { "amount", 0.70f }, { "rate", 0.82f }, { "filter", 0.30f }, { "mix", 0.95f }, { "noise", 0.50f }, { "crush", 0.55f }, { "width", 0.90f } } },
        { "BROKEN", "Eaten By The Deck", { { "amount", 1.00f }, { "rate", 0.75f }, { "filter", 0.32f }, { "mix", 0.92f }, { "noise", 0.42f }, { "crush", 0.48f }, { "drive", 0.68f } } },
        { "BROKEN", "Demagnetized",     { { "amount", 0.85f }, { "rate", 0.90f }, { "filter", 0.24f }, { "mix", 1.00f }, { "noise", 0.60f }, { "crush", 0.60f }, { "width", 0.30f }, { "drive", 0.65f } } },

        // ============================== STUDIO ==============================
        { "STUDIO", "Vocal Ghosting",   { { "amount", 0.16f }, { "rate", 0.18f }, { "filter", 0.64f }, { "mix", 0.30f }, { "noise", 0.06f }, { "width", 0.70f }, { "drive", 0.28f } } },
        { "STUDIO", "Vocal Séance",     { { "amount", 0.34f }, { "rate", 0.26f }, { "filter", 0.48f }, { "mix", 0.45f }, { "noise", 0.14f }, { "width", 0.80f }, { "drive", 0.35f } } },
        { "STUDIO", "Keys To Dust",     { { "amount", 0.36f }, { "rate", 0.22f }, { "filter", 0.52f }, { "mix", 0.55f }, { "noise", 0.18f }, { "width", 0.75f }, { "drive", 0.38f } } },
        { "STUDIO", "Guitar Locket",    { { "amount", 0.28f }, { "rate", 0.20f }, { "filter", 0.58f }, { "mix", 0.48f }, { "noise", 0.12f }, { "width", 0.65f }, { "drive", 0.45f } } },
        { "STUDIO", "Bass Wobble Light", { { "amount", 0.14f }, { "rate", 0.16f }, { "filter", 0.60f }, { "mix", 0.35f }, { "noise", 0.04f }, { "width", 0.35f }, { "drive", 0.40f } } },
        { "STUDIO", "Drum Bus Rot",     { { "amount", 0.20f }, { "rate", 0.30f }, { "filter", 0.50f }, { "mix", 0.32f }, { "crush", 0.22f }, { "noise", 0.16f }, { "width", 0.55f }, { "drive", 0.50f } } },
        { "STUDIO", "Mix Glue Nostalgia", { { "amount", 0.12f }, { "rate", 0.20f }, { "filter", 0.56f }, { "mix", 0.25f }, { "noise", 0.08f }, { "width", 0.62f }, { "drive", 0.35f } } },
        { "STUDIO", "Parallel Wreck",   { { "amount", 0.66f }, { "rate", 0.52f }, { "filter", 0.36f }, { "mix", 0.40f }, { "crush", 0.35f }, { "noise", 0.30f }, { "drive", 0.55f } } },

        // ============================== GHOST ===============================
        { "GHOST", "White Sheet",       { { "amount", 0.24f }, { "rate", 0.24f }, { "filter", 0.60f }, { "mix", 0.50f }, { "noise", 0.10f }, { "width", 0.85f } } },
        { "GHOST", "Cold Spot",         { { "amount", 0.40f }, { "rate", 0.12f }, { "filter", 0.38f }, { "mix", 0.62f }, { "noise", 0.14f }, { "width", 0.90f }, { "drive", 0.25f } } },
        { "GHOST", "Orb Footage",       { { "amount", 0.46f }, { "rate", 0.40f }, { "filter", 0.42f }, { "mix", 0.68f }, { "noise", 0.36f }, { "crush", 0.24f }, { "width", 0.60f } } },
        { "GHOST", "Séance Circle",     { { "amount", 0.54f }, { "rate", 0.30f }, { "filter", 0.35f }, { "mix", 0.78f }, { "noise", 0.28f }, { "width", 0.95f }, { "drive", 0.40f } } },
        { "GHOST", "Poltergeist",       { { "amount", 0.82f }, { "rate", 0.64f }, { "filter", 0.44f }, { "mix", 0.88f }, { "noise", 0.32f }, { "crush", 0.38f }, { "width", 1.00f }, { "drive", 0.58f } } },
        { "GHOST", "Ectoplasm",         { { "amount", 0.64f }, { "rate", 0.18f }, { "filter", 0.26f }, { "mix", 0.90f }, { "noise", 0.24f }, { "width", 1.00f }, { "drive", 0.45f } } },
        { "GHOST", "Haunted Love",      { { "amount", 0.52f }, { "rate", 0.36f }, { "filter", 0.50f }, { "mix", 0.68f }, { "noise", 0.22f }, { "crush", 0.12f }, { "width", 0.85f }, { "drive", 0.42f } } },
    };
    return presets;
}

// Category of a preset index, for grouped preset menus.
inline const char* getFactoryPresetCategory(int index)
{
    const auto& p = getFactoryPresets();
    return (index >= 0 && index < (int) p.size()) ? p[(size_t) index].category : "";
}
