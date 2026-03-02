// ======================================================================
// Parry the Storm – Ashes of the Bullet (Dark Souls Edition)
// Episode 11: The Ultimate Trial – Bullet Hell with True Soulslike Punishment
// Collect souls, level up at bonfire, lose everything on death. Git Gud.
// ======================================================================
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <vector>
#include <string>
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

const int SCREEN_WIDTH = 1440;
const int SCREEN_HEIGHT = 810;

// Tuned for punishing difficulty
const float PLAYER_BASE_SPEED = 8.2f;
const float SPRINT_MULTIPLIER = 1.65f;
const float ROLL_SPEED = 22.0f;
const float ROLL_DURATION = 0.30f;
const float ROLL_RECOVERY = 0.35f;
const float ROLL_COST = 32.0f;
const float SHOOT_RATE_BASE = 0.14f;
const float PLAYER_BULLET_SPEED_BASE = 35.0f;
const float ENEMY_BULLET_SPEED = 20.0f;
const float PARRY_WINDOW_BASE = 0.22f;
const float PARRY_RANGE = 7.0f;
const float PARRY_COST = 35.0f;
const int BASE_MAX_HEALTH = 80;
const int BASE_MAX_STAMINA = 140;
const float STAMINA_REGEN_BASE = 28.0f;
const int MAX_FLASKS = 5;
const float FLASK_HEAL_BASE = 35.0f;
const float FLASK_TIME = 1.3f;
const float CAMERA_HEIGHT = 38.0f;
const float CAMERA_DISTANCE = 28.0f;
const float CAMERA_SMOOTH = 12.0f;

const float BULLET_LIFETIME = 5.5f;
const float BULLET_SIZE = 0.65f;
const float PERFECT_PARRY_BONUS = 2.8f;

const int UPGRADE_COST_BASE = 300;
const int UPGRADE_COST_MULTIPLIER = 180;

// ======================================================================
// Thread Pool
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
enum GameState { TITLE, PLAYING, BONFIRE, PAUSED, DEAD, VICTORY };
enum EnemyType { GRUNT, SPIRAL, WALL, RAPID, SHIELDED, BOSS };

struct Bullet {
    Vector3 pos;
    Vector3 vel;
    Color color;
    float life;
    bool playerBullet = false;
    bool reflected = false;
};

struct Particle {
    Vector3 pos;
    Vector3 vel;
    float life;
    float maxLife;
    Color color;
    float size;
};

struct SoulOrb {
    Vector3 pos;
    float timer;
};

struct Player {
    Vector3 pos {0,0,0};
    float rotation = 0.0f;
    int health = BASE_MAX_HEALTH;
    int maxHealth = BASE_MAX_HEALTH;
    float stamina = BASE_MAX_STAMINA;
    int maxStamina = BASE_MAX_STAMINA;
    int flasks = 0;
    float shootCD = 0.0f;
    float shootRate = SHOOT_RATE_BASE;
    float bulletSpeed = PLAYER_BULLET_SPEED_BASE;
    bool isRolling = false;
    float rollTimer = 0.0f;
    float recoveryTimer = 0.0f;
    Vector3 rollDir {0,0,0};
    bool isParrying = false;
    float parryTimer = 0.0f;
    float parryWindow = PARRY_WINDOW_BASE;
    float hitInvuln = 0.0f;
    float healTimer = 0.0f;
    bool isHealing = false;
    std::atomic<int> score {0};
    std::atomic<int> combo {0};
    std::atomic<int> souls {0};
    int vitality = 0;
    int endurance = 0;
    int strength = 0;
    int dexterity = 0;
    float shake = 0.0f;

    void reset() {
        pos = {0,0,0};
        rotation = 0.0f;
        health = BASE_MAX_HEALTH;
        maxHealth = BASE_MAX_HEALTH;
        stamina = BASE_MAX_STAMINA;
        maxStamina = BASE_MAX_STAMINA;
        flasks = 0;
        shootCD = 0.0f;
        shootRate = SHOOT_RATE_BASE;
        bulletSpeed = PLAYER_BULLET_SPEED_BASE;
        isRolling = false;
        rollTimer = 0.0f;
        recoveryTimer = 0.0f;
        rollDir = {0,0,0};
        isParrying = false;
        parryTimer = 0.0f;
        parryWindow = PARRY_WINDOW_BASE;
        hitInvuln = 0.0f;
        healTimer = 0.0f;
        isHealing = false;
        score.store(0);
        combo.store(0);
        souls.store(0);
        vitality = 0;
        endurance = 0;
        strength = 0;
        dexterity = 0;
        shake = 0.0f;
    }
};

struct Enemy {
    EnemyType type;
    Vector3 pos;
    float rotation;
    std::atomic<int> health;
    int maxHealth;
    float shootTimer;
    float patternAngle = 0.0f;
    float speed = 3.2f;
    float scale = 1.0f;
    std::atomic<bool> alive {true};
    Color color;
    int soulValue = 100;

    Enemy() = default;

    Enemy(Enemy&& other) noexcept
        : type(other.type),
          pos(other.pos),
          rotation(other.rotation),
          health(other.health.load()),
          maxHealth(other.maxHealth),
          shootTimer(other.shootTimer),
          patternAngle(other.patternAngle),
          speed(other.speed),
          scale(other.scale),
          alive(other.alive.load()),
          color(other.color),
          soulValue(other.soulValue) {}

    Enemy& operator=(Enemy&& other) noexcept {
        if (this != &other) {
            type = other.type;
            pos = other.pos;
            rotation = other.rotation;
            health.store(other.health.load());
            maxHealth = other.maxHealth;
            shootTimer = other.shootTimer;
            patternAngle = other.patternAngle;
            speed = other.speed;
            scale = other.scale;
            alive.store(other.alive.load());
            color = other.color;
            soulValue = other.soulValue;
        }
        return *this;
    }
};

GameState state = TITLE;
int wave = 1;
Player player;
std::vector<Enemy> enemies;
std::vector<Bullet> playerBullets;
std::vector<Bullet> enemyBullets;
std::vector<Particle> particles;
std::vector<SoulOrb> soulOrbs;
Camera3D camera = {0};
float hitStop = 0.0f;
std::atomic<int> totalEnemyBullets {0};
std::atomic<int> neutralized {0};
float accuracy = 0.0f;

Vector3 bonfirePos = {0, 0, 0};

std::mutex sharedMutex;
std::mutex playerHitMutex;

