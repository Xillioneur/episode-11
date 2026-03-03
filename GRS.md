# Game Requirements Specification (GRS): Lumen Fidei – The Joy of the Trinity

## 1. Technical Requirements
*   **Engine/Framework:** C++ with Raylib (3D).
*   **Target Platform:** macOS.
*   **Physics Engine:** Update the Kinetic Drift model to support external **Force Fields** (wind/currents).

## 2. Functional Requirements
*   **F14: Mansion Hazards:**
    *   **Wind Force:** Implement a periodic vector force applied to the Guardian's velocity in Mansion II.
    *   **Centripetal Pull:** Implement a distance-based force toward {0,0,0} in Mansion III.
*   **F15: Sacred Geometry Patterns:**
    *   Implement **Spiral** and **Parametric** math for projectile velocity calculation.
*   **F16: Saintly Echoes:**
    *   Implement a post-redemption state for Vices that provides a proximity-based Fervor buff.

## 3. Content Requirements
*   **Visual Assets:**
    *   Visual representation of wind (dust motes).
    *   Visual representation of whirlpools (ripples on the stained glass).
*   **AI:**
    *   New pattern-based projectile spawning functions.

## 4. Non-Functional Requirements
*   **Determinism:** Parallel projectile physics must remain consistent under hazard forces.
