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
const float PLAYER_SPEED = 9.0f;
const float DASH_SPEED = 24.0f;
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
};

struct LoveHeart {
    Vector3 pos;
    float life;
    float floatOffset;
};

struct LightMote {
    Vector3 pos;
    Vector3 vel;
    float life;
    float maxLife;
    Color color;
    float size;
};

struct Guardian {
    Vector3 pos {0,0,0};
    float facingAngle = 0.0f; 
    
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
    
    // Sign of the Cross
    bool isCrossing = false;
    float crossTimer = 0.0f;
    float crossDuration = 0.7f;
    
    // The Holy Spirit: Praise & Joy
    float fervor = 0.0f;
    float maxFervor = 1000.0f;
    
    // Mechanics
    bool isShielding = false;
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
        grace = maxGrace;
        spirit = maxSpirit;
        fervor = 0.0f;
        isShielding = false;
        isSwinging = false;
        isCrossing = false;
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
void SpawnTemptation(Vector3 pos, Vector3 vel, Color col, bool isTruth);
void SpawnMotes(Vector3 pos, Color col, int count, float speed);

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
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !guardian.isSwinging && guardian.spirit >= 10.0f) {
        guardian.isSwinging = true;
        guardian.swingTimer = guardian.swingDuration;
        guardian.spirit -= 10.0f;
        screenShake = 0.15f;
    }
    
    if (guardian.isSwinging) {
        guardian.swingTimer -= dt;
        if (guardian.swingTimer <= 0) guardian.isSwinging = false;
        // Spawn trail particles
        float progress = 1.0f - (guardian.swingTimer / guardian.swingDuration);
        float ang = guardian.facingAngle + (progress - 0.5f) * guardian.swingArc;
        Vector3 censerPos = Vector3Add(guardian.pos, {sinf(ang) * guardian.swingRange, 2.0f, cosf(ang) * guardian.swingRange});
        SpawnMotes(censerPos, GOLD, 2, 4.0f);
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
        // Divine protection during the sign
    }

    // Canticle of Joy (F)
    if (IsKeyPressed(KEY_F) && guardian.fervor >= guardian.maxFervor) {
        guardian.fervor = 0;
        screenShake = 1.0f;
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
    
    // Input: The Word (E)
    if (IsKeyPressed(KEY_E) && guardian.spirit >= WORD_COST && guardian.wordCooldown <= 0.0f) {
        guardian.spirit -= WORD_COST;
        guardian.wordCooldown = WORD_COOLDOWN;
        
        // Effect: Pushback and Stun
        SpawnMotes(guardian.pos, COL_GRACE, 30, 15.0f);
        screenShake = 0.3f;
        
        for (auto& v : vices) {
            float dist = Vector3Distance(v.pos, guardian.pos);
            if (dist < WORD_RADIUS) {
                Vector3 pushDir = Vector3Normalize(Vector3Subtract(v.pos, guardian.pos));
                v.pos = Vector3Add(v.pos, Vector3Scale(pushDir, 8.0f)); // Knockback
                v.stunned = true;
                v.stunTimer = 1.5f;
            }
        }
        
        // Clear nearby weak temptations
        for (auto& t : temptations) {
            if (Vector3Distance(t.pos, guardian.pos) < WORD_RADIUS && !t.isTruth) {
                t.life = 0; // Dissipate
                SpawnMotes(t.pos, WHITE, 5, 5.0f);
            }
        }
    }
    
    // Input: Movement (WASD)
    Vector3 input = {0,0,0};
    if (IsKeyDown(KEY_W)) input.z -= 1;
    if (IsKeyDown(KEY_S)) input.z += 1;
    if (IsKeyDown(KEY_A)) input.x -= 1;
    if (IsKeyDown(KEY_D)) input.x += 1;
    
    // Dash (Shift)
    if (IsKeyPressed(KEY_LEFT_SHIFT) && guardian.spirit >= DASH_COST && !guardian.isDashing && Vector3Length(input) > 0.1f) {
        guardian.isDashing = true;
        guardian.dashTimer = DASH_DURATION;
        guardian.dashDir = Vector3Normalize(input);
        guardian.spirit -= DASH_COST;
    }
    
    if (guardian.isDashing) {
        guardian.pos = Vector3Add(guardian.pos, Vector3Scale(guardian.dashDir, DASH_SPEED * dt));
        guardian.dashTimer -= dt;
        if (guardian.dashTimer <= 0) guardian.isDashing = false;
        SpawnMotes(guardian.pos, COL_SPIRIT, 1, 2.0f);
    } else if (Vector3Length(input) > 0.1f) {
        guardian.pos = Vector3Add(guardian.pos, Vector3Scale(Vector3Normalize(input), PLAYER_SPEED * dt));
    }
    
    // Clamp
    guardian.pos.x = Clamp(guardian.pos.x, -80, 80);
    guardian.pos.z = Clamp(guardian.pos.z, -80, 80);
}