std::vector<std::string> deathQuotes = {
    "Bullet Issue", "Git Gud @ Dodging", "Parry Failed", "Souls Lost Forever",
    "Accuracy = 0%", "Try Shooting Them", "Flask Harder", "Roll Punished",
    "Combo Lost", "Bonfire Denied", "Humanity Drained", "You Died... Again"
};

// ======================================================================
// Functions
// ======================================================================
void InitGame();
void ResetWave(bool fullReset = false);
void UpdateGame(float dt);
void UpdatePlayer(float dt);
void UpdateEnemies(float dt);
void UpdateBullets(float dt);
void UpdateParticles(float dt);
void CollectSouls(float dt);
void UpdateCamera();
void SpawnBullet(Vector3 pos, Vector3 vel, Color col, bool playerOwned, bool reflected = false);
void SpawnParticles(Vector3 pos, Color col, int count, float speed, std::vector<Particle>& out_particles);
void SpawnParticles(Vector3 pos, Color col, int count, float speed);
void DropSouls(Vector3 pos, int amount, std::vector<SoulOrb>& out_soulOrbs);
void DropSouls(Vector3 pos, int amount);
void RestAtBonfire();
int GetUpgradeCost(int level);
void Draw3D();
void DrawPlayer();
void DrawEnemy(const Enemy& e);
void DrawCrosshairAndAimMarker();
void DrawHUD();
void DrawBonfireMenu();
void DrawTitle();
void DrawDeath();
void DrawVictory();
Vector3 GetAimPoint();

ThreadPool* g_pool = nullptr;

// ======================================================================
// Main
// ======================================================================
int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Parry the Storm – Ashes of the Bullet (Dark Souls Edition)");

    SetExitKey(KEY_NULL);

    SetTargetFPS(60);
    HideCursor();
    InitAudioDevice();
    InitGame();

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    g_pool = new ThreadPool(numThreads);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (hitStop > 0.0f) {
            hitStop -= dt;
            dt = 0.0f;
        }

        if (state == TITLE) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER)) {
                wave = 1;
                state = PLAYING;
                ResetWave();
            }
        } else if (state == PLAYING || state == PAUSED || state == BONFIRE) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                state = (state == PLAYING || state == BONFIRE) ? PAUSED : PLAYING;
            }
            if (state == PLAYING) {
                UpdateGame(dt);
            } else if (state == BONFIRE) {
                if (IsKeyPressed(KEY_ONE) && player.souls.load() >= GetUpgradeCost(player.vitality)) {
                    player.souls.fetch_sub(GetUpgradeCost(player.vitality++));
                    player.maxHealth += 12;
                    player.health = player.maxHealth;
                }
                if (IsKeyPressed(KEY_TWO) && player.souls.load() >= GetUpgradeCost(player.endurance)) {
                    player.souls.fetch_sub(GetUpgradeCost(player.endurance++));
                    player.maxStamina += 15;
                    player.stamina = player.maxStamina;
                }
                if (IsKeyPressed(KEY_THREE) && player.souls.load() >= GetUpgradeCost(player.strength)) {
                    player.souls.fetch_sub(GetUpgradeCost(player.strength++));
                    player.bulletSpeed += 5.0f;
                }
                if (IsKeyPressed(KEY_FOUR) && player.souls.load() >= GetUpgradeCost(player.dexterity)) {
                    player.souls.fetch_sub(GetUpgradeCost(player.dexterity++));
                    player.shootRate *= 0.92f;
                    player.parryWindow += 0.02f;
                }
                if (IsKeyPressed(KEY_SPACE)) {
                    ResetWave();
                    state = PLAYING;
                }
            }
        } else if (state == DEAD) {
            if (IsKeyPressed(KEY_R)) {
                wave = 1;
                ResetWave(true);
                state = PLAYING;
            }
        }

        BeginDrawing();
        ClearBackground({8, 8, 18, 255});

        BeginMode3D(camera);
        Draw3D();
        EndMode3D();

        DrawCrosshairAndAimMarker();
        DrawHUD();
        if (state == TITLE) DrawTitle();
        if (state == DEAD) DrawDeath();
        if (state == VICTORY) DrawVictory();
        if (state == BONFIRE) DrawBonfireMenu();
        if (state == PAUSED) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.7f));
            DrawText("PAUSED - GIT GUD", SCREEN_WIDTH/2 - MeasureText("PAUSED - GIT GUD", 80)/2, SCREEN_HEIGHT/2 - 40, 80, GOLD);
        }

        EndDrawing();
    }

    delete g_pool;

    CloseWindow();
    return 0;
}

void InitGame() {
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = {0,1,0};
    ResetWave(true);
}

void ResetWave(bool fullReset) {
    if (fullReset) {
        player.reset();
    } else {
        player.health = player.maxHealth;
        player.stamina = player.maxStamina;
    }
    player.flasks = 0;
    player.pos = {0, 0, 25.0f};

    enemies.clear();
    playerBullets.clear();
    enemyBullets.clear();
    particles.clear();
    soulOrbs.clear();
    totalEnemyBullets.store(0);
    neutralized.store(0);
    player.score.store(0);
    player.combo.store(0);

    auto spawnEnemy = [&](EnemyType t, int count, int hp, int souls) {
        for (int i = 0; i < count; i++) {
            enemies.emplace_back();
            Enemy& e = enemies.back();
            e.type = t;
            e.health.store(hp);
            e.maxHealth = hp;
            e.soulValue = souls;
            e.shootTimer = (float)i * 0.25f;
            float angle = (float)i / (float)count * 2 * PI + (float)GetRandomValue(-30,30) * DEG2RAD;
            float radius = 55.0f;
            e.pos = {cosf(angle) * radius, 0, sinf(angle) * radius};
            e.color = (t == BOSS) ? MAROON : (t == SHIELDED) ? DARKGRAY : (t == RAPID) ? ORANGE : (t == SPIRAL) ? PURPLE : RED;
            e.scale = (t == BOSS) ? 3.5f : (t == SHIELDED) ? 1.4f : 1.0f;
            e.alive.store(true);
        }
    };

    if (wave == 1) {
        spawnEnemy(GRUNT, 10, 70, 80);
    } else if (wave == 2) {
        spawnEnemy(GRUNT, 4, 90, 120);
        spawnEnemy(SPIRAL, 3, 60, 140);
        spawnEnemy(RAPID, 4, 55, 110);
    } else if (wave == 3) {
        spawnEnemy(WALL, 4, 100, 180);
        spawnEnemy(SHIELDED, 4, 140, 250);
        spawnEnemy(BOSS, 1, 3200, 5000);
    }
}

