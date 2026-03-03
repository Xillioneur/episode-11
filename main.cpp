// ======================================================================
// Lumen Fidei – The Shield of Saints
// A Spiritual Defense Arcade Game
// ======================================================================

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <vector>
#include <string>
#include <deque>
#include <cmath>
#include <algorithm>
#include <random>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <atomic>
#include <iostream>

// ======================================================================
// Constants & Configuration
// ======================================================================
const int SCREEN_WIDTH = 1440;
const int SCREEN_HEIGHT = 900;

// Gameplay Constants
const float PLAYER_ACCEL = 160.0f;
const float PLAYER_DRAG = 9.5f;
const float PLAYER_MAX_SPEED = 20.0f;
const float DASH_IMPULSE = 75.0f;
const float LUNGE_IMPULSE = 52.0f;

const float DASH_DURATION = 0.25f;
const float DASH_COST = 25.0f;

// Stats
const float BASE_MAX_GRACE = 100.0f;   // Health
const float BASE_MAX_SPIRIT = 100.0f;  // Stamina/Energy
const float SPIRIT_REGEN = 15.0f;
const float SPIRIT_DRAIN_SHIELD = 12.0f; // Cost per second to hold shield

// Mechanics
const float SHIELD_ARC = 110.0f * DEG2RAD; 
const float PARRY_WINDOW = 0.22f; 
const float WORD_RADIUS = 14.0f;  
const float WORD_COST = 35.0f;
const float WORD_COOLDOWN = 1.0f;

const float CLARITAS_COST = 500.0f; // Merit cost
const float CLARITAS_DURATION = 5.0f;
const float CLARITAS_SLOW_FACTOR = 0.25f;

// Camera
const float CAMERA_HEIGHT = 44.0f;
const float CAMERA_DISTANCE = 34.0f;

// Colors (Ethereal Palette)
const Color COL_GRACE = { 255, 245, 180, 255 }; 
const Color COL_SPIRIT = { 130, 220, 255, 255 }; 
const Color COL_VICE = { 35, 30, 45, 255 };     
const Color COL_TEMPTATION = { 220, 20, 60, 255 }; 
const Color COL_TRUTH = { 240, 255, 255, 255 };  
const Color COL_ALTAR = { 255, 215, 0, 255 };  
const Color COL_BOSS = { 120, 40, 180, 255 };  // Royal Purple for Pride

// ======================================================================
// Thread Pool (Unchanged utility)
// ======================================================================
class ThreadPool {
public:
    ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) worker.join();
    }

    template <class F>
    auto enqueue(F&& f) -> std::future<decltype(f())> {
        using return_type = decltype(f());
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
};

// ======================================================================
// Enums & Structs
// ======================================================================
enum GameState { STATE_TITLE, STATE_VIGIL, STATE_ALTAR, STATE_DESOLATION, STATE_ASCENSION };
enum ViceType { WHISPERER, ACCUSER, RAGER, CARDINAL_SIN };

struct Temptation {
    Vector3 pos;
    Vector3 vel;
    Color color;
    float life;
    bool isTruth = false; // "Truth" is a converted temptation (player projectile)
    bool reflected = false;
    std::deque<Vector3> trail;
    float frozenTimer = 0.0f;
};

struct LoveHeart {
    Vector3 pos;
    float life;
    float floatOffset;
};

enum MoteType { MOTE_SPARK, MOTE_DOVE, MOTE_HEART };

struct LightMote {
    Vector3 pos;
    Vector3 vel;
    float life;
    float maxLife;
    Color color;
    float size;
    MoteType type;
};
struct Guardian {
    Vector3 pos {0,0,0};
    Vector3 vel {0,0,0}; // Physics-based velocity
    float facingAngle = 0.0f; 
    
    // Ghost Trail (After-images)
    std::deque<Vector3> ghosts;
    std::deque<float> ghostAngles;
    
    // The Father: Grace & Infinite Love
    float grace = BASE_MAX_GRACE;
    float maxGrace = BASE_MAX_GRACE;
    
    // The Son: Truth & Mercy
    float spirit = BASE_MAX_SPIRIT;
    float maxSpirit = BASE_MAX_SPIRIT;
    bool isSwinging = false;
    float swingTimer = 0.0f;
    float swingDuration = 0.45f;
    float swingArc = 140.0f * DEG2RAD;
    float swingRange = 10.0f;
    
    int comboStep = 0; // 0: None, 1: Humility, 2: Mercy, 3: Charity
    float comboTimer = 0.0f; // Window to continue combo
    
    // Sign of the Cross
    bool isCrossing = false;
    float crossTimer = 0.0f;
    float crossDuration = 0.7f;
    
    // The Holy Spirit: Praise & Joy
    float fervor = 0.0f;
    float maxFervor = 1000.0f;
    
    // Mechanics
    bool isShielding = false;
    bool isPraying = false;
    float prayerTimer = 0.0f;
    float heartPulse = 0.0f;
    
    float shieldTimer = 0.0f; 
    float wordCooldown = 0.0f; 
    float claritasTimer = 0.0f; 
    
    // Movement
    bool isDashing = false;
    float dashTimer = 0.0f;
    Vector3 dashDir {0,0,0};
    float hitStun = 0.0f;
    
    // Progression
    std::atomic<int> merit {0}; 

    void reset() {
        pos = {0,0,0};
        vel = {0,0,0};
        ghosts.clear();
        ghostAngles.clear();
        grace = maxGrace;
        spirit = maxSpirit;
        fervor = 0.0f;
        isShielding = false;
        isSwinging = false;
        isCrossing = false;
        isPraying = false;
        swingTimer = 0.0f;
        crossTimer = 0.0f;
        isDashing = false;
        dashTimer = 0.0f;
        hitStun = 0.0f;
        merit.store(0);
        claritasTimer = 0.0f;
    }
};

struct Vice {
    ViceType type;
    Vector3 pos;
    float rotation;
    
    // Redemption Mechanic
    std::atomic<float> corruption; // Health equivalent
    float maxCorruption;
    
    float attackTimer;
    float moveSpeed;
    float scale;
    std::atomic<bool> redeemed {false};
    
    // AI State
    bool stunned = false;
    float stunTimer = 0.0f;

    Vice() = default;
    
    // Move constructor/assignment for vector resizing
    Vice(Vice&& other) noexcept : corruption(other.corruption.load()), redeemed(other.redeemed.load()) {
        type = other.type; pos = other.pos; rotation = other.rotation;
        maxCorruption = other.maxCorruption; attackTimer = other.attackTimer;
        moveSpeed = other.moveSpeed; scale = other.scale;
        stunned = other.stunned; stunTimer = other.stunTimer;
    }
    
    Vice& operator=(Vice&& other) noexcept {
        if (this != &other) {
            type = other.type; pos = other.pos; rotation = other.rotation;
            corruption.store(other.corruption.load());
            redeemed.store(other.redeemed.load());
            maxCorruption = other.maxCorruption; attackTimer = other.attackTimer;
            moveSpeed = other.moveSpeed; scale = other.scale;
            stunned = other.stunned; stunTimer = other.stunTimer;
        }
        return *this;
    }
};

