# Game Requirements Specification (GRS): Lumen Fidei – The Shield of Saints

## 1. Technical Requirements
*   **Engine/Framework:** C++ with Raylib (optimized for 3D).
*   **Target Platform:** macOS (Intel/Apple Silicon).
*   **Performance:** Maintain 60 FPS. Rendering must support high-quality transparency for "ethereal" effects.
*   **Audio Engine:** Support for dynamic music layering (fading between "Dissonance" and "Harmony" tracks based on gameplay state).

## 2. Functional Requirements
*   **F01: Non-Violent Interaction Logic:**
    *   Remove all "Damage" calculations. Replace with "Conversion" logic.
    *   Projectiles must have two states: "Temptation" (Damages Player) and "Truth" (Converts Enemy).
    *   Enemy death state must be replaced with a "Redemption" animation (fading to white/ascending).
*   **F02: The Shield Mechanic:**
    *   Implement a directional shield that rotates around the player.
    *   Parry logic: Precision timing reflects projectiles at the source. Holding the button creates a "Guard" that absorbs impacts but drains Stamina (Spirit).
*   **F03: The Word (Aura Mechanic):**
    *   Implement a short-range, cone-shaped "attack" that applies a "Stun/Pacify" status effect, not damage.
*   **F04: AI Behavior Changes:**
    *   AI should not "die" but "retreat" or "change phase" when converted.
    *   Bosses: Implement non-lethal defeat states (e.g., The Boss kneels in repentance).

## 3. Content Requirements
*   **Levels:**
    *   *The Garden of Gethsemane:* Tutorial area.
    *   *The Desert of Temptation:* Open area with swarming Vices.
    *   *The Crystal Cathedral:* Boss arena.
*   **Visual Assets:**
    *   Replace "Bullet" meshes with glowing orbs, thorns, or snakes.
    *   Replace "Player" mesh with a stylized Guardian/Saint figure.
    *   Replace "Bonfire" with a Candle-lit Altar or Tabernacle.
*   **UI/UX:**
    *   Health Bar -> "Grace Meter" (Gold/White).
    *   Stamina Bar -> "Spirit Meter" (Blue).
    *   Boss Bar -> "Redemption Meter" (starts full Red, empties to White).

## 4. Non-Functional Requirements
*   **Accessibility:** "Contemplative Mode" (Easy difficulty) that slows down projectiles for a more meditative experience.
*   **Tone Compliance:** Ensure all text, names, and visuals strictly adhere to the reverent, non-violent theme. No aggression, gore, or occult imagery.