void RestAtBonfire() {
    player.flasks = MAX_FLASKS;
    player.health = player.maxHealth;
    player.stamina = player.maxStamina;
    player.pos = bonfirePos;
}

int GetUpgradeCost(int level) {
    return UPGRADE_COST_BASE + level * UPGRADE_COST_MULTIPLIER;
}

void DropSouls(Vector3 pos, int amount, std::vector<SoulOrb>& out_soulOrbs) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(-60, 60);
    int orbs = amount / 80;
    for (int i = 0; i < orbs; i++) {
        SoulOrb s;
        s.pos = Vector3Add(pos, {dist(rng)/10.0f, 3.0f, dist(rng)/10.0f});
        s.timer = 10.0f;
        out_soulOrbs.push_back(s);
    }
    player.souls.fetch_add(amount % 80);
}

void DropSouls(Vector3 pos, int amount) {
    std::vector<SoulOrb> local;
    DropSouls(pos, amount, local);
    if (!local.empty()) {
        std::lock_guard<std::mutex> lock(sharedMutex);
        soulOrbs.insert(soulOrbs.end(), local.begin(), local.end());
    }
}

void CollectSouls(float dt) {
    for (auto it = soulOrbs.begin(); it != soulOrbs.end(); ) {
        Vector3 toPlayer = Vector3Subtract(player.pos, it->pos);
        float dist = Vector3Length(toPlayer);
        if (dist < 6.0f || it->timer <= 0.0f) {
            player.souls.fetch_add(80);
            it = soulOrbs.erase(it);
        } else {
            it->pos = Vector3Add(it->pos, Vector3Scale(Vector3Normalize(toPlayer), 20.0f * dt));
            it->timer -= dt;
            ++it;
        }
    }
}

Vector3 GetAimPoint() {
    Ray ray = GetMouseRay(GetMousePosition(), camera);
    if (ray.direction.y != 0.0f) {
        float t = -ray.position.y / ray.direction.y;
        if (t > 0) return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    }
    return player.pos;
}

void UpdatePlayer(float dt) {
    player.hitInvuln = std::max(0.0f, player.hitInvuln - dt);
    player.shake = std::max(0.0f, player.shake - dt);
    player.shootCD = std::max(0.0f, player.shootCD - dt);

    if (player.isHealing) {
        player.healTimer -= dt;
        if (player.healTimer <= 0.0f) player.isHealing = false;
    }

    player.stamina = std::min(player.stamina + STAMINA_REGEN_BASE * dt, (float)player.maxStamina);

    Vector3 aimPoint = GetAimPoint();
    Vector3 toAim = Vector3Subtract(aimPoint, player.pos);
    toAim.y = 0;
    if (Vector3Length(toAim) > 0.1f) {
        player.rotation = atan2f(toAim.x, toAim.z);
    }

    Vector3 input {0,0,0};
    if (IsKeyDown(KEY_W)) input.z += 1;
    if (IsKeyDown(KEY_S)) input.z -= 1;
    if (IsKeyDown(KEY_D)) input.x += 1;
    if (IsKeyDown(KEY_A)) input.x -= 1;
    bool moving = Vector3Length(input) > 0.1f;

    Vector3 camDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    camDir.y = 0;
    camDir = Vector3Normalize(camDir);
    Vector3 camRight = Vector3CrossProduct(camDir, {0,1,0});

    Vector3 moveDir = Vector3Add(Vector3Scale(camDir, input.z), Vector3Scale(camRight, input.x));
    if (moving) moveDir = Vector3Normalize(moveDir);

    float speed = PLAYER_BASE_SPEED;
    if (IsKeyDown(KEY_LEFT_SHIFT) && moving && player.stamina > 10.0f) speed *= SPRINT_MULTIPLIER;

    if (player.recoveryTimer > 0.0f) {
        player.recoveryTimer -= dt;
        speed *= 0.4f;
    }

    static float shiftTimer = 0.0f;
    if (IsKeyDown(KEY_LEFT_SHIFT)) {
        shiftTimer += dt;
    } else {
        if (shiftTimer > 0.0f && shiftTimer < 0.22f && moving && player.stamina >= ROLL_COST && !player.isRolling && player.recoveryTimer <= 0.0f) {
            player.isRolling = true;
            player.rollTimer = ROLL_DURATION;
            player.rollDir = moveDir;
            player.stamina -= ROLL_COST;
            player.hitInvuln = ROLL_DURATION + 0.15f;
        }
        shiftTimer = 0.0f;
    }

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && player.shootCD <= 0.0f) {
        Vector3 shootDir = Vector3Normalize(toAim);
        Vector3 muzzle = Vector3Add(player.pos, Vector3Scale(shootDir, 2.0f));
        muzzle.y = 1.5f;
        SpawnBullet(muzzle, Vector3Scale(shootDir, player.bulletSpeed), SKYBLUE, true);
        player.shootCD = player.shootRate;
        SpawnParticles(muzzle, YELLOW, 6, 8.0f);
    }

    if (IsKeyPressed(KEY_SPACE) && player.stamina >= PARRY_COST && !player.isParrying) {
        player.isParrying = true;
        player.parryTimer = player.parryWindow;
        player.stamina -= PARRY_COST;
    }
    if (player.isParrying) {
        player.parryTimer -= dt;
        if (player.parryTimer <= 0.0f) player.isParrying = false;
    }

    if (IsKeyPressed(KEY_E) && player.flasks > 0 && !player.isHealing) {
        player.isHealing = true;
        player.healTimer = FLASK_TIME;
        player.flasks--;
    }
    if (player.isHealing && player.healTimer <= 0.5f) {
        player.health = std::min(player.health + (int)FLASK_HEAL_BASE, player.maxHealth);
    }

    if (player.isRolling) {
        player.rollTimer -= dt;
        player.pos = Vector3Add(player.pos, Vector3Scale(player.rollDir, ROLL_SPEED * dt));
        if (player.rollTimer <= 0.0f) {
            player.isRolling = false;
            player.recoveryTimer = ROLL_RECOVERY;
        }
    } else {
        player.pos = Vector3Add(player.pos, Vector3Scale(moveDir, speed * dt));
    }

    float limit = 80.0f;
    player.pos.x = Clamp(player.pos.x, -limit, limit);
    player.pos.z = Clamp(player.pos.z, -limit, limit);
}