// ======================================================================
// Globals
// ======================================================================
GameState currentState = STATE_TITLE;
int vigilCount = 1; // Wave number
int initialViceCount = 0; // For candle progress

// Biome Colors (Dynamic)
Color currentSkyTop = {12, 12, 22, 255};
Color currentFloorBase = {18, 18, 28, 255};
Color currentViceCol = {35, 30, 45, 255};
Color currentLineCol = {255, 245, 180, 50};

Guardian guardian;
std::vector<Vice> vices;
std::vector<Temptation> temptations;
std::vector<LightMote> motes;
std::vector<LoveHeart> loveHearts;
Camera3D camera = {0};
ThreadPool* g_pool = nullptr;
std::mutex sharedMutex;

// Visuals
float timeSinceStart = 0.0f;
float screenShake = 0.0f;
float hitStop = 0.0f;

// ======================================================================
// Forward Declarations
// ======================================================================
void InitLumenFidei();
void StartVigil(bool fullReset);
void UpdateFrame(float dt);
void DrawFrame();
void SpawnTemptation(Vector3 pos, Vector3 vel, Color col, bool isTruth, std::vector<Temptation>* localList = nullptr);
void SpawnMotes(Vector3 pos, Color col, int count, float speed, MoteType type = MOTE_SPARK, std::vector<LightMote>* localList = nullptr);

// ======================================================================
// Logic
// ======================================================================

void InitLumenFidei() {
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = {0,1,0};
    camera.position = {0, CAMERA_HEIGHT, CAMERA_DISTANCE};
    camera.target = {0, 0, 0};
    
    StartVigil(true);
}

void StartVigil(bool fullReset) {
    if (fullReset) {
        guardian.reset();
        vigilCount = 1;
    } else {
        guardian.grace = std::min(guardian.grace + 40.0f, guardian.maxGrace);
    }
    
    guardian.pos = {0,0,0};
    temptations.clear();
    vices.clear();
    motes.clear();
    
    // Boss Wave at 5
    if (vigilCount == 5) {
        Vice v;
        v.type = CARDINAL_SIN;
        v.maxCorruption = 2500.0f;
        v.corruption.store(v.maxCorruption);
        v.moveSpeed = 0.0f; // Static
        v.scale = 6.0f;
        v.attackTimer = 2.0f;
        v.pos = {0, 0, -40};
        vices.push_back(std::move(v));
    } else {
        int count = 6 + (vigilCount * 2);
        for (int i = 0; i < count; i++) {
            Vice v;
            v.type = (i % 6 == 0) ? RAGER : (i % 4 == 0) ? ACCUSER : WHISPERER;
            v.maxCorruption = (v.type == RAGER) ? 180 : (v.type == ACCUSER) ? 120 : 50;
            v.corruption.store(v.maxCorruption);
            v.moveSpeed = (v.type == WHISPERER) ? 6.0f : 3.5f;
            v.scale = (v.type == RAGER) ? 2.0f : (v.type == ACCUSER) ? 1.6f : 0.9f;
            v.attackTimer = GetRandomValue(10, 50) / 10.0f;
            
            float ang = GetRandomValue(0, 360) * DEG2RAD;
            float dist = GetRandomValue(45, 80);
        v.pos = {cosf(ang) * dist, 0, sinf(ang) * dist};
        vices.push_back(std::move(v));
        }
    }
    initialViceCount = (int)vices.size();
}

