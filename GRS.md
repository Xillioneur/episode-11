# Game Requirements Specification (GRS): Lumen Fidei – The Joy of the Trinity

## 1. Technical Requirements
*   **Engine/Framework:** C++ with Raylib (3D).
*   **Target Platform:** macOS.
*   **Biome System:** Implement a shader-like color interpolation system for the sky, floor, and projectiles based on the current `Vigil` number.

## 2. Functional Requirements
*   **F11: The Mansion System (Biomes):**
    *   Vigils 1-5: Theme "Humility" (Deep Blue/Gold).
    *   Vigils 6-10: Theme "Temptation" (Ocher/Red/White).
    *   Each theme must change `COL_VICE` and the floor's line colors.
*   **F12: Pax (Ability):**
    *   Replaces the old "Word" on `E`.
    *   Logic: Applies a `frozen` flag to all `Temptations` within range. Frozen doubts do not move or deal damage for its duration.
*   **F13: Blessing System (Relics):**
    *   Implement a persistent `Blessing` struct.
    *   Example: "Veil of Purity" (reduces hit stun duration).

## 3. Content Requirements
*   **Visual Assets:**
    *   Dynamic sky color interpolation.
    *   Radiant burst visual for the **Pax** ability.
*   **UI/UX:**
    *   Display the current **Mansion** name upon level transition.

## 4. Non-Functional Requirements
*   **Smooth Transitions:** Environmental color changes should happen over 2-3 seconds when entering a new Mansion.