void UpdateEnemies(float dt) {
    size_t num = enemies.size();
    if (num == 0) return;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    size_t chunk = (num + numThreads - 1) / numThreads;

    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * chunk;
        size_t endd = std::min(start + chunk, num);
        futures.push_back(g_pool->enqueue([start, endd, dt]() {
            std::vector<Bullet> localBullets;
            int localCount = 0;
            for (size_t k = start; k < endd; ++k) {
                Enemy& e = enemies[k];
                if (!e.alive.load()) continue;

                Vector3 toPlayer = Vector3Subtract(player.pos, e.pos);
                toPlayer.y = 0;
                float dist = Vector3Length(toPlayer);
                if (dist > 1.0f) {
                    e.rotation = atan2f(toPlayer.x, toPlayer.z);
                }

                if (e.type != BOSS) {
                    Vector3 dir = Vector3Normalize(toPlayer);
                    e.pos = Vector3Add(e.pos, Vector3Scale(dir, e.speed * dt));
                }

                e.shootTimer -= dt;
                if (e.shootTimer <= 0.0f && dist < 70.0f) {
                    Vector3 dir = Vector3Normalize(toPlayer);
                    if (Vector3Length(dir) < 0.1f) dir = {0,0,1};

                    switch (e.type) {
                        case GRUNT: {
                            Bullet b;
                            b.pos = Vector3Add(e.pos, {0,2,0});
                            b.pos.y = 2.0f;
                            b.vel = Vector3Scale(dir, ENEMY_BULLET_SPEED);
                            b.color = RED;
                            b.life = BULLET_LIFETIME;
                            b.playerBullet = false;
                            b.reflected = false;
                            localBullets.push_back(b);
                            localCount++;
                            e.shootTimer = 1.8f;
                            break;
                        }
                        case SPIRAL: {
                            for (int i = 0; i < 8; i++) {
                                float ang = e.patternAngle + i * PI/4;
                                Vector3 spd = {sinf(ang), 0, cosf(ang)};
                                Bullet b;
                                b.pos = Vector3Add(e.pos, {0,2,0});
                                b.pos.y = 2.0f;
                                b.vel = Vector3Scale(spd, ENEMY_BULLET_SPEED);
                                b.color = PURPLE;
                                b.life = BULLET_LIFETIME;
                                b.playerBullet = false;
                                b.reflected = false;
                                localBullets.push_back(b);
                                localCount++;
                            }
                            e.patternAngle += 0.4f;
                            e.shootTimer = 0.9f;
                            break;
                        }
                        case RAPID: {
                            Bullet b;
                            b.pos = Vector3Add(e.pos, {0,2,0});
                            b.pos.y = 2.0f;
                            b.vel = Vector3Scale(dir, ENEMY_BULLET_SPEED * 1.3f);
                            b.color = ORANGE;
                            b.life = BULLET_LIFETIME;
                            b.playerBullet = false;
                            b.reflected = false;
                            localBullets.push_back(b);
                            localCount++;
                            e.shootTimer = 0.25f;
                            break;
                        }
                        case WALL: {
                            for (int i = -4; i <= 4; i++) {
                                Vector3 side = Vector3CrossProduct(dir, {0,1,0});
                                Vector3 offset = Vector3Scale(side, i * 3.0f);
                                Bullet b;
                                b.pos = Vector3Add(Vector3Add(e.pos, offset), {0,0,0});
                                b.pos.y = 2.0f;
                                b.vel = Vector3Scale(dir, ENEMY_BULLET_SPEED);
                                b.color = MAROON;
                                b.life = BULLET_LIFETIME;
                                b.playerBullet = false;
                                b.reflected = false;
                                localBullets.push_back(b);
                                localCount++;
                            }
                            e.shootTimer = 2.2f;
                            break;
                        }
                        case SHIELDED: {
                            Bullet b;
                            b.pos = Vector3Add(e.pos, {0,2,0});
                            b.pos.y = 2.0f;
                            b.vel = Vector3Scale(dir, ENEMY_BULLET_SPEED * 0.9f);
                            b.color = DARKGRAY;
                            b.life = BULLET_LIFETIME;
                            b.playerBullet = false;
                            b.reflected = false;
                            localBullets.push_back(b);
                            localCount++;
                            e.shootTimer = 2.0f;
                            break;
                        }
                        case BOSS: {
                            int phase = (e.health.load() > 1600) ? 1 : (e.health.load() > 800) ? 2 : 3;
                            if (phase == 1) {
                                for (int i = 0; i < 12; i++) {
                                    float ang = e.patternAngle + i * PI/6;
                                    Vector3 spd = {sinf(ang), 0, cosf(ang)};
                                    Bullet b;
                                    b.pos = Vector3Add(e.pos, {0,4,0});
                                    b.pos.y = 2.0f;
                                    b.vel = Vector3Scale(spd, ENEMY_BULLET_SPEED);
                                    b.color = RED;
                                    b.life = BULLET_LIFETIME;
                                    b.playerBullet = false;
                                    b.reflected = false;
                                    localBullets.push_back(b);
                                    localCount++;
                                }
                                e.patternAngle += 0.3f;
                                e.shootTimer = 0.6f;
                            } else if (phase == 2) {
                                for (int i = 0; i < 5; i++) {
                                    Bullet b;
                                    b.pos = Vector3Add(e.pos, {0,4,0});
                                    b.pos.y = 2.0f;
                                    b.vel = Vector3Scale(dir, ENEMY_BULLET_SPEED * (1 + i*0.2f));
                                    b.color = MAROON;
                                    b.life = BULLET_LIFETIME;
                                    b.playerBullet = false;
                                    b.reflected = false;
                                    localBullets.push_back(b);
                                    localCount++;
                                }
                                e.shootTimer = 1.4f;
                            } else {
                                for (int i = 0; i < 20; i++) {
                                    float ang = (float)i / 20 * 2 * PI;
                                    Vector3 spd = {sinf(ang), 0, cosf(ang)};
                                    Bullet b;
                                    b.pos = Vector3Add(e.pos, {0,4,0});
                                    b.pos.y = 2.0f;
                                    b.vel = Vector3Scale(spd, ENEMY_BULLET_SPEED * 1.2f);
                                    b.color = VIOLET;
                                    b.life = BULLET_LIFETIME;
                                    b.playerBullet = false;
                                    b.reflected = false;
                                    localBullets.push_back(b);
                                    localCount++;
                                }
                                e.shootTimer = 0.8f;
                            }
                            break;
                        }
                    }
                }
            }
            if (!localBullets.empty()) {
                std::lock_guard<std::mutex> lock(sharedMutex);
                enemyBullets.insert(enemyBullets.end(), localBullets.begin(), localBullets.end());
                totalEnemyBullets.fetch_add(localCount);
            }
        }));
    }
    for (auto& fut : futures) fut.wait();
}

