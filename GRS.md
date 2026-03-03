# Game Requirements Specification (GRS): Lumen Fidei – The Joy of the Trinity

## 1. Technical Requirements
*   **Spirit Point Logic:** Implement a mapping between `Merit` (Joy) and `Level`. 
*   **Formula:** `Level = Merit / 1000`. Each level grants 1 `SpiritPoint`.

## 2. Functional Requirements
*   **F20: Persistent Inspiration Shop:**
    *   Trigger: The menu appears whenever `guardian.spiritPoints > 0`.
    *   Selection: Press `1`, `2`, or `3` to spend a point. The menu stays until all points are spent.
*   **F21: Incremental Stat Boosts:**
    *   Upgrades must be additive and have no hard cap.
*   **F22: Visual Progression:**
    *   Guardian's halo scale and brightness increase with `Level`.

## 3. Content Requirements
*   **Shop UI:** Display the number of available Spirit Points clearly.
*   **Feedback:** Sound and visual "ding" upon leveling up.

## 4. Non-Functional Requirements
*   **Zero-Lag UI:** Ensure the persistent menu does not interfere with mouse-aiming or character responsiveness.
