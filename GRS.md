# Game Requirements Specification (GRS): Lumen Fidei – The Joy of the Trinity

## 1. Technical Requirements
*   **Engine/Framework:** C++ with Raylib (3D).
*   **Target Platform:** macOS (Intel/Apple Silicon).
*   **Performance:** 60 FPS minimum. Optimized trail rendering for combo steps.

## 2. Functional Requirements
*   **F06: The Censer of Mercy (Combo System):**
    *   **Combo Sequence:** Implement a 3-step state machine:
        *   **Step 1 (Humility):** Single sweep (140 deg). Range: 10.
        *   **Step 2 (Mercy):** Faster double swing. Range: 12.
        *   **Step 3 (Charity):** Full 360-degree radiant burst. Range: 15.
    *   **Combo Window:** Successive clicks within 0.3s of the previous swing's end will advance the combo.
    *   **Reset:** If the window is missed, the combo resets to Step 1.
*   **F07: The Sign of the Cross (Sanctuary):** 
    *   Maintains its protective and sanctifying properties.
*   **F08: The Father's Love (Hearts):** 
    *   Collection restores Grace and builds Praise Fervor.

## 3. Content Requirements
*   **Visual Assets:**
    *   **Unique Trails:** Different trail colors/radiance for each combo step (Gold, White, Radiant Pink).
*   **UI/UX:**
    *   Combo Counter: Displays the current step (Humility, Mercy, Charity).

## 4. Non-Functional Requirements
*   **Tone Compliance:** Every element must radiate joy and love.
*   **Feel:** High "Juice"—screen shake and light flares on the 3rd combo step.