void SpawnBullet(Vector3 pos, Vector3 vel, Color col, bool playerOwned, bool reflected) {
    Bullet b;
    b.pos = pos;
    b.pos.y = 2.0f;
    b.vel = vel;
    b.color = col;
    b.life = BULLET_LIFETIME;
    b.playerBullet = playerOwned;
    b.reflected = reflected;
    std::lock_guard<std::mutex> lock(sharedMutex);
    if (playerOwned) {
        playerBullets.push_back(b);
    } else {
        enemyBullets.push_back(b);
        totalEnemyBullets.fetch_add(1);
    }
}

void SpawnParticles(Vector3 pos, Color col, int count, float speed, std::vector<Particle>& out_particles) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist100(-100, 100);
    std::uniform_int_distribution<int> dist30(30, 100);
    std::uniform_int_distribution<int> dist3080(30, 80);
    std::uniform_int_distribution<int> dist4(4, 12);
    for (int i = 0; i < count; i++) {
        Particle p;
        p.pos = pos;
        Vector3 dir = {dist100(rng)/100.0f, dist30(rng)/100.0f, dist100(rng)/100.0f};
        p.vel = Vector3Scale(Vector3Normalize(dir), speed);
        p.life = p.maxLife = dist3080(rng)/100.0f;
        p.color = col;
        p.size = dist4(rng)/10.0f;
        out_particles.push_back(p);
    }
}

void SpawnParticles(Vector3 pos, Color col, int count, float speed) {
    std::vector<Particle> local;
    SpawnParticles(pos, col, count, speed, local);
    if (!local.empty()) {
        std::lock_guard<std::mutex> lock(sharedMutex);
        particles.insert(particles.end(), local.begin(), local.end());
    }
}

void UpdateParticles(float dt) {
    size_t num = particles.size();
    if (num == 0) return;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    size_t chunk = (num + numThreads - 1) / numThreads;

    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * chunk;
        size_t endd = std::min(start + chunk, num);
        futures.push_back(g_pool->enqueue([start, endd, dt]() {
            for (size_t k = start; k < endd; ++k) {
                Particle& p = particles[k];
                p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
                p.vel.y -= 20.0f * dt;
                p.life -= dt;
            }
        }));
    }
    for (auto& fut : futures) fut.wait();

    // Sequential removal
    for (auto it = particles.begin(); it != particles.end(); ) {
        if (it->life <= 0.0f) it = particles.erase(it);
        else ++it;
    }
}

void UpdateCamera() {
    Vector3 desiredPos = Vector3Add(player.pos, {0, CAMERA_HEIGHT, CAMERA_DISTANCE});
    camera.position = Vector3Lerp(camera.position, desiredPos, CAMERA_SMOOTH * GetFrameTime());
    camera.target = Vector3Add(player.pos, {0, 3.0f, 0});

    if (player.shake > 0.0f) {
        Vector3 shakeOffset = {GetRandomValue(-100,100)/100.0f * player.shake * 10,
                               GetRandomValue(-100,100)/100.0f * player.shake * 10,
                               GetRandomValue(-100,100)/100.0f * player.shake * 10};
        camera.position = Vector3Add(camera.position, shakeOffset);
    }
}