void UpdateGuardian(float dt) {
    guardian.hitStun = std::max(0.0f, guardian.hitStun - dt);
    guardian.wordCooldown = std::max(0.0f, guardian.wordCooldown - dt);
    guardian.claritasTimer = std::max(0.0f, guardian.claritasTimer - dt);
    
    // Claritas (Time-Slow)
    if (IsKeyPressed(KEY_Q) && guardian.merit >= CLARITAS_COST && guardian.claritasTimer <= 0.0f) {
        guardian.merit -= CLARITAS_COST;
        guardian.claritasTimer = CLARITAS_DURATION;
        SpawnMotes(guardian.pos, COL_SPIRIT, 50, 12.0f);
    }

    // Regeneration
    if (!guardian.isShielding && !guardian.isDashing) {
        guardian.spirit = std::min(guardian.spirit + SPIRIT_REGEN * dt, guardian.maxSpirit);
    }

    // Input: Aiming
    Vector2 mousePos = GetMousePosition();
    Ray ray = GetMouseRay(mousePos, camera);
    float t = -ray.position.y / ray.direction.y;
    Vector3 groundPos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    Vector3 lookDir = Vector3Subtract(groundPos, guardian.pos);
    guardian.facingAngle = atan2f(lookDir.x, lookDir.z);

    // Input: Censer of Mercy (Left Click)
    guardian.comboTimer -= dt;
    if (guardian.comboTimer <= 0 && !guardian.isSwinging) {
        guardian.comboStep = 0;
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !guardian.isSwinging && guardian.spirit >= 10.0f) {
        guardian.isSwinging = true;
        guardian.spirit -= 10.0f;
        
        // Advance Combo Step
        guardian.comboStep++;
        if (guardian.comboStep > 3) guardian.comboStep = 1;
        
        // Define Step Properties (Faster & Snappier)
        if (guardian.comboStep == 1) { // Humility
            guardian.swingDuration = 0.25f;
            guardian.swingArc = 140.0f * DEG2RAD;
            guardian.swingRange = 10.0f;
            screenShake = 0.08f;
        } else if (guardian.comboStep == 2) { // Mercy
            guardian.swingDuration = 0.22f;
            guardian.swingArc = 180.0f * DEG2RAD;
            guardian.swingRange = 12.0f;
            screenShake = 0.12f;
        } else if (guardian.comboStep == 3) { // Charity
            guardian.swingDuration = 0.35f;
            guardian.swingArc = 360.0f * DEG2RAD;
            guardian.swingRange = 15.0f;
            screenShake = 0.25f;
            SpawnMotes(guardian.pos, GOLD, 20, 10.0f);
        }
        
        // Divine Lunge: Physics Impulse
        Vector3 lungeDir = {sinf(guardian.facingAngle), 0, cosf(guardian.facingAngle)};
        float impulseMag = (guardian.comboStep == 3) ? LUNGE_IMPULSE * 1.5f : LUNGE_IMPULSE;
        guardian.vel = Vector3Add(guardian.vel, Vector3Scale(lungeDir, impulseMag));
        
        guardian.swingTimer = guardian.swingDuration;
    }
    
    if (guardian.isSwinging) {
        guardian.swingTimer -= dt;
        if (guardian.swingTimer <= 0) {
            guardian.isSwinging = false;
            guardian.comboTimer = 0.35f; 
        }
        // Spawn trail particles based on step
        float progress = 1.0f - (guardian.swingTimer / guardian.swingDuration);
        float ang = guardian.facingAngle + (progress - 0.5f) * guardian.swingArc;
        Vector3 censerPos = Vector3Add(guardian.pos, {sinf(ang) * guardian.swingRange, 2.0f, cosf(ang) * guardian.swingRange});
        Color trailCol = (guardian.comboStep == 3) ? GOLD : (guardian.comboStep == 2) ? WHITE : COL_GRACE;
        SpawnMotes(censerPos, trailCol, 3, 4.0f);
    }

    // Input: Sign of the Cross (Space)
    if (IsKeyPressed(KEY_SPACE) && !guardian.isCrossing && guardian.spirit >= 40.0f) {
        guardian.isCrossing = true;
        guardian.crossTimer = guardian.crossDuration;
        guardian.spirit -= 40.0f;
        SpawnMotes(guardian.pos, WHITE, 40, 10.0f);
    }

    if (guardian.isCrossing) {
        guardian.crossTimer -= dt;
        if (guardian.crossTimer <= 0) guardian.isCrossing = false;
    }

    // Canticle of Joy (F)
    if (IsKeyPressed(KEY_F) && guardian.fervor >= guardian.maxFervor) {
        guardian.fervor = 0;
        screenShake = 1.0f;
        SpawnMotes(guardian.pos, WHITE, 40, 15.0f, MOTE_DOVE); // Seven Gifts (Doves)
        SpawnMotes(guardian.pos, PINK, 100, 30.0f); // Pink for Love
        for (auto& v : vices) {
            v.corruption.store(v.corruption - 500.0f);
            if (v.corruption <= 0 && !v.redeemed) {
                v.redeemed = true;
                guardian.merit.fetch_add(100);
            }
        }
    }

    // Input: Shield (Right Click)
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && guardian.spirit > 0 && !guardian.isSwinging) {
        if (!guardian.isShielding) {
            guardian.shieldTimer = 0.0f;
        }
        guardian.isShielding = true;
        guardian.shieldTimer += dt;
        guardian.spirit -= SPIRIT_DRAIN_SHIELD * dt;
    } else {
        guardian.isShielding = false;
        guardian.shieldTimer = 0.0f;
    }
    
    // Input: Pax (E) - Freeze Doubts
    if (IsKeyPressed(KEY_E) && guardian.spirit >= WORD_COST && guardian.wordCooldown <= 0.0f) {
        guardian.spirit -= WORD_COST;
        guardian.wordCooldown = WORD_COOLDOWN;
        
        SpawnMotes(guardian.pos, COL_SPIRIT, 40, 15.0f);
        screenShake = 0.25f;
        
        // Freeze nearby temptations
        for (auto& t : temptations) {
            if (Vector3Distance(t.pos, guardian.pos) < WORD_RADIUS * 1.5f && !t.isTruth) {
                t.frozenTimer = 3.0f;
            }
        }
        
        // Pacify Vices
        for (auto& v : vices) {
            float dist = Vector3Distance(v.pos, guardian.pos);
            if (dist < WORD_RADIUS) {
                v.stunned = true;
                v.stunTimer = 2.0f;
            }
        }
    }
    
    // Input: Prayer of Contemplation (Hold R)
    if (IsKeyDown(KEY_R) && !guardian.isSwinging && !guardian.isDashing) {
        guardian.isPraying = true;
        guardian.vel = Vector3Lerp(guardian.vel, {0,0,0}, 15.0f * dt);
        guardian.grace = std::min(guardian.grace + 15.0f * dt, guardian.maxGrace);
        guardian.fervor = std::min(guardian.fervor + 50.0f * dt, guardian.maxFervor);
        
        if (fmodf(GetTime(), 0.1f) < dt) {
            SpawnMotes(Vector3Add(guardian.pos, {0, 10.0f, 0}), WHITE, 2, 5.0f);
            SpawnMotes(guardian.pos, COL_GRACE, 1, 2.0f);
        }
    } else {
        guardian.isPraying = false;
    }

    // Input: Movement (WASD) - PHYSICS ACCELERATION
    if (!guardian.isPraying) {
        Vector3 accel = {0,0,0};
        if (IsKeyDown(KEY_W)) accel.z -= 1;
        if (IsKeyDown(KEY_S)) accel.z += 1;
        if (IsKeyDown(KEY_A)) accel.x -= 1;
        if (IsKeyDown(KEY_D)) accel.x += 1;
        
        if (Vector3Length(accel) > 0.1f) {
            accel = Vector3Normalize(accel);
            guardian.vel = Vector3Add(guardian.vel, Vector3Scale(accel, PLAYER_ACCEL * dt));
        }
        
        // Dash (Shift) - PHYSICS IMPULSE
        if (IsKeyPressed(KEY_LEFT_SHIFT) && guardian.spirit >= DASH_COST && !guardian.isDashing && Vector3Length(accel) > 0.1f) {
            guardian.isDashing = true;
            guardian.dashTimer = DASH_DURATION;
            guardian.dashDir = accel;
            guardian.spirit -= DASH_COST;
            guardian.vel = Vector3Add(guardian.vel, Vector3Scale(accel, DASH_IMPULSE));
        }
    }
    
    if (guardian.isDashing) {
        guardian.dashTimer -= dt;
        if (guardian.dashTimer <= 0) guardian.isDashing = false;
        SpawnMotes(guardian.pos, COL_SPIRIT, 1, 2.0f);
    }
    
    // Apply Drag / Friction
    float dragFactor = 1.0f - (PLAYER_DRAG * dt);
    if (dragFactor < 0) dragFactor = 0;
    guardian.vel = Vector3Scale(guardian.vel, dragFactor);
    
    // Cap Speed
    float currentSpeed = Vector3Length(guardian.vel);
    if (currentSpeed > PLAYER_MAX_SPEED && !guardian.isDashing) {
        guardian.vel = Vector3Scale(Vector3Normalize(guardian.vel), PLAYER_MAX_SPEED);
    }
    
    // Update Position
    guardian.pos = Vector3Add(guardian.pos, Vector3Scale(guardian.vel, dt));
    
    // Ghost Trail Logic
    currentSpeed = Vector3Length(guardian.vel);
    if (currentSpeed > 4.0f || guardian.isSwinging || guardian.isDashing || guardian.isCrossing) {
        guardian.ghosts.push_front(guardian.pos);
        guardian.ghostAngles.push_front(guardian.facingAngle);
    }
    
    size_t maxGhosts = (currentSpeed > PLAYER_MAX_SPEED) ? 24 : 12;
    while (guardian.ghosts.size() > maxGhosts) {
        guardian.ghosts.pop_back();
        guardian.ghostAngles.pop_back();
    }
    if (currentSpeed < 2.0f && !guardian.ghosts.empty()) {
        guardian.ghosts.pop_back();
        guardian.ghostAngles.pop_back();
    }
    
    // Clamp
    guardian.pos.x = Clamp(guardian.pos.x, -120, 120);
    guardian.pos.z = Clamp(guardian.pos.z, -120, 120);
}