void UpdateVices(float dt) {
    if (vices.empty()) return;

    size_t num = vices.size();
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    size_t chunk = (num + numThreads - 1) / numThreads;

    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * chunk;
        size_t endd = std::min(start + chunk, num);
        
        futures.push_back(g_pool->enqueue([start, endd, dt]() {
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
                                SpawnMotes(v.pos, COL_GRACE, 2, 5.0f);
                                if (v.corruption <= 0) {
                                    v.redeemed = true;
                                    guardian.merit.fetch_add(100);
                                    SpawnMotes(v.pos, GOLD, 50, 10.0f);
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
                            SpawnTemptation(v.pos, Vector3Scale(dir, 18.0f), COL_TEMPTATION, false);
                            v.attackTimer = 1.5f;
                        } else if (v.type == ACCUSER) {
                            // Shotgun blast
                             for(int k=-1; k<=1; k++) {
                                 Vector3 spread = Vector3RotateByAxisAngle(dir, {0,1,0}, k * 0.2f);
                                 SpawnTemptation(v.pos, Vector3Scale(spread, 14.0f), COL_TEMPTATION, false);
                             }
                             v.attackTimer = 3.0f;
                        } else if (v.type == RAGER) {
                            // Charge
                            v.pos = Vector3Add(v.pos, Vector3Scale(dir, 10.0f)); 
                            SpawnTemptation(v.pos, Vector3Scale(dir, 25.0f), MAROON, false);
                            v.attackTimer = 2.0f;
                        } else if (v.type == CARDINAL_SIN) {
                            // Phase 1: Cross Pattern
                            float angOffset = (float)GetTime() * 0.5f;
                            for (int k = 0; k < 4; k++) {
                                float ang = angOffset + k * PI/2;
                                Vector3 crossDir = {cosf(ang), 0, sinf(ang)};
                                SpawnTemptation(v.pos, Vector3Scale(crossDir, 16.0f), COL_TEMPTATION, false);
                            }
                            
                            // Phase 2: Rapid Doubts (Targeted)
                            if (fmodf(GetTime(), 4.0f) > 2.0f) {
                                Vector3 spread = Vector3RotateByAxisAngle(dir, {0,1,0}, (float)GetRandomValue(-20, 20) * 0.01f);
                                SpawnTemptation(v.pos, Vector3Scale(spread, 22.0f), MAROON, false);
                            }
                            
                            v.attackTimer = 0.5f;
                        }
                    }
                }
            }
        }));
    }
    for (auto& fut : futures) fut.wait();
    
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

    // 1. Move
    for (auto& t : temptations) {
        t.pos = Vector3Add(t.pos, Vector3Scale(t.vel, currentDt));
        t.life -= currentDt;
        t.trail.push_front(t.pos);
        if (t.trail.size() > 12) t.trail.pop_back();
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

void SpawnTemptation(Vector3 pos, Vector3 vel, Color col, bool isTruth) {
    Temptation t;
    t.pos = pos;
    t.pos.y = 2.0f;
    t.vel = vel;
    t.color = col;
    t.life = 5.0f;
    t.isTruth = isTruth;
    
    std::lock_guard<std::mutex> lock(sharedMutex);
    temptations.push_back(t);
}

void SpawnMotes(Vector3 pos, Color col, int count, float speed) {
    std::lock_guard<std::mutex> lock(sharedMutex);
    for(int i=0; i<count; i++) {
        LightMote m;
        m.pos = pos;
        Vector3 dir = { (float)GetRandomValue(-10,10), (float)GetRandomValue(-10,10), (float)GetRandomValue(-10,10) };
        m.vel = Vector3Scale(Vector3Normalize(dir), speed);
        m.color = col;
        m.life = 1.0f;
        m.maxLife = 1.0f;
        m.size = (float)GetRandomValue(2,5) / 10.0f;
        motes.push_back(m);
    }
}

void UpdateFrame(float dt) {
    timeSinceStart += dt;
    screenShake = std::max(0.0f, screenShake - dt);
    
    // Camera follow
    Vector3 targetPos = guardian.pos;
    // Add mouse peek
    Vector3 mouseOffset = Vector3Subtract(GetScreenToWorldRay(GetMousePosition(), camera).position, guardian.pos);
    // Rough approx since raycasting to ground plane is needed for perfect peek, just use simple lerp for now
    
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
    
    // Update Motes
    for(auto& m : motes) {
        m.pos = Vector3Add(m.pos, Vector3Scale(m.vel, dt));
        m.life -= dt;
    }
    motes.erase(std::remove_if(motes.begin(), motes.end(), [](const LightMote& m){ return m.life <= 0; }), motes.end());

    // Check Win/Loss
    if (guardian.grace <= 0) currentState = STATE_DESOLATION;
    if (vices.empty()) {
        currentState = STATE_ALTAR;
    }
}

void DrawFrame() {
    BeginDrawing();
    ClearBackground({12, 12, 22, 255}); 
    
    BeginMode3D(camera);
        // Stained Glass Radiant Floor
        DrawPlane({0,-0.1f,0}, {250, 250}, {18, 18, 28, 255});
        for (int i = 0; i < 12; i++) {
            float ang = (float)i / 12 * 2 * PI;
            Vector3 start = {cosf(ang) * 5.0f, 0, sinf(ang) * 5.0f};
            Vector3 end = {cosf(ang) * 120.0f, 0, sinf(ang) * 120.0f};
            Color col = (i % 3 == 0) ? Fade(COL_GRACE, 0.15f) : (i % 3 == 1) ? Fade(COL_SPIRIT, 0.1f) : Fade(PURPLE, 0.08f);
            DrawLine3D(start, end, col);
            for (int r = 1; r < 5; r++) {
                DrawCircle3D({0, 0, 0}, r * 25.0f, {0, 1, 0}, 90.0f, col);
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
            for (int i = 0; i < 20; i++) {
                float step = (float)i / 20.0f;
                float ang = guardian.facingAngle + (progress - 0.5f - step * 0.3f) * guardian.swingArc;
                Vector3 arcPos = Vector3Add(guardian.pos, {sinf(ang) * guardian.swingRange, 2.0f, cosf(ang) * guardian.swingRange});
                DrawSphere(arcPos, 0.5f * (1.0f - step), Fade(GOLD, 0.6f * (1.0f - step)));
                DrawLine3D(guardian.pos, arcPos, Fade(COL_GRACE, 0.15f * (1.0f - step)));
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
            Color vCol = (v.stunned) ? GRAY : (v.type == CARDINAL_SIN) ? COL_BOSS : COL_VICE;
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
            DrawSphere(t.pos, 0.6f, t.color);
            if (t.isTruth) DrawSphereWires(t.pos, 0.8f, 6, 6, GOLD);
            
            // Draw Trail
            if (t.trail.size() > 1) {
                for (size_t i = 0; i < t.trail.size() - 1; i++) {
                    float alpha = 0.6f * (1.0f - ((float)i / t.trail.size()));
                    DrawLine3D(t.trail[i], t.trail[i+1], Fade(t.color, alpha));
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
    
    // The Father (Grace)
    DrawRectangle(20, SCREEN_HEIGHT - 60, 300, 30, Fade(BLACK, 0.5f));
    DrawRectangle(20, SCREEN_HEIGHT - 60, 300 * (guardian.grace / guardian.maxGrace), 30, COL_GRACE);
    DrawText("LOVE", 30, SCREEN_HEIGHT - 55, 20, BLACK);
    
    // The Son (Mercy)
    DrawRectangle(20, SCREEN_HEIGHT - 90, 250, 20, Fade(BLACK, 0.5f));
    DrawRectangle(20, SCREEN_HEIGHT - 90, 250 * (guardian.spirit / guardian.maxSpirit), 20, COL_SPIRIT);
    DrawText("MERCY", 30, SCREEN_HEIGHT - 90, 18, BLACK);

    // The Holy Spirit (Praise)
    float fervorPct = guardian.fervor / guardian.maxFervor;
    DrawRectangle(20, SCREEN_HEIGHT - 120, 200, 15, Fade(BLACK, 0.5f));
    DrawRectangle(20, SCREEN_HEIGHT - 120, 200 * fervorPct, 15, PINK);
    DrawText("PRAISE", 30, SCREEN_HEIGHT - 120, 14, BLACK);
    if (fervorPct >= 1.0f) DrawText("PRESS F - CANTICLE OF JOY", 20, SCREEN_HEIGHT - 150, 20, GOLD);

    // Merit
    DrawText(TextFormat("JOY: %d", guardian.merit.load()), SCREEN_WIDTH - 250, 20, 30, GOLD);
    
    // Instructions
    DrawText("WASD Move • L-Click Swing • R-Click Shield • SPACE Cross • F Canticle", 20, 100, 20, Fade(WHITE, 0.5f));
    
    if (currentState == STATE_DESOLATION) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.8f));
        DrawText("DESOLATION", SCREEN_WIDTH/2 - MeasureText("DESOLATION", 60)/2, SCREEN_HEIGHT/2 - 50, 60, RED);
        DrawText("The light has faded...", SCREEN_WIDTH/2 - MeasureText("The light has faded...", 30)/2, SCREEN_HEIGHT/2 + 20, 30, GRAY);
        DrawText("Press R to Rekindle", SCREEN_WIDTH/2 - MeasureText("Press R to Rekindle", 20)/2, SCREEN_HEIGHT/2 + 60, 20, WHITE);
    } else if (currentState == STATE_ALTAR) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.85f));
        DrawText("ALTAR OF REFLECTION", SCREEN_WIDTH/2 - MeasureText("ALTAR OF REFLECTION", 60)/2, 100, 60, GOLD);
        DrawText(TextFormat("Merit: %d", guardian.merit.load()), SCREEN_WIDTH/2 - MeasureText("Merit: 9999", 40)/2, 180, 40, WHITE);
        
        DrawText("1. Prudence (Parry Window +0.02s) - 200 Merit", 300, 300, 30, (guardian.merit >= 200) ? GREEN : GRAY);
        DrawText("2. Temperance (Max Grace +20) - 200 Merit", 300, 350, 30, (guardian.merit >= 200) ? GREEN : GRAY);
        DrawText("3. Justice (Reflect Damage +15%) - 300 Merit", 300, 400, 30, (guardian.merit >= 300) ? GREEN : GRAY);
        
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