void UpdateBullets(float dt) {
    // Update positions and life
    auto update_pos = [dt](std::vector<Bullet>& buls) {
        size_t num = buls.size();
        if (num == 0) return;
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;
        size_t chunk = (num + numThreads - 1) / numThreads;
        std::vector<std::future<void>> futures;
        for (size_t t = 0; t < numThreads; ++t) {
            size_t start = t * chunk;
            size_t endd = std::min(start + chunk, num);
            futures.push_back(g_pool->enqueue([start, endd, dt, &buls]() {
                for (size_t k = start; k < endd; ++k) {
                    Bullet& b = buls[k];
                    b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
                    b.life -= dt;
                }
            }));
        }
        for (auto& fut : futures) fut.wait();
    };
    update_pos(playerBullets);
    update_pos(enemyBullets);

    // Remove expired bullets
    std::set<size_t> toRemovePlayer, toRemoveEnemy;
    for (size_t i = 0; i < playerBullets.size(); ++i) {
        const Bullet& b = playerBullets[i];
        if (b.life <= 0.0f || Vector3Length(b.pos) > 120.0f) {
            toRemovePlayer.insert(i);
        }
    }
    for (size_t i = 0; i < enemyBullets.size(); ++i) {
        const Bullet& b = enemyBullets[i];
        if (b.life <= 0.0f || Vector3Length(b.pos) > 120.0f) {
            toRemoveEnemy.insert(i);
        }
    }

    // Player hit by enemy bullets (sequential for simplicity, as n is large but checks fast)
    for (size_t i = 0; i < enemyBullets.size(); ++i) {
        const Bullet& b = enemyBullets[i];
        if (Vector3Distance(b.pos, player.pos) < 3.0f && player.hitInvuln <= 0.0f) {
            player.health -= 12;
            player.hitInvuln = 0.6f;
            player.combo.store(0);
            player.shake = 0.4f;
            hitStop = 0.06f;
            SpawnParticles(b.pos, RED, 25, 14.0f);
            toRemoveEnemy.insert(i);
        }
    }

    // Parry enemy bullets
    struct ThreadData {
        std::set<size_t> local_toRemoveEnemy;
        std::vector<Bullet> local_reflected;
        std::vector<Particle> local_particles;
        int local_num_parries = 0;
    };
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    std::vector<ThreadData> threadDatas(numThreads);
    size_t eb_num = enemyBullets.size();
    size_t eb_chunk = (eb_num + numThreads - 1) / numThreads;
    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * eb_chunk;
        size_t endd = std::min(start + eb_chunk, eb_num);
        futures.push_back(g_pool->enqueue([start, endd, &threadDatas, t]() {
            ThreadData& data = threadDatas[t];
            for (size_t i = start; i < endd; ++i) {
                const Bullet& b = enemyBullets[i];
                if (player.isParrying && Vector3Distance(b.pos, player.pos) < PARRY_RANGE) {
                    Bullet newb = b;
                    newb.vel = Vector3Scale(Vector3Normalize(Vector3Negate(newb.vel)), Vector3Length(newb.vel) * PERFECT_PARRY_BONUS);
                    newb.playerBullet = true;
                    newb.reflected = true;
                    newb.color = GOLD;
                    data.local_reflected.push_back(newb);
                    data.local_num_parries++;
                    SpawnParticles(newb.pos, YELLOW, 35, 18.0f, data.local_particles);
                    data.local_toRemoveEnemy.insert(i);
                }
            }
        }));
    }
    for (auto& fut : futures) fut.wait();

    int total_num_parries = 0;
    for (auto& data : threadDatas) {
        total_num_parries += data.local_num_parries;
        toRemoveEnemy.insert(data.local_toRemoveEnemy.begin(), data.local_toRemoveEnemy.end());
        playerBullets.insert(playerBullets.end(), data.local_reflected.begin(), data.local_reflected.end());
        if (!data.local_particles.empty()) {
            std::lock_guard<std::mutex> lock(sharedMutex);
            particles.insert(particles.end(), data.local_particles.begin(), data.local_particles.end());
        }
    }
    if (total_num_parries > 0) {
        hitStop = 0.09f;
        player.shake = 0.5f;
    }
    for (int k = 0; k < total_num_parries; ++k) {
        neutralized.fetch_add(1);
        int old_c = player.combo.fetch_add(1);
        player.score.fetch_add(30 * (old_c + 1));
    }

    // Bullet on bullet collisions
    struct ThreadDataColl {
        std::set<size_t> local_toRemovePlayer;
        std::set<size_t> local_toRemoveEnemy;
        std::vector<Particle> local_particles;
        int local_num_coll = 0;
    };
    std::vector<ThreadDataColl> collDatas(numThreads);
    size_t pb_num = playerBullets.size();
    size_t pb_chunk = (pb_num + numThreads - 1) / numThreads;
    futures.clear();
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * pb_chunk;
        size_t endd = std::min(start + pb_chunk, pb_num);
        futures.push_back(g_pool->enqueue([start, endd, &collDatas, t]() {
            ThreadDataColl& data = collDatas[t];
            for (size_t i = start; i < endd; ++i) {
                const Vector3& ppos = playerBullets[i].pos;
                for (size_t j = 0; j < enemyBullets.size(); ++j) {
                    if (Vector3Distance(ppos, enemyBullets[j].pos) < BULLET_SIZE * 2) {
                        data.local_num_coll++;
                        SpawnParticles(ppos, WHITE, 15, 12.0f, data.local_particles);
                        data.local_toRemovePlayer.insert(i);
                        data.local_toRemoveEnemy.insert(j);
                    }
                }
            }
        }));
    }
    for (auto& fut : futures) fut.wait();

    int total_num_coll = 0;
    for (auto& data : collDatas) {
        total_num_coll += data.local_num_coll;
        toRemovePlayer.insert(data.local_toRemovePlayer.begin(), data.local_toRemovePlayer.end());
        toRemoveEnemy.insert(data.local_toRemoveEnemy.begin(), data.local_toRemoveEnemy.end());
        if (!data.local_particles.empty()) {
            std::lock_guard<std::mutex> lock(sharedMutex);
            particles.insert(particles.end(), data.local_particles.begin(), data.local_particles.end());
        }
    }
    for (int k = 0; k < total_num_coll; ++k) {
        neutralized.fetch_add(1);
        int old_c = player.combo.fetch_add(1);
        player.score.fetch_add(15 * (old_c + 1));
    }

    // Player bullets on enemies
    struct ThreadDataEnemy {
        std::set<size_t> local_toRemovePlayer;
        std::vector<Particle> local_particles;
        std::vector<SoulOrb> local_soulOrbs;
    };
    std::vector<ThreadDataEnemy> enemyDatas(numThreads);
    futures.clear();
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * pb_chunk;
        size_t endd = std::min(start + pb_chunk, pb_num);
        futures.push_back(g_pool->enqueue([start, endd, &enemyDatas, t]() {
            ThreadDataEnemy& data = enemyDatas[t];
            for (size_t i = start; i < endd; ++i) {
                const Vector3& ppos = playerBullets[i].pos;
                const bool reflected = playerBullets[i].reflected;
                for (size_t k = 0; k < enemies.size(); ++k) {
                    Enemy& e = enemies[k];
                    if (!e.alive.load()) continue;
                    Vector3 fromBulletToEnemy = Vector3Subtract(e.pos, ppos);
                    float dot = Vector3DotProduct(Vector3Normalize(fromBulletToEnemy), 
                                                  Vector3Normalize({sinf(e.rotation * DEG2RAD), 0, cosf(e.rotation * DEG2RAD)}));
                    bool blocked = (e.type == SHIELDED && dot > 0.35f);
                    if (Vector3Distance(ppos, e.pos) < e.scale * 4.0f) {
                        if (blocked) {
                            SpawnParticles(ppos, GRAY, 20, 10.0f, data.local_particles);
                        } else {
                            int dmg = reflected ? 35 : 18;
                            int new_h = e.health.fetch_sub(dmg);
                            if (new_h <= dmg && e.alive.exchange(false)) {
                                player.score.fetch_add(1000);
                                player.combo.fetch_add(10);
                                SpawnParticles(e.pos, RED, 60, 16.0f, data.local_particles);
                                DropSouls(e.pos, e.soulValue, data.local_soulOrbs);
                            }
                            SpawnParticles(ppos, reflected ? GOLD : SKYBLUE, 15, 10.0f, data.local_particles);
                            player.score.fetch_add(reflected ? 80 : 30);
                        }
                        data.local_toRemovePlayer.insert(i);
                    }
                }
            }
        }));
    }
    for (auto& fut : futures) fut.wait();

    for (auto& data : enemyDatas) {
        toRemovePlayer.insert(data.local_toRemovePlayer.begin(), data.local_toRemovePlayer.end());
        if (!data.local_particles.empty()) {
            std::lock_guard<std::mutex> lock(sharedMutex);
            particles.insert(particles.end(), data.local_particles.begin(), data.local_particles.end());
        }
        if (!data.local_soulOrbs.empty()) {
            std::lock_guard<std::mutex> lock(sharedMutex);
            soulOrbs.insert(soulOrbs.end(), data.local_soulOrbs.begin(), data.local_soulOrbs.end());
        }
    }

    // Remove marked bullets
    for (auto rit = toRemovePlayer.rbegin(); rit != toRemovePlayer.rend(); ++rit) {
        playerBullets.erase(playerBullets.begin() + *rit);
    }
    for (auto rit = toRemoveEnemy.rbegin(); rit != toRemoveEnemy.rend(); ++rit) {
        enemyBullets.erase(enemyBullets.begin() + *rit);
    }
}