void UpdateVices(float dt) {
    if (vices.empty()) return;

    size_t num = vices.size();
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    size_t chunk = (num + numThreads - 1) / numThreads;

    // Result buffers to avoid mutex contention
    std::vector<std::vector<Temptation>> threadTemps(numThreads);
    std::vector<std::vector<LightMote>> threadMotes(numThreads);

    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * chunk;
        size_t endd = std::min(start + chunk, num);
        if (start >= endd) continue;

        futures.push_back(g_pool->enqueue([start, endd, dt, t, &threadTemps, &threadMotes]() {
            for (size_t i = start; i < endd; ++i) {
                Vice& v = vices[i];
                if (v.redeemed) {
                    v.pos.y += 5.0f * dt; // Ascend
                    continue;
                }
                
                if (v.stunned) {
                    v.stunTimer -= dt;
                    if (v.stunTimer <= 0) v.stunned = false;
                } else {
                    // Censer Swing Interaction
                    if (guardian.isSwinging) {
                        float dist = Vector3Distance(v.pos, guardian.pos);
                        if (dist < guardian.swingRange + 2.0f) {
                            Vector3 toVice = Vector3Subtract(v.pos, guardian.pos);
                            float angleToVice = atan2f(toVice.x, toVice.z);
                            float angleDiff = fabs(angleToVice - guardian.facingAngle);
                            while (angleDiff > PI) angleDiff -= 2*PI;
                            while (angleDiff < -PI) angleDiff += 2*PI;

                            if (fabs(angleDiff) < guardian.swingArc / 2.0f) {
                                v.corruption.store(v.corruption - 50.0f * dt * 10.0f); // Fast conversion on hit
                                SpawnMotes(v.pos, COL_GRACE, 2, 5.0f, MOTE_SPARK, &threadMotes[t]);
                                if (v.corruption <= 0) {
                                    v.redeemed = true;
                                    guardian.merit.fetch_add(100);
                                    SpawnMotes(v.pos, GOLD, 50, 10.0f, MOTE_SPARK, &threadMotes[t]);
                                }
                            }
                        }
                    }

                    // Chase behavior
                    Vector3 toPlayer = Vector3Subtract(guardian.pos, v.pos);
                    toPlayer.y = 0;
                    float dist = Vector3Length(toPlayer);
                    Vector3 dir = Vector3Normalize(toPlayer);
                    
                    float keepDist = (v.type == WHISPERER) ? 20.0f : 8.0f;
                    
                    if (dist > keepDist) {
                        v.pos = Vector3Add(v.pos, Vector3Scale(dir, v.moveSpeed * dt));
                    } else if (dist < keepDist - 2.0f) {
                        v.pos = Vector3Subtract(v.pos, Vector3Scale(dir, v.moveSpeed * 0.5f * dt));
                    }
                    
                    // Attack behavior
                    v.attackTimer -= dt;
                    if (v.attackTimer <= 0.0f) {
                        // Attack logic
                        if (v.type == WHISPERER) {
                            SpawnTemptation(v.pos, Vector3Scale(dir, 18.0f), COL_TEMPTATION, false, &threadTemps[t]);
                            v.attackTimer = 1.5f;
                        } else if (v.type == ACCUSER) {
                            // Shotgun blast
                             for(int k=-1; k<=1; k++) {
                                 Vector3 spread = Vector3RotateByAxisAngle(dir, {0,1,0}, k * 0.2f);
                                 SpawnTemptation(v.pos, Vector3Scale(spread, 14.0f), COL_TEMPTATION, false, &threadTemps[t]);
                             }
                             v.attackTimer = 3.0f;
                        } else if (v.type == RAGER) {
                            // Charge
                            v.pos = Vector3Add(v.pos, Vector3Scale(dir, 10.0f)); 
                            SpawnTemptation(v.pos, Vector3Scale(dir, 25.0f), MAROON, false, &threadTemps[t]);
                            v.attackTimer = 2.0f;
                        } else if (v.type == CARDINAL_SIN) {
                            // Phase 1: Cross Pattern
                            float angOffset = (float)GetTime() * 0.5f;
                            for (int k = 0; k < 4; k++) {
                                float ang = angOffset + k * PI/2;
                                Vector3 crossDir = {cosf(ang), 0, sinf(ang)};
                                SpawnTemptation(v.pos, Vector3Scale(crossDir, 16.0f), COL_TEMPTATION, false, &threadTemps[t]);
                            }
                            
                            // Phase 2: Rapid Doubts (Targeted)
                            if (fmodf(GetTime(), 4.0f) > 2.0f) {
                                Vector3 spread = Vector3RotateByAxisAngle(dir, {0,1,0}, (float)GetRandomValue(-20, 20) * 0.01f);
                                SpawnTemptation(v.pos, Vector3Scale(spread, 22.0f), MAROON, false, &threadTemps[t]);
                            }
                            
                            v.attackTimer = 0.5f;
                        }
                    }
                }
            }
        }));
    }
    for (auto& fut : futures) fut.wait();

    // Merge results back to main thread
    for (int t = 0; t < numThreads; t++) {
        if (!threadTemps[t].empty()) {
            temptations.insert(temptations.end(), threadTemps[t].begin(), threadTemps[t].end());
        }
        if (!threadMotes[t].empty()) {
            motes.insert(motes.end(), threadMotes[t].begin(), threadMotes[t].end());
        }
    }
    
    // Clean up redeemed souls
    for (auto it = vices.begin(); it != vices.end();) {
        if (it->redeemed && it->pos.y > 20.0f) {
            it = vices.erase(it);
        } else {
            ++it;
        }
    }
}

