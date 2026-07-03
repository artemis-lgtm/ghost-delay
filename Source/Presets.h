#pragma once
#include <vector>
#include <utility>

/**
 * Ghost Machine — factory presets.
 * Values are plain (denormalized) parameter values. Anything not listed
 * stays at the parameter default. Loaded via setCurrentProgram(); the host's
 * built-in preset selector shows getProgramName() for each entry, so no
 * faceplate UI change was required (Austin 6/14: "don't mess anything up
 * with the pedal — just high-quality presets").
 *
 * Param IDs (legacy names, current label in CAPS):
 *   time     - SIZE     0..1, default 0.5
 *   feedback - DECAY    0..1, default 0.4
 *   decay    - TONE     0..1, default 0.6   (higher = brighter)
 *   tone     - MIX      0..1, default 0.35
 *   rate     - SHIMMER  0..1, default 0.0   (octave-up tail regen)
 *   depth    - DUCK     0..1, default 0.0   (input ducks the wet)
 *   spread   - WIDTH    0..1, default 0.667 (M/S width 0..150%)
 *   mix      - GRIT     0..1, default 0.0   (wet saturation + darken)
 *
 * Categories follow the Valhalla bank structure (Halls, Plates, Rooms,
 * Chambers, Ambiences, Shimmer, Special). Each preset is a deliberate
 * combination — no near-duplicates.
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

        // =============================== HALLS ==============================
        { "HALL", "Small Hall",          { { "time", 0.30f }, { "feedback", 0.35f }, { "decay", 0.65f }, { "tone", 0.28f }, { "spread", 0.70f } } },
        { "HALL", "Medium Hall",         { { "time", 0.50f }, { "feedback", 0.50f }, { "decay", 0.60f }, { "tone", 0.34f }, { "spread", 0.75f } } },
        { "HALL", "Large Hall",          { { "time", 0.72f }, { "feedback", 0.65f }, { "decay", 0.55f }, { "tone", 0.36f }, { "spread", 0.80f } } },
        { "HALL", "Cathedral",           { { "time", 0.92f }, { "feedback", 0.82f }, { "decay", 0.45f }, { "tone", 0.40f }, { "rate", 0.18f }, { "spread", 0.85f } } },
        { "HALL", "Stone Chapel",        { { "time", 0.78f }, { "feedback", 0.74f }, { "decay", 0.32f }, { "tone", 0.34f }, { "spread", 0.65f }, { "mix", 0.15f } } },
        { "HALL", "Echo Hall",           { { "time", 0.62f }, { "feedback", 0.78f }, { "decay", 0.58f }, { "tone", 0.38f }, { "spread", 0.78f } } },
        { "HALL", "Bright Hall",         { { "time", 0.58f }, { "feedback", 0.55f }, { "decay", 0.80f }, { "tone", 0.40f }, { "spread", 0.82f } } },
        { "HALL", "Vintage Hall",        { { "time", 0.55f }, { "feedback", 0.55f }, { "decay", 0.40f }, { "tone", 0.32f }, { "spread", 0.65f }, { "mix", 0.30f } } },
        { "HALL", "Ambient Hall",        { { "time", 0.80f }, { "feedback", 0.68f }, { "decay", 0.55f }, { "tone", 0.22f }, { "spread", 0.90f } } },
        { "HALL", "Wide Hall",           { { "time", 0.65f }, { "feedback", 0.60f }, { "decay", 0.62f }, { "tone", 0.38f }, { "spread", 1.00f } } },

        // ============================== PLATES ==============================
        { "PLATE", "Vintage Plate",      { { "time", 0.22f }, { "feedback", 0.45f }, { "decay", 0.50f }, { "tone", 0.32f }, { "spread", 0.55f }, { "mix", 0.28f } } },
        { "PLATE", "Studio Plate",       { { "time", 0.35f }, { "feedback", 0.50f }, { "decay", 0.65f }, { "tone", 0.32f }, { "spread", 0.70f } } },
        { "PLATE", "Dark Plate",         { { "time", 0.40f }, { "feedback", 0.55f }, { "decay", 0.30f }, { "tone", 0.30f }, { "spread", 0.65f }, { "mix", 0.18f } } },
        { "PLATE", "Bright Plate",       { { "time", 0.28f }, { "feedback", 0.42f }, { "decay", 0.85f }, { "tone", 0.36f }, { "spread", 0.72f } } },
        { "PLATE", "Vocal Plate",        { { "time", 0.42f }, { "feedback", 0.58f }, { "decay", 0.62f }, { "tone", 0.30f }, { "spread", 0.68f } } },
        { "PLATE", "Drum Plate",         { { "time", 0.20f }, { "feedback", 0.32f }, { "decay", 0.55f }, { "tone", 0.30f }, { "depth", 0.35f }, { "spread", 0.62f } } },
        { "PLATE", "Snare Plate",        { { "time", 0.15f }, { "feedback", 0.28f }, { "decay", 0.78f }, { "tone", 0.32f }, { "spread", 0.60f } } },
        { "PLATE", "Modern Plate",       { { "time", 0.38f }, { "feedback", 0.50f }, { "decay", 0.72f }, { "tone", 0.35f }, { "spread", 0.85f } } },

        // ============================== ROOMS ===============================
        { "ROOM", "Small Room",          { { "time", 0.18f }, { "feedback", 0.28f }, { "decay", 0.62f }, { "tone", 0.26f }, { "spread", 0.55f } } },
        { "ROOM", "Medium Room",         { { "time", 0.32f }, { "feedback", 0.42f }, { "decay", 0.60f }, { "tone", 0.30f }, { "spread", 0.65f } } },
        { "ROOM", "Large Room",          { { "time", 0.48f }, { "feedback", 0.55f }, { "decay", 0.58f }, { "tone", 0.34f }, { "spread", 0.75f } } },
        { "ROOM", "Vocal Booth",         { { "time", 0.12f }, { "feedback", 0.22f }, { "decay", 0.65f }, { "tone", 0.24f }, { "spread", 0.50f } } },
        { "ROOM", "Drum Room",           { { "time", 0.25f }, { "feedback", 0.30f }, { "decay", 0.55f }, { "tone", 0.32f }, { "depth", 0.50f }, { "spread", 0.68f } } },
        { "ROOM", "Live Room",           { { "time", 0.42f }, { "feedback", 0.48f }, { "decay", 0.78f }, { "tone", 0.36f }, { "spread", 0.85f } } },
        { "ROOM", "Dead Room",           { { "time", 0.20f }, { "feedback", 0.20f }, { "decay", 0.40f }, { "tone", 0.22f }, { "depth", 0.60f }, { "spread", 0.55f } } },
        { "ROOM", "Boxy Room",           { { "time", 0.28f }, { "feedback", 0.42f }, { "decay", 0.35f }, { "tone", 0.28f }, { "spread", 0.40f } } },

        // ============================ CHAMBERS ==============================
        { "CHAMBER", "Small Chamber",    { { "time", 0.32f }, { "feedback", 0.48f }, { "decay", 0.40f }, { "tone", 0.30f }, { "spread", 0.62f } } },
        { "CHAMBER", "Echo Chamber",     { { "time", 0.65f }, { "feedback", 0.75f }, { "decay", 0.55f }, { "tone", 0.34f }, { "spread", 0.75f } } },
        { "CHAMBER", "Bright Chamber",   { { "time", 0.48f }, { "feedback", 0.55f }, { "decay", 0.82f }, { "tone", 0.36f }, { "spread", 0.70f } } },
        { "CHAMBER", "Crystal Chamber",  { { "time", 0.52f }, { "feedback", 0.58f }, { "decay", 0.78f }, { "tone", 0.32f }, { "rate", 0.45f }, { "spread", 0.78f } } },
        { "CHAMBER", "Dark Chamber",     { { "time", 0.58f }, { "feedback", 0.65f }, { "decay", 0.28f }, { "tone", 0.30f }, { "spread", 0.68f }, { "mix", 0.22f } } },
        { "CHAMBER", "Gothic Chamber",   { { "time", 0.85f }, { "feedback", 0.82f }, { "decay", 0.30f }, { "tone", 0.32f }, { "spread", 0.80f }, { "mix", 0.18f } } },

        // ============================ AMBIENCES =============================
        { "AMBIENCE", "Subtle Air",      { { "time", 0.55f }, { "feedback", 0.60f }, { "decay", 0.72f }, { "tone", 0.15f }, { "spread", 0.85f } } },
        { "AMBIENCE", "Lush Pad Bed",    { { "time", 0.78f }, { "feedback", 0.75f }, { "decay", 0.65f }, { "tone", 0.28f }, { "spread", 0.95f } } },
        { "AMBIENCE", "Drone Bed",       { { "time", 0.90f }, { "feedback", 0.88f }, { "decay", 0.35f }, { "tone", 0.32f }, { "spread", 0.80f } } },
        { "AMBIENCE", "Whisper Trail",   { { "time", 0.45f }, { "feedback", 0.50f }, { "decay", 0.60f }, { "tone", 0.12f }, { "spread", 0.55f } } },
        { "AMBIENCE", "Halo",            { { "time", 0.62f }, { "feedback", 0.68f }, { "decay", 0.78f }, { "tone", 0.30f }, { "rate", 0.55f }, { "spread", 0.88f } } },
        { "AMBIENCE", "Distant Memory",  { { "time", 0.72f }, { "feedback", 0.72f }, { "decay", 0.30f }, { "tone", 0.18f }, { "depth", 0.30f }, { "spread", 0.70f }, { "mix", 0.20f } } },

        // ============================= SHIMMER ==============================
        { "SHIMMER", "Bright Shimmer",   { { "time", 0.55f }, { "feedback", 0.62f }, { "decay", 0.85f }, { "tone", 0.42f }, { "rate", 0.75f }, { "spread", 0.80f } } },
        { "SHIMMER", "Subtle Sparkle",   { { "time", 0.48f }, { "feedback", 0.50f }, { "decay", 0.70f }, { "tone", 0.32f }, { "rate", 0.30f }, { "spread", 0.75f } } },
        { "SHIMMER", "Octave Heaven",    { { "time", 0.80f }, { "feedback", 0.85f }, { "decay", 0.70f }, { "tone", 0.38f }, { "rate", 1.00f }, { "spread", 0.88f } } },
        { "SHIMMER", "Crystal Shimmer",  { { "time", 0.42f }, { "feedback", 0.55f }, { "decay", 0.80f }, { "tone", 0.30f }, { "rate", 0.65f }, { "spread", 0.72f } } },
        { "SHIMMER", "Pad Shimmer",      { { "time", 0.75f }, { "feedback", 0.78f }, { "decay", 0.62f }, { "tone", 0.32f }, { "rate", 0.50f }, { "spread", 0.95f } } },
        { "SHIMMER", "Glassy Air",       { { "time", 0.58f }, { "feedback", 0.62f }, { "decay", 0.82f }, { "tone", 0.20f }, { "rate", 0.60f }, { "depth", 0.25f }, { "spread", 0.82f } } },
        { "SHIMMER", "Cathedral Shimmer",{ { "time", 0.95f }, { "feedback", 0.88f }, { "decay", 0.50f }, { "tone", 0.40f }, { "rate", 0.85f }, { "spread", 0.92f } } },
        { "SHIMMER", "Drifting Shimmer", { { "time", 0.70f }, { "feedback", 0.72f }, { "decay", 0.55f }, { "tone", 0.26f }, { "rate", 0.40f }, { "spread", 1.00f } } },

        // ============================= SPECIAL ==============================
        { "SPECIAL", "Gated Verb",       { { "time", 0.32f }, { "feedback", 0.40f }, { "decay", 0.65f }, { "tone", 0.40f }, { "depth", 0.85f }, { "spread", 0.70f } } },
        { "SPECIAL", "Lo-Fi Verb",       { { "time", 0.45f }, { "feedback", 0.55f }, { "decay", 0.32f }, { "tone", 0.28f }, { "spread", 0.55f }, { "mix", 0.55f } } },
        { "SPECIAL", "Slow Riser",       { { "time", 0.82f }, { "feedback", 0.85f }, { "decay", 0.78f }, { "tone", 0.32f }, { "rate", 0.55f }, { "spread", 0.95f } } },
        { "SPECIAL", "Pumping Verb",     { { "time", 0.50f }, { "feedback", 0.55f }, { "decay", 0.55f }, { "tone", 0.36f }, { "depth", 0.90f }, { "spread", 0.75f } } },
        { "SPECIAL", "Wall Of Sound",    { { "time", 0.95f }, { "feedback", 0.90f }, { "decay", 0.65f }, { "tone", 0.55f }, { "rate", 0.55f }, { "spread", 1.00f }, { "mix", 0.20f } } },
        { "SPECIAL", "Ducked Plate",     { { "time", 0.30f }, { "feedback", 0.42f }, { "decay", 0.62f }, { "tone", 0.40f }, { "depth", 0.70f }, { "spread", 0.65f } } },
        { "SPECIAL", "Reverse Pad",      { { "time", 0.72f }, { "feedback", 0.78f }, { "decay", 0.70f }, { "tone", 0.32f }, { "rate", 0.25f }, { "spread", 0.90f } } },
        { "SPECIAL", "Bloom",            { { "time", 0.68f }, { "feedback", 0.72f }, { "decay", 0.78f }, { "tone", 0.34f }, { "rate", 0.35f }, { "spread", 0.85f } } },
    };
    return presets;
}