void UpdateGame(float dt) {
    if (dt == 0.0f) return;

    UpdateCamera();
    UpdatePlayer(dt);
    UpdateEnemies(dt);
    UpdateBullets(dt);
    CollectSouls(dt);
    UpdateParticles(dt);

    bool allDead = true;
    for (const auto& e : enemies) if (e.alive.load()) allDead = false;
    if (allDead) {
        if (wave < 3) {
            wave++;
            state = BONFIRE;
            RestAtBonfire();
        } else {
            state = VICTORY;
        }
    }

    if (player.health <= 0) {
        state = DEAD;
    }
}

void Draw3D() {
    DrawPlane({0,0,0}, {200,200}, {20,25,40,255});

    Vector3 aimPoint = GetAimPoint();
    DrawCircle3D(aimPoint, 3.0f, {1,0,0}, 90.0f, Fade(LIME, 0.5f));
    DrawCircle3D(aimPoint, 1.5f, {1,0,0}, 90.0f, Fade(LIME, 0.8f));

    for (const auto& b : playerBullets) {
        DrawSphere(b.pos, BULLET_SIZE, b.color);
        if (b.reflected) DrawSphere(b.pos, BULLET_SIZE * 1.6f, Fade(GOLD, 0.4f));
    }
    for (const auto& b : enemyBullets) {
        DrawSphere(b.pos, BULLET_SIZE, b.color);
        if (b.reflected) DrawSphere(b.pos, BULLET_SIZE * 1.6f, Fade(GOLD, 0.4f));
    }

    for (const auto& p : particles) {
        DrawSphere(p.pos, p.size * (p.life / p.maxLife), Fade(p.color, p.life / p.maxLife));
    }

    for (const auto& s : soulOrbs) {
        DrawSphere(s.pos, 1.0f, Fade(GOLD, 0.7f + 0.3f * sinf(GetTime() * 8)));
    }

    // Bonfire
    DrawCylinder(bonfirePos, 2.2f, 1.8f, 9.0f, 16, DARKBROWN);
    for (int i = 0; i < 25; i++) {
        float ang = i / 25.0f * PI * 2;
        float h = 3.0f + sinf(GetTime() * 10 + i) * 2.0f;
        Vector3 flame = {cosf(ang) * 2.2f, h, sinf(ang) * 2.2f};
        DrawSphere(Vector3Add(bonfirePos, flame), 1.0f, Fade(ORANGE, 0.8f));
    }

    DrawPlayer();
    for (const auto& e : enemies) if (e.alive.load()) DrawEnemy(e);
}

void DrawPlayer() {
    rlPushMatrix();
    rlTranslatef(player.pos.x, player.pos.y, player.pos.z);
    rlRotatef(player.rotation * RAD2DEG, 0,1,0);

    Color body = player.isParrying ? GOLD : SKYBLUE;
    if (player.hitInvuln > 0.0f) body = Fade(body, 0.6f + 0.4f * sinf(GetTime() * 30));

    DrawCylinderEx({0,0,0}, {0,3,0}, 1.2f, 0.8f, 16, body);
    DrawSphere({0,3.5f,0}, 0.9f, body);
    DrawCylinderEx({-0.8f,1.5f,0}, {-1.6f,0.5f,0}, 0.4f, 0.3f, 12, DARKGRAY);
    DrawCylinderEx({0.8f,2.0f,0.6f}, {1.4f,0.8f,1.2f}, 0.35f, 0.25f, 12, GRAY);

    if (player.isParrying) {
        DrawSphere({0,1.5f,0}, 5.0f, Fade(GOLD, 0.4f + 0.4f * sinf(GetTime() * 20)));
    }

    rlPopMatrix();
}

void DrawEnemy(const Enemy& e) {
    rlPushMatrix();
    rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
    rlRotatef(e.rotation * RAD2DEG, 0,1,0);
    rlScalef(e.scale, e.scale, e.scale);
    DrawSphere({0,2,0}, 1.8f, e.color);
    DrawCylinderEx({0,2,0}, {0,5,0}, 0.8f, 0.4f, 12, Fade(e.color, 0.7f));

    // Shield visual for SHIELDED
    if (e.type == SHIELDED) {
        rlPushMatrix();
        rlTranslatef(-1.2f, 2.0f, 0);
        rlRotatef(90.0f, 0,1,0);
        DrawCube({0,0,0}, 2.5f, 4.0f, 0.5f, DARKGRAY);
        rlPopMatrix();
    }

    rlPopMatrix();
}

void DrawCrosshairAndAimMarker() {
    Vector2 mousePos = GetMousePosition();

    DrawLineEx({mousePos.x - 12, mousePos.y}, {mousePos.x + 12, mousePos.y}, 2.0f, WHITE);
    DrawLineEx({mousePos.x, mousePos.y - 12}, {mousePos.x, mousePos.y + 12}, 2.0f, WHITE);
    DrawCircleLines((int)mousePos.x, (int)mousePos.y, 18.0f, WHITE);
    DrawCircleLines((int)mousePos.x, (int)mousePos.y, 10.0f, WHITE);
}