void UpdateTemptations(float dt) {
    float timeScale = (guardian.claritasTimer > 0) ? CLARITAS_SLOW_FACTOR : 1.0f;
    float currentDt = dt * timeScale;

    size_t numT = temptations.size();
    if (numT > 0) {
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;
        size_t chunk = (numT + numThreads - 1) / numThreads;
        std::vector<std::future<void>> futures;
        for (size_t t = 0; t < numThreads; ++t) {
            size_t start = t * chunk;
            size_t endd = std::min(start + chunk, numT);
            futures.push_back(g_pool->enqueue([start, endd, currentDt, dt]() {
                for (size_t i = start; i < endd; ++i) {
                    Temptation& temp = temptations[i];
                    if (temp.frozenTimer > 0) {
                        temp.frozenTimer -= dt;
                        continue; // Do not move
                    }
                    temp.pos = Vector3Add(temp.pos, Vector3Scale(temp.vel, currentDt));
                    temp.life -= currentDt;
                    temp.trail.push_front(temp.pos);
                    if (temp.trail.size() > 12) temp.trail.pop_back();
                }
            }));
        }
        for (auto& fut : futures) fut.wait();
    }
    
    // 2. Collision with Guardian (Censer Swing, Shield, or Body)
    for (auto& t : temptations) {
        if (t.isTruth) continue; 
        if (t.life <= 0) continue;
        
        float dist = Vector3Distance(t.pos, guardian.pos);
        
        // Censer Swing Check
        if (guardian.isSwinging && dist < guardian.swingRange + 2.0f) {
            Vector3 toBullet = Vector3Subtract(t.pos, guardian.pos);
            float angleToBullet = atan2f(toBullet.x, toBullet.z);
            float angleDiff = fabs(angleToBullet - guardian.facingAngle);
            while (angleDiff > PI) angleDiff -= 2*PI;
            while (angleDiff < -PI) angleDiff += 2*PI;

            if (fabs(angleDiff) < guardian.swingArc / 2.0f) {
                // PURIFY with Censer
                t.vel = Vector3Scale(Vector3Normalize(Vector3Negate(t.vel)), Vector3Length(t.vel) * 2.0f);
                t.isTruth = true;
                t.color = COL_TRUTH;
                t.life = 6.0f;
                SpawnMotes(t.pos, COL_GRACE, 12, 12.0f);
                guardian.merit.fetch_add(5); 
                guardian.fervor = std::min(guardian.fervor + 10.0f, guardian.maxFervor); // Build fervor
                continue;
            }
        }

        // Sign of the Cross Check
        if (guardian.isCrossing && dist < 12.0f) {
            // Check if bullet is within the vertical or horizontal beams of the cross
            Vector3 local = Vector3Subtract(t.pos, guardian.pos);
            if (fabs(local.x) < 2.0f || fabs(local.z) < 2.0f) {
                t.isTruth = true;
                t.color = COL_TRUTH;
                t.vel = Vector3Scale(Vector3Normalize(Vector3Negate(t.vel)), Vector3Length(t.vel) * 1.5f);
                t.life = 7.0f;
                SpawnMotes(t.pos, WHITE, 5, 8.0f);
                continue;
            }
        }

        if (dist < 3.0f) {
            // Check Shield
            Vector3 toBullet = Vector3Subtract(t.pos, guardian.pos);
            float angleToBullet = atan2f(toBullet.x, toBullet.z);
            float angleDiff = fabs(angleToBullet - guardian.facingAngle);
            while (angleDiff > PI) angleDiff -= 2*PI;
            while (angleDiff < -PI) angleDiff += 2*PI;
            
            bool blocked = guardian.isShielding && (fabs(angleDiff) < SHIELD_ARC / 2.0f);
            
            if (blocked) {
                // REFLECT / BLOCK
                bool perfect = guardian.shieldTimer < PARRY_WINDOW;
                
                t.vel = Vector3Scale(Vector3Normalize(Vector3Negate(t.vel)), Vector3Length(t.vel) * 1.5f);
                t.isTruth = true; // Convert to Truth
                t.color = COL_TRUTH;
                t.life = 5.0f;
                
                SpawnMotes(t.pos, COL_GRACE, 10, 10.0f);
                
                if (perfect) {
                    guardian.spirit = std::min(guardian.spirit + 15.0f, guardian.maxSpirit); // Restore spirit
                    screenShake = 0.2f;
                    // Play chime sound here
                } else {
                    guardian.spirit -= 10.0f; // Chip stamina on block
                    if (guardian.spirit < 0) guardian.isShielding = false; // Guard break
                }
            } else if (guardian.hitStun <= 0.0f && !guardian.isDashing) {
                // HIT
                guardian.grace -= 15.0f;
                guardian.hitStun = 1.0f;
                screenShake = 0.5f;
                hitStop = 0.1f;
                t.life = 0;
                SpawnMotes(guardian.pos, RED, 20, 15.0f);
            }
        }
    }
    
    // 3. Collision with Vices (Redemption)
    for (auto& t : temptations) {
        if (!t.isTruth) continue;
        if (t.life <= 0) continue;
        
        for (auto& v : vices) {
            if (v.redeemed) continue;
            if (Vector3Distance(t.pos, v.pos) < v.scale * 2.0f) {
                v.corruption.store(v.corruption - 35.0f); // Damage
                t.life = 0; // Destroy projectile
                SpawnMotes(t.pos, COL_SPIRIT, 15, 8.0f);
                
                if (v.corruption <= 0) {
                    v.redeemed = true;
                    guardian.merit.fetch_add(100);
                    SpawnMotes(v.pos, GOLD, 50, 10.0f);
                    
                    // Divine Love: 35% chance to drop a Heart
                    if (GetRandomValue(0, 100) < 35) {
                        LoveHeart h;
                        h.pos = v.pos; h.pos.y = 2.0f;
                        h.life = 10.0f;
                        h.floatOffset = (float)GetRandomValue(0, 100);
                        std::lock_guard<std::mutex> lock(sharedMutex);
                        loveHearts.push_back(h);
                    }
                }
            }
        }
    }
    
    // Cleanup
    temptations.erase(std::remove_if(temptations.begin(), temptations.end(), 
        [](const Temptation& t){ return t.life <= 0; }), temptations.end());
}

void SpawnTemptation(Vector3 pos, Vector3 vel, Color col, bool isTruth, std::vector<Temptation>* localList) {
    Temptation t;
    t.pos = pos;
    t.pos.y = 2.0f;
    t.vel = vel;
    t.color = col;
    t.life = 5.0f;
    t.isTruth = isTruth;
    
    if (localList) {
        localList->push_back(t);
    } else {
        std::lock_guard<std::mutex> lock(sharedMutex);
        temptations.push_back(t);
    }
}

void SpawnMotes(Vector3 pos, Color col, int count, float speed, MoteType type, std::vector<LightMote>* localList) {
    for(int i=0; i<count; i++) {
        LightMote m;
        m.pos = pos;
        Vector3 dir = { (float)GetRandomValue(-10,10), (float)GetRandomValue(-10,10), (float)GetRandomValue(-10,10) };
        m.vel = Vector3Scale(Vector3Normalize(dir), speed);
        m.color = col;
        m.life = (type == MOTE_DOVE) ? 3.0f : 1.0f;
        m.maxLife = m.life;
        m.size = (float)GetRandomValue(2,5) / 10.0f;
        if (type == MOTE_DOVE) m.size *= 2.5f;
        m.type = type;
        
        if (localList) {
            localList->push_back(m);
        } else {
            std::lock_guard<std::mutex> lock(sharedMutex);
            motes.push_back(m);
        }
    }
}

