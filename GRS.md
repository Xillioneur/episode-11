# Game Requirements Specification (GRS): Lumen Fidei – The Joy of the Trinity

## 1. Technical Requirements
*   **Dynamic Variable Mapping:** Implement `meritMult`, `fervorRegenMult`, and `dashCostMult` in the `Guardian` struct.
*   **UI Layout:** Expand the side-panel to support up to 6 upgrade entries without overlapping other HUD elements.

## 2. Functional Requirements
*   **F23: The Six Gifts Shop:**
    *   Trigger: Spirit Points > 0.
    *   Keys: `1` through `6` for selection.
    *   Logic: Each selection must immediately update the relevant Guardian variable.
*   **F24: Multiplier Application:**
    *   `meritMult` must be applied to all Joy gains (Vice redemption).
    *   `fervorRegenMult` must be applied to Censer hits and Prayer.
    *   `dashCostMult` must reduce the spirit drain of the shift-dash.

## 3. Content Requirements
*   **Benefit UI:** Each entry must display its numerical bonus (e.g., "+40% Speed").
*   **Scaling Halo:** The `haloScale` must grow significantly when choosing high-tier upgrades.

## 4. Non-Functional Requirements
*   **Input Reliability:** Key presses for upgrades must not conflict with combat keys.