void DrawHUD() {
    int y = 30;

    // Health
    DrawRectangle(30, y, 400, 40, Fade(BLACK, 0.7f));
    DrawRectangle(35, y+5, 390 * (float)player.health / player.maxHealth, 30, RED);
    DrawText("HEALTH", 40, y+8, 28, WHITE);
    y += 60;

    // Stamina
    DrawRectangle(30, y, 400, 30, Fade(BLACK, 0.7f));
    DrawRectangle(35, y+5, 390 * player.stamina / player.maxStamina, 20, LIME);
    y += 50;

    // Score & Combo
    DrawText(TextFormat("SCORE: %d", player.score.load()), 30, y, 40, GOLD);
    int comb = player.combo.load();
    if (comb > 1) DrawText(TextFormat("COMBO x%d", comb), 30, y+50, 50, ORANGE);
    y += 100;

    // Accuracy
    int teb = totalEnemyBullets.load();
    if (teb > 0) {
        accuracy = 100.0f * neutralized.load() / teb;
        Color accCol = accuracy > 80 ? LIME : accuracy > 50 ? YELLOW : RED;
        DrawText(TextFormat("ACCURACY: %.1f%%", accuracy), 30, y, 40, accCol);
    }
    y += 60;

    // Souls & Stats
    DrawText(TextFormat("Souls: %d", player.souls.load()), SCREEN_WIDTH - 320, 30, 50, YELLOW);
    DrawText(TextFormat("VIT %d | END %d | STR %d | DEX %d", 
                        player.vitality, player.endurance, player.strength, player.dexterity),
             SCREEN_WIDTH - 520, 90, 40, DARKGRAY);

    // Wave & Flasks
    DrawText(TextFormat("WAVE %d", wave), SCREEN_WIDTH - 300, 150, 50, GOLD);
    DrawText(TextFormat("FLASKS: %d", player.flasks), SCREEN_WIDTH - 300, 210, 40, ORANGE);

    // Boss health
    for (const auto& e : enemies) {
        if (e.alive.load() && e.type == BOSS) {
            float ratio = (float)e.health.load() / e.maxHealth;
            DrawRectangle(SCREEN_WIDTH/2 - 400, 40, 800, 50, Fade(BLACK, 0.8f));
            DrawRectangle(SCREEN_WIDTH/2 - 390, 50, 780 * ratio, 30, RED);
            DrawText("BULLET LORD", SCREEN_WIDTH/2 - MeasureText("BULLET LORD", 60)/2, 20, 60, GOLD);
        }
    }
}

void DrawBonfireMenu() {
    DrawRectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,Fade(BLACK,0.85f));
    DrawText("SITE OF GRACE - LEVEL UP", SCREEN_WIDTH/2 - MeasureText("SITE OF GRACE - LEVEL UP", 70)/2, 120, 70, GOLD);
    DrawText(TextFormat("Souls: %d", player.souls.load()), SCREEN_WIDTH/2 - 120, 220, 60, YELLOW);

    int y = 320;
    const char* stats[] = {"Vitality (+12 HP)", "Endurance (+15 Stamina)", "Strength (+5 Bullet Speed)", "Dexterity (Faster Fire/Parry)"};
    int levels[] = {player.vitality, player.endurance, player.strength, player.dexterity};
    for (int i = 0; i < 4; i++) {
        int cost = GetUpgradeCost(levels[i]);
        Color col = player.souls.load() >= cost ? LIME : RED;
        DrawText(TextFormat("%d - %s (Lv %d) - Cost %d", i+1, stats[i], levels[i], cost), 300, y, 45, col);
        y += 70;
    }

    DrawText("SPACE to Continue Into the Storm", SCREEN_WIDTH/2 - MeasureText("SPACE to Continue Into the Storm", 40)/2, SCREEN_HEIGHT - 140, 40, LIGHTGRAY);
}

void DrawTitle() {
    DrawRectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,Fade(BLACK,0.85f));
    DrawText("PARRY THE STORM", SCREEN_WIDTH/2 - MeasureText("PARRY THE STORM", 100)/2, 150, 100, GOLD);
    DrawText("Ashes of the Bullet - Dark Souls Edition", SCREEN_WIDTH/2 - MeasureText("Ashes of the Bullet - Dark Souls Edition", 50)/2, 270, 50, YELLOW);
    DrawText("WASD Move • Mouse Aim/Shoot • SPACE Parry • SHIFT Roll • E Flask", 200, 420, 36, LIGHTGRAY);
    DrawText("Die and lose everything. Git Gud eternally.", 200, 480, 36, ORANGE);
    DrawText("Click or ENTER to begin the trial", SCREEN_WIDTH/2 - MeasureText("Click or ENTER to begin the trial", 40)/2, SCREEN_HEIGHT - 120, 40, WHITE);
}

void DrawDeath() {
    DrawRectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,Fade(BLACK,0.9f));
    DrawText("YOU DIED", SCREEN_WIDTH/2 - MeasureText("YOU DIED", 140)/2, SCREEN_HEIGHT/2 - 100, 140, RED);
    const char* quote = deathQuotes[GetRandomValue(0, deathQuotes.size()-1)].c_str();
    DrawText(quote, SCREEN_WIDTH/2 - MeasureText(quote, 60)/2, SCREEN_HEIGHT/2 + 40, 60, ORANGE);
    DrawText("All souls and upgrades lost...", SCREEN_WIDTH/2 - MeasureText("All souls and upgrades lost...", 50)/2, SCREEN_HEIGHT/2 + 120, 50, DARKGRAY);
    int teb = totalEnemyBullets.load();
    if (teb > 0) {
        DrawText(TextFormat("Final Accuracy: %.1f%%", accuracy), SCREEN_WIDTH/2 - MeasureText("Final Accuracy: 100.0%", 50)/2, SCREEN_HEIGHT/2 + 180, 50, accuracy > 80 ? LIME : RED);
    }
    DrawText("R to Try Again From the Beginning", SCREEN_WIDTH/2 - MeasureText("R to Try Again From the Beginning", 40)/2, SCREEN_HEIGHT/2 + 260, 40, WHITE);
}

void DrawVictory() {
    DrawRectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,Fade(BLACK,0.8f));
    DrawText("VICTORY – THE STORM IS PARRIED", SCREEN_WIDTH/2 - MeasureText("VICTORY – THE STORM IS PARRIED", 80)/2, 150, 80, GOLD);
    DrawText(TextFormat("FINAL SCORE: %d", player.score.load()), SCREEN_WIDTH/2 - MeasureText("FINAL SCORE: 999999", 60)/2, 280, 60, YELLOW);
    DrawText(TextFormat("FINAL ACCURACY: %.1f%%", accuracy), SCREEN_WIDTH/2 - MeasureText("FINAL ACCURACY: 100.0%", 60)/2, 360, 60, accuracy >= 99.0f ? LIME : WHITE);
    if (accuracy >= 99.0f) DrawText("TRUE GIT GUD ACHIEVED", SCREEN_WIDTH/2 - MeasureText("TRUE GIT GUD ACHIEVED", 60)/2, 460, 60, GOLD);
    DrawText("You have conquered the ultimate trial.", SCREEN_WIDTH/2 - MeasureText("You have conquered the ultimate trial.", 40)/2, SCREEN_HEIGHT - 120, 40, LIGHTGRAY);
}