void UpdateFrame(float dt) {
    timeSinceStart += dt;
    screenShake = std::max(0.0f, screenShake - dt);
    
    // Biome Color Interpolation
    Color targetSky, targetFloor, targetVice, targetLine;
    if (vigilCount <= 5) { // Mansion I: Humility
        targetSky = {12, 12, 22, 255}; targetFloor = {18, 18, 28, 255}; targetVice = {35, 30, 45, 255}; targetLine = {255, 245, 180, 50};
    } else { // Mansion II: Temptation
        targetSky = {45, 15, 15, 255}; targetFloor = {35, 10, 10, 255}; targetVice = {60, 20, 20, 255}; targetLine = {255, 255, 255, 60};
    }
    currentSkyTop = ColorLerp(currentSkyTop, targetSky, 2.0f * dt);
    currentFloorBase = ColorLerp(currentFloorBase, targetFloor, 2.0f * dt);
    currentViceCol = ColorLerp(currentViceCol, targetVice, 2.0f * dt);
    currentLineCol = ColorLerp(currentLineCol, targetLine, 2.0f * dt);

    // Camera follow
    Vector3 targetPos = guardian.pos;
    
    // Dynamic FOV Punch
    float currentSpeed = Vector3Length(guardian.vel);
    float targetFOV = 55.0f + (currentSpeed / PLAYER_MAX_SPEED) * 12.0f;
    camera.fovy = Lerp(camera.fovy, targetFOV, 10.0f * dt);
    
    camera.target = Vector3Lerp(camera.target, targetPos, 5.0f * dt);
    if (screenShake > 0) {
        camera.position.x += GetRandomValue(-10,10) * screenShake * 0.1f;
        camera.position.z += GetRandomValue(-10,10) * screenShake * 0.1f;
    } else {
        camera.position = Vector3Add(camera.target, {0, CAMERA_HEIGHT, CAMERA_DISTANCE});
    }

    UpdateGuardian(dt);
    UpdateVices(dt);
    UpdateTemptations(dt);
    
    // Update Divine Love
    for (auto it = loveHearts.begin(); it != loveHearts.end();) {
        it->life -= dt;
        it->pos.y = 2.0f + 0.5f * sinf(GetTime() * 3.0f + it->floatOffset);
        
        float dist = Vector3Distance(it->pos, guardian.pos);
        if (dist < 4.0f) {
            guardian.grace = std::min(guardian.grace + 25.0f, guardian.maxGrace);
            SpawnMotes(it->pos, PINK, 20, 10.0f); // Pink for Love
            it = loveHearts.erase(it);
        } else if (it->life <= 0) {
            it = loveHearts.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update Motes (Parallel)
    size_t numMotes = motes.size();
    if (numMotes > 0) {
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;
        size_t chunk = (numMotes + numThreads - 1) / numThreads;
        std::vector<std::future<void>> futures;
        for (size_t t = 0; t < numThreads; ++t) {
            size_t start = t * chunk;
            size_t endd = std::min(start + chunk, numMotes);
            futures.push_back(g_pool->enqueue([start, endd, dt]() {
                for (size_t i = start; i < endd; ++i) {
                    LightMote& m = motes[i];
                    if (m.type == MOTE_DOVE) {
                        // Rising and swaying behavior for Doves
                        m.pos.y += 12.0f * dt;
                        m.pos.x += sinf(GetTime() * 5.0f + i) * 4.0f * dt;
                        m.pos.z += cosf(GetTime() * 5.0f + i) * 4.0f * dt;
                    } else {
                        m.pos = Vector3Add(m.pos, Vector3Scale(m.vel, dt));
                    }
                    m.life -= dt;
                }
            }));
        }
        for (auto& fut : futures) fut.wait();
    }
    
    // Sequential Cleanup
    motes.erase(std::remove_if(motes.begin(), motes.end(), [](const LightMote& m){ return m.life <= 0; }), motes.end());

    // Check Win/Loss
    if (guardian.grace <= 0) currentState = STATE_DESOLATION;
    if (vices.empty()) {
        currentState = STATE_ALTAR;
    }
}

void DrawFrame() {
    BeginDrawing();
    ClearBackground(currentSkyTop); 
    
    BeginMode3D(camera);
        // Stained Glass Radiant Floor
        DrawPlane({0,-0.1f,0}, {250, 250}, currentFloorBase);
        for (int i = 0; i < 12; i++) {
            float ang = (float)i / 12 * 2 * PI;
            Vector3 start = {cosf(ang) * 5.0f, 0, sinf(ang) * 5.0f};
            Vector3 end = {cosf(ang) * 120.0f, 0, sinf(ang) * 120.0f};
            DrawLine3D(start, end, currentLineCol);
            for (int r = 1; r < 5; r++) {
                DrawCircle3D({0, 0, 0}, r * 25.0f, {0, 1, 0}, 90.0f, currentLineCol);
            }
        }
        
        // Saintly Ghost Trail (Radiant Ribbon & Ethereal Echoes)
        if (!guardian.ghosts.empty()) {
            // 1. Draw the "Life Streak" (Ribbon connecting ghosts)
            for (size_t i = 0; i < guardian.ghosts.size() - 1; i++) {
                float alpha = 0.4f * (1.0f - (float)i / guardian.ghosts.size());
                Vector3 p1 = Vector3Add(guardian.ghosts[i], {0, 2.0f, 0});
                Vector3 p2 = Vector3Add(guardian.ghosts[i+1], {0, 2.0f, 0});
                DrawLine3D(p1, p2, Fade(COL_GRACE, alpha));
                DrawLine3D(Vector3Add(p1, {0, 1.5f, 0}), Vector3Add(p2, {0, 1.5f, 0}), Fade(WHITE, alpha * 0.5f));
            }

            // 2. Draw the Ethereal Echoes
            for (size_t i = 0; i < guardian.ghosts.size(); i += 2) { // Step by 2 for performance/clarity
                float t = (float)i / guardian.ghosts.size();
                float alpha = 0.3f * (1.0f - t);
                float scale = 1.0f - (t * 0.4f); // Shrink as it ages
                
                // Divine Gradient: White -> Gold -> Pink
                Color ghostCol = (t < 0.5f) ? ColorLerp(WHITE, COL_GRACE, t * 2.0f) : ColorLerp(COL_GRACE, PINK, (t - 0.5f) * 2.0f);
                ghostCol = Fade(ghostCol, alpha);
                
                rlPushMatrix();
                rlTranslatef(guardian.ghosts[i].x, 0, guardian.ghosts[i].z);
                rlRotatef(guardian.ghostAngles[i] * RAD2DEG, 0, 1, 0);
                rlScalef(scale, scale, scale);
                
                // Main body echo
                DrawCylinder({0,0,0}, 0.8f, 0.8f, 3.0f, 8, ghostCol);
                DrawSphere({0, 3.5f, 0}, 0.9f, ghostCol);
                
                // Glow shell
                DrawSphere({0, 3.5f, 0}, 1.4f, Fade(ghostCol, 0.1f)); 
                
                rlPopMatrix();
            }
        }

        // Guardian Radiance
        DrawCylinder(guardian.pos, 1.0f, 1.0f, 3.2f, 8, COL_GRACE);
        DrawSphere(Vector3Add(guardian.pos, {0,3.5f,0}), 1.1f, COL_GRACE);
        DrawSphere(Vector3Add(guardian.pos, {0,3.5f,0}), 1.8f, Fade(COL_GRACE, 0.15f)); // Halo
        
        if (guardian.isCrossing) {
            float progress = 1.0f - (guardian.crossTimer / guardian.crossDuration);
            Color crossCol = Fade(WHITE, (1.0f - progress) * 0.8f);
            // Vertical bar
            DrawCube(Vector3Add(guardian.pos, {0, 4.0f, 0}), 0.8f, 12.0f, 0.8f, crossCol);
            // Horizontal bar
            DrawCube(Vector3Add(guardian.pos, {0, 6.0f, 0}), 8.0f, 0.8f, 0.8f, crossCol);
            DrawSphere(guardian.pos, 10.0f, Fade(WHITE, 0.05f));
        }

        if (guardian.isSwinging) {
            float progress = 1.0f - (guardian.swingTimer / guardian.swingDuration);
            Color arcCol = (guardian.comboStep == 3) ? GOLD : (guardian.comboStep == 2) ? WHITE : COL_GRACE;
            float radiusScale = (guardian.comboStep == 3) ? 1.5f : (guardian.comboStep == 2) ? 1.2f : 1.0f;
            
            for (int i = 0; i < 24; i++) {
                float step = (float)i / 24.0f;
                float ang = guardian.facingAngle + (progress - 0.5f - step * 0.25f) * guardian.swingArc;
                Vector3 arcPos = Vector3Add(guardian.pos, {sinf(ang) * guardian.swingRange, 2.0f, cosf(ang) * guardian.swingRange});
                DrawSphere(arcPos, 0.6f * (1.0f - step) * radiusScale, Fade(arcCol, 0.6f * (1.0f - step)));
                DrawLine3D(guardian.pos, arcPos, Fade(arcCol, 0.2f * (1.0f - step)));
            }
        }
        
        if (guardian.isShielding) {
            rlPushMatrix();
            rlTranslatef(guardian.pos.x, 2.0f, guardian.pos.z);
            rlRotatef(-guardian.facingAngle * RAD2DEG - 90, 0, 1, 0);
            
            Color shieldCol = (guardian.shieldTimer < PARRY_WINDOW) ? WHITE : COL_SPIRIT;
            // Shield "Shell"
            DrawCylinderEx({0,0,0}, {0,0,0}, 4.5f, 4.5f, 16, Fade(shieldCol, 0.12f)); 
            
            Vector3 left = {cosf(-SHIELD_ARC/2), 0, sinf(-SHIELD_ARC/2)};
            Vector3 right = {cosf(SHIELD_ARC/2), 0, sinf(SHIELD_ARC/2)};
            DrawLine3D({0,0,0}, Vector3Scale(left, 5.0f), shieldCol);
            DrawLine3D({0,0,0}, Vector3Scale(right, 5.0f), shieldCol);
            DrawSphere({0,0,3.2f}, 0.7f, shieldCol);
            DrawSphere({0,0,3.2f}, 1.4f, Fade(shieldCol, 0.2f)); 
            
            rlPopMatrix();
        }
        
        // Divine Love Drops
        for (const auto& h : loveHearts) {
            float pulse = 0.3f * sinf(GetTime() * 5.0f);
            DrawSphere(h.pos, 0.8f + pulse, PINK);
            DrawSphere(h.pos, 1.2f + pulse, Fade(GOLD, 0.3f));
            // Heart-like wings/petals
            for (int i=0; i<2; i++) {
                float side = (i == 0) ? 1.0f : -1.0f;
                Vector3 p = {side * 1.5f, 0.8f, 0};
                DrawSphere(Vector3Add(h.pos, p), 0.6f, Fade(PINK, 0.6f));
            }
        }

        // Vices
        for (const auto& v : vices) {
            Color vCol = (v.stunned) ? GRAY : (v.type == CARDINAL_SIN) ? COL_BOSS : currentViceCol;
            if (v.redeemed) vCol = GOLD;
            
            if (v.type == CARDINAL_SIN) {
                // Boss Visual
                DrawSphere(v.pos, v.scale * 1.5f, vCol);
                DrawSphere(v.pos, v.scale * 2.2f, Fade(vCol, 0.15f)); 
                // Crown
                for (int i=0; i<8; i++) {
                    float ang = i * PI/4 + GetTime();
                    Vector3 spike = {cosf(ang) * v.scale, v.scale * 2.0f, sinf(ang) * v.scale};
                    DrawLine3D(v.pos, Vector3Add(v.pos, spike), GOLD);
                }
            } else {
                DrawSphere(v.pos, v.scale * 1.5f, vCol);
            }
            
            // Redemption Bar
            if (!v.redeemed) {
                Vector3 barPos = Vector3Add(v.pos, {0, v.scale * 2.8f, 0});
                float hpPct = v.corruption / v.maxCorruption;
                float width = v.scale * 3.0f;
                // Background
                DrawCube(barPos, width, 0.4f, 0.4f, Fade(BLACK, 0.5f));
                // Foreground (offset so it fills from left to right)
                Vector3 fgPos = barPos;
                fgPos.x -= (width / 2.0f) * (1.0f - hpPct);
                DrawCube(fgPos, width * hpPct, 0.5f, 0.5f, (v.type == CARDINAL_SIN) ? PURPLE : GOLD);
            }
        }
        
        // Temptations
        for (const auto& t : temptations) {
            Color tCol = (t.frozenTimer > 0) ? LIGHTGRAY : t.color;
            if (t.isTruth) {
                // Radiant Orb of Truth
                DrawSphere(t.pos, 0.8f, WHITE);
                DrawSphere(t.pos, 1.2f, Fade(GOLD, 0.3f));
                DrawSphereWires(t.pos, 1.0f, 6, 6, GOLD);
            } else {
                // Dissonant Thorn of Doubt
                rlPushMatrix();
                rlTranslatef(t.pos.x, t.pos.y, t.pos.z);
                rlRotatef(GetTime() * 200.0f, 1, 1, 1);
                DrawCube({0,0,0}, 0.4f, 1.8f, 0.4f, tCol);
                DrawCube({0,0,0}, 1.8f, 0.4f, 0.4f, tCol);
                rlPopMatrix();
            }
            
            // Draw Trail
            if (t.trail.size() > 1) {
                for (size_t i = 0; i < t.trail.size() - 1; i++) {
                    float alpha = (t.isTruth ? 0.8f : 0.4f) * (1.0f - ((float)i / t.trail.size()));
                    DrawLine3D(t.trail[i], t.trail[i+1], Fade(tCol, alpha));
                }
            }
        }
        
        // Motes
        for (const auto& m : motes) {
            DrawCube(m.pos, m.size, m.size, m.size, Fade(m.color, m.life));
        }
        
        // Cursor
        Vector3 aimPos = {0};
        Ray ray = GetMouseRay(GetMousePosition(), camera);
        float t = -ray.position.y / ray.direction.y;
        aimPos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
        DrawCircle3D(aimPos, 1.2f, {1, 0, 0}, 90.0f, Fade(WHITE, 0.5f));
        DrawCircle3D(aimPos, 0.6f, {1, 0, 0}, 90.0f, Fade(WHITE, 0.3f));

    EndMode3D();
    
    // UI
    DrawText("LUMEN FIDEI", 20, 20, 40, COL_GRACE);
    DrawText(TextFormat("VIGIL %d", vigilCount), 20, 60, 30, WHITE);
    
    // The Sacred Heart (Grace/Love)
    float pulseSpeed = 1.0f + (1.0f - (guardian.grace / guardian.maxGrace)) * 4.0f;
    float heartScale = 1.0f + 0.15f * sinf(GetTime() * pulseSpeed * PI * 2.0f);
    int hX = 80, hY = SCREEN_HEIGHT - 80;
    
    // Heart Glow
    DrawCircleGradient(hX, hY, 60 * heartScale, Fade(PINK, 0.3f), Fade(PINK, 0.0f));
    DrawCircleGradient(hX, hY, 40 * heartScale, Fade(GOLD, 0.4f), Fade(GOLD, 0.0f));
    
    // Heart Visual (Simple procedural)
    DrawCircle(hX - 15 * heartScale, hY - 10 * heartScale, 20 * heartScale, PINK);
    DrawCircle(hX + 15 * heartScale, hY - 10 * heartScale, 20 * heartScale, PINK);
    DrawTriangle({(float)hX - 35 * heartScale, (float)hY}, {(float)hX + 35 * heartScale, (float)hY}, {(float)hX, (float)hY + 40 * heartScale}, PINK);
    
    // Cross on Heart
    DrawRectangle(hX - 2, hY - 30 * heartScale, 4, 40 * heartScale, Fade(WHITE, 0.8f));
    DrawRectangle(hX - 15 * heartScale, hY - 15 * heartScale, 30 * heartScale, 4, Fade(WHITE, 0.8f));

    // Love Bar (Background for Heart)
    DrawRectangle(140, SCREEN_HEIGHT - 65, 200, 20, Fade(BLACK, 0.5f));
    DrawRectangle(140, SCREEN_HEIGHT - 65, 200 * (guardian.grace / guardian.maxGrace), 20, PINK);
    DrawText("LOVE", 150, SCREEN_HEIGHT - 63, 16, BLACK);
    
    // The Son (Mercy)
    DrawRectangle(140, SCREEN_HEIGHT - 90, 180, 15, Fade(BLACK, 0.5f));
    DrawRectangle(140, SCREEN_HEIGHT - 90, 180 * (guardian.spirit / guardian.maxSpirit), 15, COL_SPIRIT);
    DrawText("MERCY", 150, SCREEN_HEIGHT - 90, 12, BLACK);

    // The Holy Spirit (Praise)
    float fervorPct = guardian.fervor / guardian.maxFervor;
    DrawRectangle(140, SCREEN_HEIGHT - 110, 160, 10, Fade(BLACK, 0.5f));
    DrawRectangle(140, SCREEN_HEIGHT - 110, 160 * fervorPct, 10, GOLD);
    if (fervorPct >= 1.0f) DrawText("PRESS F - CANTICLE OF JOY", 140, SCREEN_HEIGHT - 140, 20, GOLD);

    // Merit
    DrawText(TextFormat("JOY: %d", guardian.merit.load()), SCREEN_WIDTH - 250, 20, 30, GOLD);
    
    // Liturgical Candle (Vigil Progress)
    int cX = SCREEN_WIDTH - 60, cY = 150, cW = 25, cH = 300;
    float progress = (initialViceCount > 0) ? (float)vices.size() / initialViceCount : 0.0f;
    float currentCandleH = cH * progress;
    
    // Wax
    DrawRectangle(cX, cY + (cH - currentCandleH), cW, currentCandleH, WHITE);
    DrawRectangleLines(cX, cY, cW, cH, Fade(GOLD, 0.5f));
    
    // Flame
    if (progress > 0) {
        float flamePulse = 0.2f * sinf(GetTime() * 10.0f);
        DrawCircle(cX + cW/2, cY + (cH - currentCandleH) - 10, 8 + flamePulse * 10, ORANGE);
        DrawCircle(cX + cW/2, cY + (cH - currentCandleH) - 10, 4 + flamePulse * 5, YELLOW);
    }
    DrawText("VIGIL", cX - 60, cY + cH + 10, 20, LIGHTGRAY);
    
    // Instructions
    DrawText("WASD Move • L-Click Swing • R-Click Shield • SPACE Cross • F Canticle • R PRAY", 20, 100, 20, Fade(WHITE, 0.5f));
    
    // Combo Counter
    if (guardian.comboStep > 0) {
        const char* stepName = (guardian.comboStep == 3) ? "CHARITY" : (guardian.comboStep == 2) ? "MERCY" : "HUMILITY";
        Color comboCol = (guardian.comboStep == 3) ? GOLD : (guardian.comboStep == 2) ? WHITE : COL_GRACE;
        DrawText(stepName, SCREEN_WIDTH/2 - MeasureText(stepName, 50)/2, SCREEN_HEIGHT - 180, 50, Fade(comboCol, 0.8f));
    }
    
    if (currentState == STATE_DESOLATION) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.8f));
        DrawText("DESOLATION", SCREEN_WIDTH/2 - MeasureText("DESOLATION", 60)/2, SCREEN_HEIGHT/2 - 50, 60, RED);
        DrawText("The light has faded...", SCREEN_WIDTH/2 - MeasureText("The light has faded...", 30)/2, SCREEN_HEIGHT/2 + 20, 30, GRAY);
        DrawText("Press R to Rekindle", SCREEN_WIDTH/2 - MeasureText("Press R to Rekindle", 20)/2, SCREEN_HEIGHT/2 + 60, 20, WHITE);
    } else if (currentState == STATE_ALTAR) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.85f));
        DrawText("THE FATHER'S HOUSE", SCREEN_WIDTH/2 - MeasureText("THE FATHER'S HOUSE", 60)/2, 100, 60, GOLD);
        DrawText("Welcome home, good and faithful servant.", SCREEN_WIDTH/2 - MeasureText("Welcome home, good and faithful servant.", 30)/2, 160, 30, LIGHTGRAY);
        DrawText(TextFormat("Joy: %d", guardian.merit.load()), SCREEN_WIDTH/2 - 60, 220, 40, PINK);
        
        DrawText("1. Prudence (Wider Mercy) - 200 Joy", 300, 320, 30, (guardian.merit >= 200) ? GREEN : GRAY);
        DrawText("2. Temperance (Deeper Love) - 200 Joy", 300, 370, 30, (guardian.merit >= 200) ? GREEN : GRAY);
        DrawText("3. Justice (Greater Truth) - 300 Joy", 300, 420, 30, (guardian.merit >= 300) ? GREEN : GRAY);
        
        DrawText("SPACE to Begin Next Vigil", SCREEN_WIDTH/2 - MeasureText("SPACE to Begin Next Vigil", 40)/2, SCREEN_HEIGHT - 100, 40, WHITE);
    }
    
    EndDrawing();
}

// ======================================================================
// Main
// ======================================================================
int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Lumen Fidei – The Shield of Saints");
    SetTargetFPS(60);
    // HideCursor(); // Use custom cursor
    
    InitLumenFidei();
    
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    g_pool = new ThreadPool(numThreads);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (hitStop > 0) {
            hitStop -= dt;
            dt = 0;
        }

        if (currentState == STATE_DESOLATION) {
            if (IsKeyPressed(KEY_R)) {
                StartVigil(true);
                currentState = STATE_VIGIL;
            }
        } else if (currentState == STATE_ALTAR) {
            if (IsKeyPressed(KEY_ONE) && guardian.merit >= 200) {
                guardian.merit -= 200;
                // Prudence logic would go here (parry window constant needs to become variable)
            }
            if (IsKeyPressed(KEY_TWO) && guardian.merit >= 200) {
                guardian.merit -= 200;
                guardian.maxGrace += 20;
                guardian.grace += 20;
            }
            if (IsKeyPressed(KEY_THREE) && guardian.merit >= 300) {
                guardian.merit -= 300;
                // Justice logic
            }
            if (IsKeyPressed(KEY_SPACE)) {
                vigilCount++;
                StartVigil(false);
                currentState = STATE_VIGIL;
            }
            UpdateFrame(0); // Update camera only? Or just pause. 0 dt pauses physics.
        } else {
            UpdateFrame(dt);
        }
        
        DrawFrame();
    }
    
    delete g_pool;
    CloseWindow();
    return 0;
}
