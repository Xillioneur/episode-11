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
const float PLAYER_ACCEL = 220.0f;
const float PLAYER_DRAG = 8.5f;
const float PLAYER_MAX_SPEED = 24.0f;
const float PLAYER_TRACTION = 6.0f;
const float DASH_IMPULSE = 80.0f;
const float LUNGE_IMPULSE = 55.0f;

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
// Shaders & Graphics
// ======================================================================
const char* vsCode = R"(
    #version 330
    in vec3 vertexPosition;
    in vec2 vertexTexCoord;
    in vec3 vertexNormal;
    in mat4 instanceTransform; // Hardware Instancing
    
    uniform mat4 mvp;
    uniform mat4 matModel;
    uniform mat4 matNormal;
    uniform float time;
    uniform float displacementStrength; // 0 for Guardian, >0 for Vices
    
    out vec3 fragPosition;
    out vec2 fragTexCoord;
    out vec3 fragNormal;
    
    // Simple 3D Noise
    float hash(vec3 p) {
        p  = fract( p*0.3183099+.1 );
        p *= 17.0;
        return fract( p.x*p.y*p.z*(p.x+p.y+p.z) );
    }

    float noise(vec3 x) {
        vec3 i = floor(x);
        vec3 f = fract(x);
        f = f*f*(3.0-2.0*f);
        return mix(mix(mix( hash(i+vec3(0,0,0)), hash(i+vec3(1,0,0)),f.x),
                       mix( hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)),f.x),f.y),
                   mix(mix( hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)),f.x),
                       mix( hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)),f.x),f.y),f.z);
    }
    
    void main() {
        vec3 pos = vertexPosition;
        vec3 normal = vertexNormal;
        
        // Procedural Displacement (Hyperrealistic Organic Form)
        if (displacementStrength > 0.0) {
            float n = noise(pos * 3.0 + vec3(0, time * 0.5, 0));
            pos += normal * n * displacementStrength;
        }
        
        // Support both instanced and non-instanced drawing
        // Raylib's default material uses matModel uniform for non-instanced
        // For instanced, we'd need to manually handle MVP.
        // Keeping it simple: We will rely on Raylib's internal handling for now but prepare the shader.
        
        fragPosition = vec3(matModel * vec4(pos, 1.0));
        fragTexCoord = vertexTexCoord;
        fragNormal = normalize(vec3(matNormal * vec4(normal, 1.0)));
        gl_Position = mvp * vec4(pos, 1.0);
    }
)";

const char* fsCode = R"(
    #version 330
    in vec3 fragPosition;
    in vec2 fragTexCoord;
    in vec3 fragNormal;
    uniform vec4 colDiffuse;
    uniform vec3 viewPos;
    uniform vec3 lightPos;
    uniform vec4 lightColor;
    uniform float rimPower;
    uniform float displacementStrength;
    uniform float time;
    out vec4 finalColor;
    
    void main() {
        vec3 normal = normalize(fragNormal);
        vec3 viewDir = normalize(viewPos - fragPosition);
        float NdotV = max(dot(normal, viewDir), 0.0);
        
        // 1. Divine Edges (Gold Wireframe)
        float edge = 1.0 - NdotV;
        edge = smoothstep(0.6, 0.95, edge); // Sharp gold rim
        vec3 edgeColor = vec3(1.0, 0.8, 0.2) * 2.0; // HDR Gold
        
        // 2. Blueprint Grid Interior
        // Triplanar mapping for grid
        vec3 coord = fragPosition * 2.0;
        float gridX = step(0.95, fract(coord.x + time * 0.1));
        float gridY = step(0.95, fract(coord.y + time * 0.15));
        float gridZ = step(0.95, fract(coord.z));
        float grid = max(max(gridX, gridY), gridZ);
        
        // 3. Material Base
        vec3 baseColor;
        if (displacementStrength > 0.0) {
            // Vices: Void Stone
            baseColor = vec3(0.05, 0.05, 0.05);
            edgeColor = vec3(0.8, 0.1, 0.1) * 2.0; // Red Edges for Vices
        } else {
            // Guardian: Lapis Lazuli
            baseColor = vec3(0.0, 0.1, 0.3);
        }
        
        // Lighting
        vec3 lightDir = normalize(lightPos - fragPosition);
        float diff = max(dot(normal, lightDir), 0.0);
        
        // Final Composition
        vec3 interior = mix(baseColor, edgeColor * 0.5, grid * 0.3);
        vec3 result = mix(interior * (0.5 + diff), edgeColor, edge);
        
        finalColor = vec4(result, colDiffuse.a);
    }
)";

const char* fsPost = R"(
    #version 330
    in vec2 fragTexCoord;
    in vec4 fragColor;
    uniform sampler2D texture0;
    uniform float aberrationStrength; // Dynamic juice
    out vec4 finalColor;
    
    void main() {
        // Chromatic Aberration
        vec2 dist = fragTexCoord - 0.5;
        vec2 offset = dist * aberrationStrength * 0.02;
        
        float r = texture(texture0, fragTexCoord - offset).r;
        float g = texture(texture0, fragTexCoord).g;
        float b = texture(texture0, fragTexCoord + offset).b;
        
        vec3 color = vec3(r, g, b);
        
        // Tone Mapping (Reinhard)
        color = color / (color + vec3(1.0));
        
        // Gamma Correction
        color = pow(color, vec3(1.0/2.2));
        
        // Vignette
        vec2 uv = fragTexCoord * (1.0 - fragTexCoord.yx);
        float vig = uv.x*uv.y * 15.0;
        vig = pow(vig, 0.2);
        color *= vig;
        
        finalColor = vec4(color, 1.0);
    }
)";

// Graphics Resources
Mesh sphereMesh;
Mesh cylinderMesh;
Mesh cubeMesh;
Mesh polyMesh; // Geometric shards
Mesh floorLinesMesh; // Baked floor geometry
Material divineMat;
Shader postShader;
RenderTexture2D target;

// ======================================================================
// Enums & Structs
// ======================================================================
enum GameState { STATE_TITLE, STATE_VIGIL, STATE_ALTAR, STATE_DESOLATION, STATE_PAUSE, STATE_VICTORY };
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

enum BlessingType { BLESS_CHARITY, BLESS_PURITY, BLESS_FORTITUDE, BLESS_NONE };

struct BlessingItem {
    Vector3 pos;
    BlessingType type;
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
    MoteType type;
};
struct Guardian {
    Vector3 pos {0,0,0};
    Vector3 vel {0,0,0}; // Physics-based velocity
    float facingAngle = 0.0f; 
    float heading = 0.0f; // Current movement heading
    float tiltX = 0.0f; // Visual lean forward/back
    float tiltZ = 0.0f; // Visual lean side-to-side
    
    // Dynamic Stats (Upgradable)
    float parryWindow = 0.22f;
    float mercyRadius = 14.0f;
    float truthSpeedMult = 1.0f;
    float meritMult = 1.0f;
    float fervorRegenMult = 1.0f;
    float dashCostMult = 1.0f;
    float haloScale = 1.0f;
    
    // Divine Leveling (Seamless Upgrades)
    int level = 1;
    int spiritPoints = 0;
    int nextLevelJoy = 1000;
    
    // Active Blessings (Permanent for run)
    bool hasCharity = false;   // Auto-collect hearts
    bool hasPurity = false;    // Reduce hitstun
    bool hasFortitude = false; // Increased acceleration
    
    // Ghost Trail (After-images)
    std::deque<Vector3> ghosts;
    std::deque<float> ghostAngles;
    std::deque<float> ghostTiltsX;
    std::deque<float> ghostTiltsZ;
    
    // Weapon Trail (Censer Ribbon)
    std::deque<Vector3> weaponTrail;
    
    // The Father: Grace & Infinite Love
    std::atomic<float> grace {BASE_MAX_GRACE};
    float maxGrace = BASE_MAX_GRACE;
    
    // The Son: Truth & Mercy
    std::atomic<float> spirit {BASE_MAX_SPIRIT};
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
    std::atomic<int> fervor {0};
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
        heading = 0.0f;
        tiltX = 0.0f;
        tiltZ = 0.0f;
        
        parryWindow = 0.22f;
        mercyRadius = 14.0f;
        truthSpeedMult = 1.0f;
        meritMult = 1.0f;
        fervorRegenMult = 1.0f;
        dashCostMult = 1.0f;
        haloScale = 1.0f;
        level = 1;
        spiritPoints = 0;
        nextLevelJoy = 1000;

        hasCharity = false;
        hasPurity = false;
        hasFortitude = false;
        ghosts.clear();
        ghostAngles.clear();
        ghostTiltsX.clear();
        ghostTiltsZ.clear();
        grace.store(BASE_MAX_GRACE);
        spirit.store(BASE_MAX_SPIRIT);
        fervor.store(0);
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

enum ViceState { VICE_IDLE, VICE_TELEGRAPH, VICE_ATTACK, VICE_RECOVER };

struct Vice {
    ViceType type;
    Vector3 pos;
    float rotation;
    
    // Redemption Mechanic
    std::atomic<float> corruption; 
    float maxCorruption;
    
    float attackTimer;
    float moveSpeed;
    float scale;
    float formationAngle; // Assigned slot in the circle around player
    std::atomic<bool> redeemed {false};
    
    // AI State Machine
    ViceState state = VICE_IDLE;
    float stateTimer = 0.0f;
    int patternIndex = 0;
    
    bool stunned = false;
    float stunTimer = 0.0f;

    Vice() = default;
    
    // Move constructor/assignment for vector resizing
    Vice(Vice&& other) noexcept : corruption(other.corruption.load()), redeemed(other.redeemed.load()) {
        type = other.type; pos = other.pos; rotation = other.rotation;
        maxCorruption = other.maxCorruption; attackTimer = other.attackTimer;
        moveSpeed = other.moveSpeed; scale = other.scale;
        state = other.state; stateTimer = other.stateTimer;
        patternIndex = other.patternIndex;
        stunned = other.stunned; stunTimer = other.stunTimer;
    }
    
    Vice& operator=(Vice&& other) noexcept {
        if (this != &other) {
            type = other.type; pos = other.pos; rotation = other.rotation;
            corruption.store(other.corruption.load());
            redeemed.store(other.redeemed.load());
            maxCorruption = other.maxCorruption; attackTimer = other.attackTimer;
            moveSpeed = other.moveSpeed; scale = other.scale;
            state = other.state; stateTimer = other.stateTimer;
            patternIndex = other.patternIndex;
            stunned = other.stunned; stunTimer = other.stunTimer;
        }
        return *this;
    }
};

struct SaintlyEcho {
    Vector3 pos;
    float life;
    float scale;
};

// AI Director
struct AIDirector {
    std::atomic<int> attackTokens {3}; // Max simultaneous attacks
    float tokenRegenTimer = 0.0f;
    Vector3 predictedPlayerPos;
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

bool debugMode = false;
bool godMode = false;

Guardian guardian;
AIDirector director;
std::vector<Vice> vices;
std::vector<Temptation> temptations;
std::vector<LightMote> motes;
std::vector<LoveHeart> loveHearts;
std::vector<BlessingItem> blessingItems;
std::vector<SaintlyEcho> saintlyEchoes;
Camera3D camera = {0};

float mansionTitleTimer = 0.0f;
std::string currentMansionName = "";
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

// Baked Geometry Generation
Mesh GenRadiantFloorMesh() {
    Mesh mesh = { 0 };
    int segments = 12;
    int circles = 5;
    int vertexCount = (segments * 2) + (circles * 64 * 2); // lines + circle edges
    
    mesh.vertices = (float *)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.colors = (unsigned char *)MemAlloc(vertexCount * 4 * sizeof(unsigned char));
    mesh.vertexCount = vertexCount;

    int vIdx = 0;
    int cIdx = 0;

    // Radiant Lines
    for (int i = 0; i < segments; i++) {
        float ang = (float)i / segments * 2.0f * PI;
        Vector3 start = { cosf(ang) * 5.0f, 0, sinf(ang) * 5.0f };
        Vector3 end = { cosf(ang) * 120.0f, 0, sinf(ang) * 120.0f };
        
        mesh.vertices[vIdx++] = start.x; mesh.vertices[vIdx++] = start.y; mesh.vertices[vIdx++] = start.z;
        mesh.vertices[vIdx++] = end.x; mesh.vertices[vIdx++] = end.y; mesh.vertices[vIdx++] = end.z;
        
        for (int k = 0; k < 2; k++) {
            mesh.colors[cIdx++] = 255; mesh.colors[cIdx++] = 255; mesh.colors[cIdx++] = 255; mesh.colors[cIdx++] = 40;
        }
    }

    // Concentric Circles
    for (int r = 1; r <= circles; r++) {
        float radius = r * 25.0f;
        for (int i = 0; i < 64; i++) {
            float ang1 = (float)i / 64.0f * 2.0f * PI;
            float ang2 = (float)(i + 1) / 64.0f * 2.0f * PI;
            
            mesh.vertices[vIdx++] = cosf(ang1) * radius; mesh.vertices[vIdx++] = 0; mesh.vertices[vIdx++] = sinf(ang1) * radius;
            mesh.vertices[vIdx++] = cosf(ang2) * radius; mesh.vertices[vIdx++] = 0; mesh.vertices[vIdx++] = sinf(ang2) * radius;
            
            for (int k = 0; k < 2; k++) {
                mesh.colors[cIdx++] = 255; mesh.colors[cIdx++] = 255; mesh.colors[cIdx++] = 255; mesh.colors[cIdx++] = 30;
            }
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

void InitLumenFidei() {
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = {0,1,0};
    camera.position = {0, CAMERA_HEIGHT, CAMERA_DISTANCE};
    camera.target = {0, 0, 0};
    
    // Graphics Init (Sacred Geometry)
    sphereMesh = GenMeshSphere(1.0f, 32, 32); 
    cylinderMesh = GenMeshTorus(0.8f, 1.2f, 32, 32); // Halo Rings
    cubeMesh = GenMeshKnot(0.8f, 1.2f, 128, 32); // Complex Vice Core
    floorLinesMesh = GenRadiantFloorMesh();
    polyMesh = GenMeshPoly(6, 1.5f); // Hexagonal prisms
    
    // Load Shaders
    Shader sh = LoadShaderFromMemory(vsCode, fsCode);
    divineMat = LoadMaterialDefault();
    divineMat.shader = sh;
    
    postShader = LoadShaderFromMemory(0, fsPost);
    
    // Initialize Uniform Locations
    float rimP = 3.0f;
    SetShaderValue(sh, GetShaderLocation(sh, "rimPower"), &rimP, SHADER_UNIFORM_FLOAT);
    
    target = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    
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
    
    // Boss Waves
    if (vigilCount % 5 == 0) {
        Vice v;
        v.type = CARDINAL_SIN;
        // Significant health boost for Multi-Phase encounter
        v.maxCorruption = 8000.0f + (vigilCount - 5) * 4000.0f;
        v.corruption.store(v.maxCorruption);
        v.moveSpeed = 6.5f; // Now they can move!
        v.scale = 6.5f + (vigilCount * 0.25f);
        v.state = VICE_IDLE;
        v.stateTimer = 0.8f; // Faster start
        v.attackTimer = 0.0f;
        v.pos = {0, 0, -50};
        vices.push_back(std::move(v));
    } else {
        int count = 6 + (vigilCount * 2);
        for (int i = 0; i < count; i++) {
            Vice v;
            v.type = (i % 6 == 0) ? RAGER : (i % 4 == 0) ? ACCUSER : WHISPERER;
            v.maxCorruption = (v.type == RAGER) ? 180 : (v.type == ACCUSER) ? 120 : 50;
            v.corruption.store(v.maxCorruption);
            v.moveSpeed = (v.type == WHISPERER) ? 9.5f : (v.type == RAGER) ? 8.0f : 7.0f;
            v.scale = (v.type == RAGER) ? 2.0f : (v.type == ACCUSER) ? 1.6f : 0.9f;
            v.formationAngle = ((float)i / count) * 2.0f * PI; // Coordinated spread
            v.attackTimer = GetRandomValue(10, 50) / 10.0f;
            
            float ang = GetRandomValue(0, 360) * DEG2RAD;
            float dist = GetRandomValue(45, 80);
        v.pos = {cosf(ang) * dist, 0, sinf(ang) * dist};
        vices.push_back(std::move(v));
        }
    }
    initialViceCount = (int)vices.size();

    // Mansion Transition Detection
    if (vigilCount == 1 || vigilCount == 6 || vigilCount == 11 || vigilCount == 16 || vigilCount == 21) {
        mansionTitleTimer = 4.0f;
        if (vigilCount == 1) currentMansionName = "Mansion I: The Courtyard of Humility";
        else if (vigilCount == 6) currentMansionName = "Mansion II: The Desert of Temptation";
        else if (vigilCount == 11) currentMansionName = "Mansion III: The Sea of Peace";
        else if (vigilCount == 16) currentMansionName = "Mansion IV: The Inner Sanctum";
        else currentMansionName = "Mansion V: The Void Core";
    }
}

// Helper for world-space text labels
void DrawText3D(const char* text, Vector3 pos, int fontSize, Color color) {
    Vector2 screenPos = GetWorldToScreen(pos, camera);
    DrawText(text, (int)screenPos.x - MeasureText(text, fontSize)/2, (int)screenPos.y, fontSize, color);
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

    // Regeneration (Atomic Spirit)
    if (!guardian.isShielding && !guardian.isDashing && !guardian.isPraying) {
        float s = guardian.spirit.load();
        if (s < guardian.maxSpirit) {
            guardian.spirit.store(std::min(s + SPIRIT_REGEN * dt, guardian.maxSpirit));
        }
    }

    // Input: Aiming
    Vector2 mousePos = GetMousePosition();
    Ray ray = GetMouseRay(mousePos, camera);
    float t = -ray.position.y / ray.direction.y;
    Vector3 groundPos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    Vector3 lookDir = Vector3Subtract(groundPos, guardian.pos);
    guardian.facingAngle = atan2f(lookDir.x, lookDir.z);

    // Input: Divine Inspirations (Spend Spirit Points)
    if (guardian.spiritPoints > 0) {
        if (IsKeyPressed(KEY_ONE)) {
            guardian.parryWindow += 0.04f; guardian.spiritPoints--; guardian.haloScale += 0.12f;
            SpawnMotes(guardian.pos, WHITE, 30, 12.0f);
        } else if (IsKeyPressed(KEY_TWO)) {
            guardian.truthSpeedMult += 0.40f; guardian.spiritPoints--; guardian.haloScale += 0.12f;
            SpawnMotes(guardian.pos, GOLD, 30, 12.0f);
        } else if (IsKeyPressed(KEY_THREE)) {
            guardian.maxGrace += 50.0f;
            float g = guardian.grace.load();
            guardian.grace.store(g + 50.0f);
            guardian.spiritPoints--; guardian.haloScale += 0.12f;
            SpawnMotes(guardian.pos, PINK, 30, 12.0f);
        } else if (IsKeyPressed(KEY_FOUR)) {
            guardian.dashCostMult *= 0.85f; 
            guardian.spiritPoints--; guardian.haloScale += 0.12f;
            SpawnMotes(guardian.pos, SKYBLUE, 30, 12.0f);
        } else if (IsKeyPressed(KEY_FIVE)) {
            guardian.meritMult += 0.25f; guardian.spiritPoints--; guardian.haloScale += 0.12f;
            SpawnMotes(guardian.pos, GOLD, 30, 12.0f, MOTE_DOVE);
        } else if (IsKeyPressed(KEY_SIX)) {
            guardian.fervorRegenMult += 0.35f; guardian.spiritPoints--; guardian.haloScale += 0.12f;
            SpawnMotes(guardian.pos, ORANGE, 30, 12.0f);
        }
    }

    // Input: Censer of Mercy (Left Click)
    guardian.comboTimer -= dt;
    if (guardian.comboTimer <= 0 && !guardian.isSwinging) {
        guardian.comboStep = 0;
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !guardian.isSwinging && guardian.spirit.load() >= 10.0f) {
        guardian.isSwinging = true;
        guardian.spirit.store(guardian.spirit.load() - 10.0f);
        
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
    if (IsKeyPressed(KEY_SPACE) && !guardian.isCrossing && guardian.spirit.load() >= 40.0f) {
        guardian.isCrossing = true;
        guardian.crossTimer = guardian.crossDuration;
        guardian.spirit.store(guardian.spirit.load() - 40.0f);
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
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && guardian.spirit.load() > 0 && !guardian.isSwinging) {
        if (!guardian.isShielding) {
            guardian.shieldTimer = 0.0f;
        }
        guardian.isShielding = true;
        guardian.shieldTimer += dt;
        guardian.spirit.store(guardian.spirit.load() - SPIRIT_DRAIN_SHIELD * dt);
    } else {
        guardian.isShielding = false;
        guardian.shieldTimer = 0.0f;
    }
    
    // Input: Pax (E) - Freeze Doubts
    if (IsKeyPressed(KEY_E) && guardian.spirit.load() >= WORD_COST && guardian.wordCooldown <= 0.0f) {
        guardian.spirit.store(guardian.spirit.load() - WORD_COST);
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
        float g = guardian.grace.load();
        if (g < guardian.maxGrace) guardian.grace.store(std::min(g + 15.0f * dt, guardian.maxGrace));
        
        // Restore fervor (int)
        static float fervorAccumulator = 0.0f;
        fervorAccumulator += 50.0f * dt;
        if (fervorAccumulator >= 1.0f) {
            int toAdd = (int)fervorAccumulator;
            guardian.fervor.fetch_add(toAdd);
            fervorAccumulator -= (float)toAdd;
        }
        
        if (fmodf(GetTime(), 0.1f) < dt) {
            SpawnMotes(Vector3Add(guardian.pos, {0, 10.0f, 0}), WHITE, 2, 5.0f);
            SpawnMotes(guardian.pos, COL_GRACE, 1, 2.0f);
        }
    } else {
        guardian.isPraying = false;
    }

    // Input: Movement (WASD) - KINETIC DRIFT PHYSICS
    float currentSpeed = Vector3Length(guardian.vel);
    if (!guardian.isPraying) {
        Vector3 inputDir = {0,0,0};
        if (IsKeyDown(KEY_W)) inputDir.z -= 1;
        if (IsKeyDown(KEY_S)) inputDir.z += 1;
        if (IsKeyDown(KEY_A)) inputDir.x -= 1;
        if (IsKeyDown(KEY_D)) inputDir.x += 1;
        
        if (Vector3Length(inputDir) > 0.1f) {
            inputDir = Vector3Normalize(inputDir);
            // Smoothly rotate heading towards input direction
            float targetHeading = atan2f(inputDir.x, inputDir.z);
            float angleDiff = targetHeading - guardian.heading;
            while (angleDiff > PI) angleDiff -= 2*PI;
            while (angleDiff < -PI) angleDiff += 2*PI;
            guardian.heading += angleDiff * 10.0f * dt;
            
            // Apply Acceleration in Heading direction (Fortitude Buff)
            float currentAccel = guardian.hasFortitude ? PLAYER_ACCEL * 1.4f : PLAYER_ACCEL;
            Vector3 force = {sinf(guardian.heading), 0, cosf(guardian.heading)};
            guardian.vel = Vector3Add(guardian.vel, Vector3Scale(force, currentAccel * dt));
            
            // Visual Tilting (Physics Based)
            float turnSharpness = angleDiff * currentSpeed * 0.1f;
            guardian.tiltZ = Lerp(guardian.tiltZ, -turnSharpness * 0.8f, 8.0f * dt);
            guardian.tiltX = Lerp(guardian.tiltX, 0.2f + (currentSpeed / PLAYER_MAX_SPEED) * 0.2f, 5.0f * dt);
            
            if (fmodf(GetTime(), 0.05f) < dt && currentSpeed > 10.0f) {
                SpawnMotes(Vector3Subtract(guardian.pos, Vector3Scale(force, 2.0f)), COL_SPIRIT, 1, 3.0f);
            }
        } else {
            guardian.tiltX = Lerp(guardian.tiltX, 0.0f, 5.0f * dt);
            guardian.tiltZ = Lerp(guardian.tiltZ, 0.0f, 5.0f * dt);
        }
        
        // Attack Leaning (Overwrites movement tilt for impact)
        if (guardian.isSwinging) {
            float swingProgress = 1.0f - (guardian.swingTimer / guardian.swingDuration);
            float leanIntensity = sinf(swingProgress * PI) * 0.8f; // Deep lunge at midpoint
            guardian.tiltX = Lerp(guardian.tiltX, leanIntensity, 20.0f * dt);
        }
        
        // Saintly Traction: Dampen lateral velocity to allow for drifts
        if (Vector3Length(guardian.vel) > 0.1f) {
            Vector3 forward = {sinf(guardian.heading), 0, cosf(guardian.heading)};
            Vector3 right = {forward.z, 0, -forward.x};
            
            float lateralVel = Vector3DotProduct(guardian.vel, right);
            guardian.vel = Vector3Subtract(guardian.vel, Vector3Scale(right, lateralVel * PLAYER_TRACTION * dt));
        }

        // Dash (Shift) - PHYSICS IMPULSE
        float currentDashCost = DASH_COST * guardian.dashCostMult;
        if (IsKeyPressed(KEY_LEFT_SHIFT) && guardian.spirit.load() >= currentDashCost && !guardian.isDashing && Vector3Length(inputDir) > 0.1f) {
            guardian.isDashing = true;
            guardian.dashTimer = DASH_DURATION;
            guardian.dashDir = inputDir;
            guardian.spirit.store(guardian.spirit.load() - currentDashCost);
            guardian.vel = Vector3Add(guardian.vel, Vector3Scale(inputDir, DASH_IMPULSE));
            screenShake = 0.2f;
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
    currentSpeed = Vector3Length(guardian.vel);
    if (currentSpeed > PLAYER_MAX_SPEED && !guardian.isDashing) {
        guardian.vel = Vector3Scale(Vector3Normalize(guardian.vel), PLAYER_MAX_SPEED);
    }
    
    // Update Position
    guardian.pos = Vector3Add(guardian.pos, Vector3Scale(guardian.vel, dt));
    
    // 1. Weapon Trail Collection (Censer Ribbon)
    if (guardian.isSwinging) {
        float progress = 1.0f - (guardian.swingTimer / guardian.swingDuration);
        float ang = guardian.facingAngle + (progress - 0.5f) * guardian.swingArc;
        Vector3 censerPos = Vector3Add(guardian.pos, {sinf(ang) * guardian.swingRange, 2.0f, cosf(ang) * guardian.swingRange});
        guardian.weaponTrail.push_front(censerPos);
    }
    while (guardian.weaponTrail.size() > 20) guardian.weaponTrail.pop_back();
    if (!guardian.isSwinging && !guardian.weaponTrail.empty()) {
        guardian.weaponTrail.pop_back(); // Fade out when not swinging
    }

    // 2. Ghost Trail Logic (Fixed Collapse)
    currentSpeed = Vector3Length(guardian.vel);
    bool movedFarEnough = guardian.ghosts.empty() || Vector3Distance(guardian.pos, guardian.ghosts.front()) > 3.0f;
    
    // Throttle updates during stationary attacks to prevent history collapse
    static float ghostThrottle = 0.0f;
    ghostThrottle += dt;
    bool actionThrottle = (guardian.isSwinging || guardian.isDashing) && ghostThrottle > 0.05f;

    if ((currentSpeed > 8.0f && movedFarEnough) || actionThrottle) {
        guardian.ghosts.push_front(guardian.pos);
        guardian.ghostAngles.push_front(guardian.facingAngle);
        guardian.ghostTiltsX.push_front(guardian.tiltX);
        guardian.ghostTiltsZ.push_front(guardian.tiltZ);
        ghostThrottle = 0.0f;
    }
    
    size_t maxGhosts = (guardian.isSwinging || guardian.isDashing) ? 12 : 4; 
    while (guardian.ghosts.size() > maxGhosts) {
        guardian.ghosts.pop_back();
        guardian.ghostAngles.pop_back();
        guardian.ghostTiltsX.pop_back();
        guardian.ghostTiltsZ.pop_back();
    }
    
    // Only clear if completely idle
    if (currentSpeed < 1.0f && !guardian.isSwinging && !guardian.isDashing && !guardian.ghosts.empty()) {
        ghostThrottle += dt;
        if (ghostThrottle > 0.1f) {
            guardian.ghosts.pop_back();
            guardian.ghostAngles.pop_back();
            guardian.ghostTiltsX.pop_back();
            guardian.ghostTiltsZ.pop_back();
            ghostThrottle = 0.0f;
        }
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
                    // Auto-Redemption check (Fixes debug K and edge cases)
                    if (v.corruption <= 0 && !v.redeemed) {
                        bool alreadyRedeemed = v.redeemed.exchange(true);
                        if (!alreadyRedeemed) {
                            guardian.merit.fetch_add(100);
                            SpawnMotes(v.pos, GOLD, 50, 10.0f, MOTE_SPARK, &threadMotes[t]);
                        }
                    }

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
                            }
                        }
                    }

                    // Chase behavior (Tactical & Coordinated)
                    Vector3 toPlayer = Vector3Subtract(guardian.pos, v.pos);
                    toPlayer.y = 0;
                    float dist = Vector3Length(toPlayer);
                    Vector3 dir = Vector3Normalize(toPlayer);
                    
                    // 1. Coordinated Formation Target
                    v.formationAngle += 0.2f * dt; // Slow orbit
                    float targetDist = (v.type == WHISPERER) ? 30.0f : (v.type == ACCUSER) ? 22.0f : 12.0f;
                    Vector3 formationOffset = { cosf(v.formationAngle) * targetDist, 0, sinf(v.formationAngle) * targetDist };
                    Vector3 targetPos = Vector3Add(guardian.pos, formationOffset);
                    Vector3 formationDir = Vector3Normalize(Vector3Subtract(targetPos, v.pos));

                    // 2. Separation Force (Avoid Clumping)
                    Vector3 separation = {0,0,0};
                    for (const auto& other : vices) {
                        if (&other == &v) continue;
                        float d = Vector3Distance(v.pos, other.pos);
                        if (d < 6.0f) {
                            separation = Vector3Add(separation, Vector3Scale(Vector3Normalize(Vector3Subtract(v.pos, other.pos)), (6.0f - d) * 2.0f));
                        }
                    }

                    // 3. Apply Combined Forces
                    Vector3 finalMoveDir = Vector3Normalize(Vector3Add(Vector3Scale(formationDir, 1.0f), Vector3Scale(separation, 1.5f)));
                    v.pos = Vector3Add(v.pos, Vector3Scale(finalMoveDir, v.moveSpeed * dt));

                    if (v.type == RAGER && dist < 18.0f && guardian.isSwinging) {
                        // Elite Reflex Dodge (unchanged but faster)
                        Vector3 dodge = {dir.z, 0, -dir.x};
                        if (GetRandomValue(0, 1) == 0) dodge = Vector3Negate(dodge);
                        v.pos = Vector3Add(v.pos, Vector3Scale(dodge, v.moveSpeed * 6.0f * dt));
                        SpawnMotes(v.pos, currentViceCol, 1, 2.0f, MOTE_SPARK, &threadMotes[t]);
                    }
                    
                    // Attack behavior
                    v.attackTimer -= dt;
                    if (v.attackTimer <= 0.0f) {
                        Vector3 spawnPos = Vector3Add(v.pos, {0, 2.0f, 0}); 
                        
                        if (v.type == CARDINAL_SIN) {
                            // Boss logic handled separately below
                        } else if (director.attackTokens > 0) {
                            // Regular Enemy Attack (Consumes Token)
                            director.attackTokens--;
                            
                            if (v.type == WHISPERER) {
                                // Predictive Shot
                                Vector3 futureDir = Vector3Normalize(Vector3Subtract(director.predictedPlayerPos, spawnPos));
                                SpawnTemptation(spawnPos, Vector3Scale(futureDir, 22.0f), COL_TEMPTATION, false, &threadTemps[t]);
                                v.attackTimer = 1.2f;
                            } else if (v.type == ACCUSER) {
                                // Burst Fire
                                Vector3 toP = Vector3Normalize(Vector3Subtract(guardian.pos, spawnPos));
                                SpawnTemptation(spawnPos, Vector3Scale(toP, 16.0f), COL_TEMPTATION, false, &threadTemps[t]);
                                v.attackTimer = 0.5f; // Fast burst
                            } else if (v.type == RAGER) {
                                // Charge Impulse
                                v.pos = Vector3Add(v.pos, Vector3Scale(dir, 12.0f)); 
                                SpawnTemptation(spawnPos, Vector3Scale(dir, 28.0f), MAROON, false, &threadTemps[t]);
                                v.attackTimer = 2.5f;
                            }
                        } else {
                            v.attackTimer = 0.2f; // Wait for token
                        }
                        
                        // Boss Logic Block (Unchanged)
                        if (v.type == CARDINAL_SIN) {
                            v.stateTimer -= dt;
                            Vector3 spawnPos = Vector3Add(v.pos, {0, 2.0f, 0});
                            Vector3 toGuardian = Vector3Subtract(guardian.pos, v.pos);
                            float distToG = Vector3Length(toGuardian);
                            Vector3 dirToG = Vector3Normalize(toGuardian);

                            if (v.state == VICE_IDLE) {
                                // Relentless Pursuit
                                v.pos = Vector3Add(v.pos, Vector3Scale(dirToG, v.moveSpeed * dt));
                                
                                if (v.stateTimer <= 0) {
                                    v.state = VICE_TELEGRAPH;
                                    v.stateTimer = 0.65f; // Fast telegraph
                                    // High chance for melee if player is close
                                    if (distToG < 22.0f) v.patternIndex = (GetRandomValue(0, 10) < 7) ? 2 : GetRandomValue(0, 1);
                                    else v.patternIndex = GetRandomValue(0, 1);
                                }
                            } else if (v.state == VICE_TELEGRAPH) {
                                // Slow down but keep tracking
                                v.pos = Vector3Add(v.pos, Vector3Scale(dirToG, v.moveSpeed * 0.3f * dt));
                                if (v.stateTimer <= 0) {
                                    v.state = VICE_ATTACK;
                                    v.stateTimer = (v.patternIndex == 2) ? 0.8f : 2.5f; 
                                }
                            } else if (v.state == VICE_ATTACK) {
                                // Move at half speed while attacking
                                v.pos = Vector3Add(v.pos, Vector3Scale(dirToG, v.moveSpeed * 0.5f * dt));
                                
                                v.attackTimer -= dt;
                                if (v.attackTimer <= 0) {
                                    if (vigilCount == 5) { // PRIDE
                                        if (v.patternIndex == 0) { // Cross Burst
                                            for (int k = 0; k < 8; k++) {
                                                float ang = k * PI/4 + GetTime() * 2.0f;
                                                Vector3 crossDir = {cosf(ang), 0, sinf(ang)};
                                                SpawnTemptation(spawnPos, Vector3Scale(crossDir, 22.0f), COL_TEMPTATION, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.3f;
                                        } else if (v.patternIndex == 1) { // Focused Doubts
                                            for(int k=-3; k<=3; k++) {
                                                Vector3 spread = Vector3RotateByAxisAngle(dirToG, {0,1,0}, k * 0.12f);
                                                SpawnTemptation(spawnPos, Vector3Scale(spread, 28.0f), MAROON, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.12f;
                                        } else { // MELEE: Divine Rebuke
                                            for(int k=0; k<48; k++) {
                                                float ang = (float)k/48.0f * 2*PI;
                                                Vector3 d = {cosf(ang), 0, sinf(ang)};
                                                SpawnTemptation(spawnPos, Vector3Scale(d, 40.0f), GOLD, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.8f;
                                            screenShake = 0.5f;
                                        }
                                    } else if (vigilCount == 10) { // GREED
                                        if (v.patternIndex == 0) { // Hyper Rain
                                            for(int k=0; k<25; k++) {
                                                Vector3 rPos = { guardian.pos.x + GetRandomValue(-40,40), 50.0f, guardian.pos.z + GetRandomValue(-40,40) };
                                                SpawnTemptation(rPos, {0,-35,0}, GOLD, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.06f;
                                        } else if (v.patternIndex == 1) { // Spiral Coin
                                            for(int k=0; k<4; k++) {
                                                float a = GetTime() * 10.0f + k * PI/2;
                                                SpawnTemptation(spawnPos, {cosf(a)*20, 0, sinf(a)*20}, GOLD, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.1f;
                                        } else { // MELEE: Avarice Storm
                                            for(int k=0; k<15; k++) {
                                                Vector3 offset = {(float)GetRandomValue(-20,20), 0, (float)GetRandomValue(-20,20)};
                                                SpawnTemptation(Vector3Add(v.pos, offset), {0, 5, 0}, GOLD, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.05f;
                                        }
                                    } else if (vigilCount == 15) { // ENVY
                                        if (v.patternIndex == 0) { // Converging Ring
                                            float r = 45.0f;
                                            for(int k=0; k<32; k++) {
                                                float a = (float)k/32.0f * 2*PI;
                                                Vector3 p = Vector3Add(guardian.pos, {cosf(a)*r, 2.0f, sinf(a)*r});
                                                SpawnTemptation(p, Vector3Scale(Vector3Normalize(Vector3Subtract(guardian.pos, p)), 18.0f), GREEN, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 2.0f;
                                        } else if (v.patternIndex == 1) { // Mimic Strike
                                            SpawnTemptation(spawnPos, Vector3Scale(dirToG, 55.0f), GREEN, false, &threadTemps[t]);
                                            v.attackTimer = 0.04f;
                                        } else { // MELEE: Mirror Blast
                                            for(int k=0; k<60; k++) {
                                                float a = (float)k/60.0f * 2*PI;
                                                SpawnTemptation(spawnPos, Vector3Scale({cosf(a), 0, sinf(a)}, 35.0f), GREEN, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.8f;
                                        }
                                    } else { // WRATH
                                        if (v.patternIndex == 0) { // Sunfire Beam
                                            for(int k=-5; k<=5; k++) {
                                                Vector3 s = Vector3RotateByAxisAngle(dirToG, {0,1,0}, k * 0.08f + sinf(GetTime()*15)*0.4f);
                                                SpawnTemptation(spawnPos, Vector3Scale(s, 35.0f), RED, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.03f;
                                        } else if (v.patternIndex == 1) { // Chaos Nova
                                            for(int k=0; k<40; k++) {
                                                float a = (float)k/40.0f * 2*PI + GetTime();
                                                SpawnTemptation(spawnPos, Vector3Scale({cosf(a), 0, sinf(a)}, 30.0f), MAROON, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.5f;
                                        } else { // MELEE: Blood Lunge
                                            v.pos = Vector3Add(v.pos, Vector3Scale(dirToG, 25.0f * dt * 60.0f)); // Super fast lunge
                                            for(int k=0; k<60; k++) {
                                                float a = (float)k/60.0f * 2*PI;
                                                SpawnTemptation(v.pos, Vector3Scale({cosf(a), 0, sinf(a)}, 20.0f), RED, false, &threadTemps[t]);
                                            }
                                            v.attackTimer = 0.8f;
                                            screenShake = 0.7f;
                                        }
                                    }
                                }
                                if (v.stateTimer <= 0) {
                                    v.state = VICE_RECOVER;
                                    v.stateTimer = 0.85f; // Extremely short recovery
                                }
                            } else if (v.state == VICE_RECOVER) {
                                if (v.stateTimer <= 0) {
                                    v.state = VICE_IDLE;
                                    v.stateTimer = 0.5f; // Ready to chase again almost immediately
                                }
                            }
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
    if (numT == 0) return;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    size_t chunk = (numT + numThreads - 1) / numThreads;

    std::vector<std::vector<LightMote>> threadMotes(numThreads);

    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * chunk;
        size_t endd = std::min(start + chunk, numT);
        if (start >= endd) continue;

        futures.push_back(g_pool->enqueue([start, endd, currentDt, dt, t, &threadMotes]() {
            for (size_t i = start; i < endd; ++i) {
                Temptation& temp = temptations[i];
                if (temp.frozenTimer > 0) {
                    temp.frozenTimer -= dt;
                    continue; 
                }
                
                // 1. Movement
                temp.pos = Vector3Add(temp.pos, Vector3Scale(temp.vel, currentDt));
                temp.life -= currentDt;
                temp.trail.push_front(temp.pos);
                if (temp.trail.size() > 12) temp.trail.pop_back();

                // Ground Collision (Splash)
                if (temp.pos.y <= 0.0f) {
                    temp.life = 0;
                    SpawnMotes(temp.pos, (temp.isTruth ? WHITE : temp.color), 4, 5.0f, MOTE_SPARK, &threadMotes[t]);
                    continue;
                }

                if (temp.isTruth) {
                    // Collision with Vices (Redemption)
                    if (temp.life <= 0) continue;
                    for (auto& v : vices) {
                        if (v.redeemed) continue;
                        if (Vector3Distance(temp.pos, v.pos) < v.scale * 2.0f) {
                            v.corruption.store(v.corruption - 35.0f); 
                            temp.life = 0; 
                            SpawnMotes(temp.pos, COL_SPIRIT, 15, 8.0f, MOTE_SPARK, &threadMotes[t]);
                            
                            if (v.corruption <= 0) {
                                bool alreadyRedeemed = v.redeemed.exchange(true);
                                if (!alreadyRedeemed) {
                                    guardian.merit.fetch_add((int)(100 * guardian.meritMult));
                                    SpawnMotes(v.pos, GOLD, 50, 10.0f, MOTE_SPARK, &threadMotes[t]);
                                    
                                    if (GetRandomValue(0, 100) < 35) {
                                        LoveHeart h;
                                        h.pos = v.pos; h.pos.y = 2.0f;
                                        h.life = 10.0f;
                                        h.floatOffset = (float)GetRandomValue(0, 100);
                                        std::lock_guard<std::mutex> lock(sharedMutex);
                                        loveHearts.push_back(h);
                                    }

                                    // Rare Blessing Drop (2%)
                                    if (GetRandomValue(0, 100) < 2) {
                                        BlessingItem b;
                                        b.pos = v.pos; b.pos.y = 2.5f;
                                        b.type = (BlessingType)GetRandomValue(0, 2);
                                        b.life = 20.0f;
                                        b.floatOffset = (float)GetRandomValue(0, 100);
                                        std::lock_guard<std::mutex> lock(sharedMutex);
                                        blessingItems.push_back(b);
                                    }

                                    // Spawn Saintly Echo (F16)
                                    SaintlyEcho echo;
                                    echo.pos = v.pos; echo.pos.y = 0.1f;
                                    echo.life = 6.0f;
                                    echo.scale = v.scale * 2.0f;
                                    std::lock_guard<std::mutex> lock(sharedMutex);
                                    saintlyEchoes.push_back(echo);
                                }
                            }
                            break;
                        }
                    }
                } else {
                    // Collision with Guardian (Censer, Shield, or Body)
                    if (temp.life <= 0) continue;
                    float dist = Vector3Distance(temp.pos, guardian.pos);
                    
                    // Censer Swing Check
                    if (guardian.isSwinging && dist < guardian.swingRange + 2.0f) {
                        Vector3 toBullet = Vector3Subtract(temp.pos, guardian.pos);
                        float angleToBullet = atan2f(toBullet.x, toBullet.z);
                        float angleDiff = fabs(angleToBullet - guardian.facingAngle);
                        while (angleDiff > PI) angleDiff -= 2*PI;
                        while (angleDiff < -PI) angleDiff += 2*PI;

                        if (fabs(angleDiff) < guardian.swingArc / 2.0f) {
                            temp.vel = Vector3Scale(Vector3Normalize(Vector3Negate(temp.vel)), Vector3Length(temp.vel) * 2.0f);
                            temp.isTruth = true;
                            temp.color = COL_TRUTH;
                            temp.life = 6.0f;
                            SpawnMotes(temp.pos, COL_GRACE, 12, 12.0f, MOTE_SPARK, &threadMotes[t]);
                            guardian.merit.fetch_add((int)(5 * guardian.meritMult)); 
                            guardian.fervor.fetch_add((int)(10 * guardian.fervorRegenMult)); 
                            continue;
                        }
                    }

                    // Sign of the Cross Check
                    if (guardian.isCrossing && dist < 12.0f) {
                        Vector3 local = Vector3Subtract(temp.pos, guardian.pos);
                        if (fabs(local.x) < 2.0f || fabs(local.z) < 2.0f) {
                            temp.isTruth = true;
                            temp.color = COL_TRUTH;
                            temp.vel = Vector3Scale(Vector3Normalize(Vector3Negate(temp.vel)), Vector3Length(temp.vel) * 1.5f);
                            temp.life = 7.0f;
                            SpawnMotes(temp.pos, WHITE, 5, 8.0f, MOTE_SPARK, &threadMotes[t]);
                            continue;
                        }
                    }

                    if (dist < 3.0f) {
                        Vector3 toBullet = Vector3Subtract(temp.pos, guardian.pos);
                        float angleToBullet = atan2f(toBullet.x, toBullet.z);
                        float angleDiff = fabs(angleToBullet - guardian.facingAngle);
                        while (angleDiff > PI) angleDiff -= 2*PI;
                        while (angleDiff < -PI) angleDiff += 2*PI;
                        
                        bool blocked = guardian.isShielding && (fabs(angleDiff) < SHIELD_ARC / 2.0f);
                        
                        if (blocked) {
                            bool perfect = guardian.shieldTimer < guardian.parryWindow;
                            temp.vel = Vector3Scale(Vector3Normalize(Vector3Negate(temp.vel)), Vector3Length(temp.vel) * 1.5f * guardian.truthSpeedMult);
                            temp.isTruth = true;
                            temp.color = COL_TRUTH;
                            temp.life = 5.0f;
                            SpawnMotes(temp.pos, COL_GRACE, 10, 10.0f, MOTE_SPARK, &threadMotes[t]);
                            if (perfect) {
                                guardian.fervor.fetch_add((int)(5 * guardian.fervorRegenMult));
                            }
                        } else if (guardian.hitStun <= 0.0f && !guardian.isDashing) {
                            // HIT handled on main thread or via atomic
                            temp.life = 0;
                            if (!godMode) {
                                float currentG = guardian.grace.load();
                                while (!guardian.grace.compare_exchange_weak(currentG, currentG - 15.0f));
                            }
                        }
                    }
                }
            }
        }));
    }
    for (auto& fut : futures) fut.wait();

    // Merge motes
    for (int t = 0; t < numThreads; t++) {
        if (!threadMotes[t].empty()) {
            motes.insert(motes.end(), threadMotes[t].begin(), threadMotes[t].end());
        }
    }

    // Cleanup (Sequential)
    temptations.erase(std::remove_if(temptations.begin(), temptations.end(), 
        [](const Temptation& t){ return t.life <= 0; }), temptations.end());
}

void SpawnTemptation(Vector3 pos, Vector3 vel, Color col, bool isTruth, std::vector<Temptation>* localList) {
    Temptation t;
    t.pos = pos;
    t.vel = vel;
    t.color = col;
    t.life = 5.0f;
    t.isTruth = isTruth;
    t.frozenTimer = 0.0f;
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
    
    // Debug Controls
    if (IsKeyPressed(KEY_TAB)) debugMode = !debugMode;
    if (debugMode) {
        if (IsKeyPressed(KEY_F1)) { vigilCount = 1; StartVigil(false); }
        if (IsKeyPressed(KEY_F2)) { vigilCount = 10; StartVigil(false); }
        if (IsKeyPressed(KEY_F3)) { vigilCount = 15; StartVigil(false); }
        if (IsKeyPressed(KEY_F4)) { vigilCount = 20; StartVigil(false); }
        if (IsKeyPressed(KEY_G)) godMode = !godMode;
        if (IsKeyPressed(KEY_M)) guardian.merit.fetch_add(1000);
        if (IsKeyPressed(KEY_P)) guardian.fervor.store(1000);
        if (IsKeyPressed(KEY_K)) { for(auto& v : vices) v.corruption.store(-1.0f); }
    }

    // Biome Color Interpolation
    Color targetSky, targetFloor, targetVice, targetLine;
    if (vigilCount <= 5) { // Mansion I: Humility (Night/Garden)
        targetSky = {12, 12, 22, 255}; targetFloor = {18, 18, 28, 255}; targetVice = {35, 30, 45, 255}; targetLine = {255, 245, 180, 50};
    } else if (vigilCount <= 10) { // Mansion II: Temptation (Desert)
        targetSky = {45, 15, 15, 255}; targetFloor = {35, 10, 10, 255}; targetVice = {60, 20, 20, 255}; targetLine = {255, 255, 255, 60};
    } else if (vigilCount <= 15) { // Mansion III: Sea of Peace (Ocean)
        targetSky = {0, 20, 40, 255}; targetFloor = {0, 10, 25, 255}; targetVice = {0, 40, 40, 255}; targetLine = {0, 200, 255, 80};
    } else if (vigilCount <= 20) { // Mansion IV: Inner Sanctum (Heaven)
        targetSky = {200, 200, 220, 255}; targetFloor = {220, 220, 240, 255}; targetVice = {80, 80, 80, 255}; targetLine = {255, 215, 0, 100};
    } else { // Mansion V: The Void Core (Pure Light)
        targetSky = {250, 250, 255, 255}; targetFloor = {255, 255, 255, 255}; targetVice = {100, 100, 100, 255}; targetLine = {255, 215, 0, 150};
    }
    
    currentSkyTop = ColorLerp(currentSkyTop, targetSky, 2.0f * dt);
    currentFloorBase = ColorLerp(currentFloorBase, targetFloor, 2.0f * dt);
    currentViceCol = ColorLerp(currentViceCol, targetVice, 2.0f * dt);
    currentLineCol = ColorLerp(currentLineCol, targetLine, 2.0f * dt);

    mansionTitleTimer = std::max(0.0f, mansionTitleTimer - dt);
    
    // Ambient Atmosphere (Dust)
    if (fmodf(GetTime(), 0.02f) < dt) {
        Vector3 spawnPos = { guardian.pos.x + (float)GetRandomValue(-60,60), (float)GetRandomValue(2, 35), guardian.pos.z + (float)GetRandomValue(-60,60) };
        SpawnMotes(spawnPos, Fade(WHITE, 0.25f), 1, 0.3f);
    }

    // AI Director Update
    director.tokenRegenTimer -= dt;
    if (director.tokenRegenTimer <= 0) {
        if (director.attackTokens < 5 + (vigilCount / 4)) director.attackTokens++;
        director.tokenRegenTimer = 0.4f;
    }
    director.predictedPlayerPos = Vector3Add(guardian.pos, Vector3Scale(guardian.vel, 0.8f)); // Look ahead 0.8s

    // Mansion Hazards (F14)
    if (!guardian.isPraying) {
        if (vigilCount >= 6 && vigilCount <= 10) { // Mansion II: Wind
            float windStrength = 18.0f * (1.0f + 0.5f * sinf(GetTime() * 0.8f));
            Vector3 windDir = Vector3Normalize({1.0f, 0, 0.3f * sinf(GetTime() * 0.5f)});
            guardian.vel = Vector3Add(guardian.vel, Vector3Scale(windDir, windStrength * dt));
            if (fmodf(GetTime(), 0.1f) < dt) SpawnMotes(Vector3Add(guardian.pos, {-20, 5, 0}), WHITE, 1, 10.0f);
        } else if (vigilCount >= 11 && vigilCount <= 15) { // Mansion III: Whirlpool
            Vector3 toCenter = Vector3Normalize(Vector3Subtract({0,0,0}, guardian.pos));
            float pullStrength = Vector3Distance({0,0,0}, guardian.pos) * 0.4f;
            guardian.vel = Vector3Add(guardian.vel, Vector3Scale(toCenter, pullStrength * dt));
        }
    }

    // Update Saintly Echoes (F16)
    for (auto it = saintlyEchoes.begin(); it != saintlyEchoes.end();) {
        it->life -= dt;
        float dist = Vector3Distance(it->pos, guardian.pos);
        if (dist < 12.0f) {
            // Standing in the aura of a saintly echo restores fervor fast
            guardian.fervor.fetch_add(1); 
            if (fmodf(GetTime(), 0.2f) < dt) SpawnMotes(guardian.pos, GOLD, 1, 2.0f);
        }
        if (it->life <= 0) it = saintlyEchoes.erase(it);
        else ++it;
    }

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
    
    // Divine Leveling (F20)
    while (guardian.merit >= guardian.nextLevelJoy) {
        guardian.level++;
        guardian.spiritPoints++;
        guardian.nextLevelJoy += 1000 + (guardian.level * 150); // Progressive scaling
        SpawnMotes(guardian.pos, GOLD, 60, 12.0f, MOTE_DOVE);
        screenShake = 0.3f;
    }

    // Update Divine Love
    for (auto it = loveHearts.begin(); it != loveHearts.end();) {
        it->life -= dt;
        it->pos.y = 2.0f + 0.5f * sinf(GetTime() * 3.0f + it->floatOffset);
        
        // Aura of Charity: Drift towards player
        if (guardian.hasCharity) {
            Vector3 toP = Vector3Normalize(Vector3Subtract(guardian.pos, it->pos));
            it->pos = Vector3Add(it->pos, Vector3Scale(toP, 12.0f * dt));
        }

        float dist = Vector3Distance(it->pos, guardian.pos);
        if (dist < 4.0f) {
            float g = guardian.grace.load();
            if (g < guardian.maxGrace) guardian.grace.store(std::min(g + 25.0f, guardian.maxGrace));
            SpawnMotes(it->pos, PINK, 20, 10.0f, MOTE_SPARK);
            it = loveHearts.erase(it);
        } else if (it->life <= 0) {
            it = loveHearts.erase(it);
        } else {
            ++it;
        }
    }

    // Update Blessings (Relics)
    for (auto it = blessingItems.begin(); it != blessingItems.end();) {
        it->life -= dt;
        it->pos.y = 2.5f + 0.3f * sinf(GetTime() * 4.0f + it->floatOffset);
        
        float dist = Vector3Distance(it->pos, guardian.pos);
        if (dist < 4.5f) {
            if (it->type == BLESS_CHARITY) guardian.hasCharity = true;
            else if (it->type == BLESS_PURITY) guardian.hasPurity = true;
            else if (it->type == BLESS_FORTITUDE) guardian.hasFortitude = true;
            
            SpawnMotes(it->pos, GOLD, 40, 15.0f, MOTE_SPARK);
            mansionTitleTimer = 3.0f; // Brief notification pulse
            it = blessingItems.erase(it);
        } else if (it->life <= 0) {
            it = blessingItems.erase(it);
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
    if (guardian.grace.load() <= 0) currentState = STATE_DESOLATION;
    if (vices.empty()) {
        vigilCount++;
        if (vigilCount > 25) {
            currentState = STATE_VICTORY;
        } else {
            StartVigil(false);
        }
    }
}

void DrawDivineMesh(Mesh mesh, Vector3 pos, Vector3 scale, Color col, float displacement = 0.0f) {
    Matrix mat = MatrixIdentity();
    mat = MatrixMultiply(mat, MatrixScale(scale.x, scale.y, scale.z));
    mat = MatrixMultiply(mat, MatrixTranslate(pos.x, pos.y, pos.z));
    divineMat.maps[MATERIAL_MAP_DIFFUSE].color = col;
    
    // Set Per-Draw Uniforms
    float disp = displacement;
    float time = (float)GetTime();
    SetShaderValue(divineMat.shader, GetShaderLocation(divineMat.shader, "displacementStrength"), &disp, SHADER_UNIFORM_FLOAT);
    SetShaderValue(divineMat.shader, GetShaderLocation(divineMat.shader, "time"), &time, SHADER_UNIFORM_FLOAT);
    
    DrawMesh(mesh, divineMat, mat);
}

void DrawFrame() {
    // Update Shader Uniforms
    SetShaderValue(divineMat.shader, GetShaderLocation(divineMat.shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
    Vector3 lightPos = {0, 50, 0};
    SetShaderValue(divineMat.shader, GetShaderLocation(divineMat.shader, "lightPos"), &lightPos, SHADER_UNIFORM_VEC3);
    float lightCol[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    SetShaderValue(divineMat.shader, GetShaderLocation(divineMat.shader, "lightColor"), lightCol, SHADER_UNIFORM_VEC4);

    BeginTextureMode(target);
    ClearBackground(currentSkyTop); 
    
    BeginMode3D(camera);
        // Atmospheric Sky Shell
        DrawSphere({0,0,0}, 250.0f, Fade(currentSkyTop, 0.3f));
        DrawSphereWires({0,0,0}, 255.0f, 16, 16, Fade(currentLineCol, 0.05f));

        // Stained Glass Radiant Floor (Baked)
        DrawPlane({0,-0.1f,0}, {250, 250}, currentFloorBase);
        DrawMesh(floorLinesMesh, LoadMaterialDefault(), MatrixIdentity());
        
        // Saintly Ghost Trail (Subtle Secondary Blur)
        if (!guardian.ghosts.empty()) {
            BeginBlendMode(BLEND_ADDITIVE);
            for (size_t i = 0; i < guardian.ghosts.size(); i++) {
                float t = (float)i / guardian.ghosts.size();
                float alpha = 0.15f * (1.0f - t * t); 
                
                // Match Player's Gold Palette
                Color ghostCol = ColorLerp(WHITE, COL_GRACE, t);
                ghostCol = Fade(ghostCol, alpha);
                
                rlPushMatrix();
                rlTranslatef(guardian.ghosts[i].x, 0, guardian.ghosts[i].z);
                rlRotatef(guardian.ghostAngles[i] * RAD2DEG, 0, 1, 0);
                rlRotatef(guardian.ghostTiltsZ[i] * RAD2DEG, 0, 0, 1);
                rlRotatef(guardian.ghostTiltsX[i] * RAD2DEG, 1, 0, 0);
                
                // Single faint light shell per echo
                DrawCylinder({0,0,0}, 0.85f, 0.85f, 3.0f, 8, Fade(ghostCol, 0.6f));
                DrawSphere({0, 3.5f, 0}, 0.9f, ghostCol);
                
                rlPopMatrix();
            }
            EndBlendMode();
        }

        // Radiant Censer Ribbon (Weapon Trail)
        if (!guardian.weaponTrail.empty()) {
            BeginBlendMode(BLEND_ADDITIVE);
            for (size_t i = 0; i < (size_t)guardian.weaponTrail.size() - 1; i++) {
                float t = (float)i / guardian.weaponTrail.size();
                float alpha = 0.7f * (1.0f - t);
                Color trailCol = ColorLerp(GOLD, WHITE, 1.0f - t);
                DrawLine3D(guardian.weaponTrail[i], guardian.weaponTrail[i+1], Fade(trailCol, alpha));
                DrawLine3D(Vector3Add(guardian.weaponTrail[i], {0, 0.1f, 0}), Vector3Add(guardian.weaponTrail[i+1], {0, 0.1f, 0}), Fade(trailCol, alpha * 0.5f));
            }
            EndBlendMode();
        }

        // Guardian Radiance
        rlPushMatrix();
        rlTranslatef(guardian.pos.x, 0, guardian.pos.z);
        rlRotatef(guardian.facingAngle * RAD2DEG, 0, 1, 0);
        // Apply Physics Leaning
        rlRotatef(guardian.tiltZ * RAD2DEG, 0, 0, 1);
        rlRotatef(guardian.tiltX * RAD2DEG, 1, 0, 0);
        
        // Body
        DrawDivineMesh(cylinderMesh, {0, 1.6f, 0}, {1.0f, 1.6f, 1.0f}, COL_GRACE, 0.0f);
        
        // Shoulders
        rlPushMatrix();
        rlTranslatef(0, 3.0f, 0);
        rlRotatef(90, 0, 0, 1);
        DrawDivineMesh(cylinderMesh, {0,0,0}, {0.4f, 1.5f, 0.4f}, COL_GRACE, 0.0f);
        rlPopMatrix();
        
        // Cape (Flowing behind)
        float capeWave = sinf(GetTime() * 10.0f) * 0.1f;
        rlPushMatrix();
        rlTranslatef(0, 3.0f, -0.5f);
        rlRotatef(15.0f + guardian.tiltX * 20.0f + capeWave * 50.0f, 1, 0, 0);
        DrawCube({0, -2.0f, -0.5f}, 2.8f, 4.0f, 0.1f, Fade(COL_GRACE, 0.4f));
        rlPopMatrix();

        // Head/Halo
        DrawDivineMesh(sphereMesh, {0,3.5f,0}, {1.1f, 1.1f, 1.1f}, COL_GRACE, 0.0f);
        DrawSphere({0,3.5f,0}, 1.8f * guardian.haloScale, Fade(COL_GRACE, 0.15f)); 
        rlPopMatrix();
        
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
            for (int i=0; i<2; i++) {
                float side = (i == 0) ? 1.0f : -1.0f;
                Vector3 p = {side * 1.5f, 0.8f, 0};
                DrawSphere(Vector3Add(h.pos, p), 0.6f, Fade(PINK, 0.6f));
            }
        }

        // Blessing Item Visuals
        for (const auto& b : blessingItems) {
            float p = 0.2f * sinf(GetTime() * 6.0f);
            DrawSphere(b.pos, 1.2f + p, GOLD);
            DrawSphereWires(b.pos, 1.8f + p, 8, 8, WHITE);
            const char* bName = (b.type == BLESS_CHARITY) ? "Aura of Charity" : (b.type == BLESS_PURITY) ? "Veil of Purity" : "Spirit of Fortitude";
            DrawText3D(bName, Vector3Add(b.pos, {0, 3.0f, 0}), 15, WHITE);
        }

        // Saintly Echoes (F16)
        for (const auto& e : saintlyEchoes) {
            float p = e.life / 6.0f;
            float pulse = 1.0f + 0.2f * sinf(GetTime() * 8.0f);
            DrawCircle3D(e.pos, e.scale * pulse * p, {0,1,0}, 90.0f, Fade(GOLD, 0.3f * p));
            DrawCircle3D(e.pos, e.scale * 0.5f * pulse * p, {0,1,0}, 90.0f, Fade(WHITE, 0.2f * p));
        }

        // Mansion Hazards Visuals
        if (vigilCount >= 11 && vigilCount <= 15) { // Whirlpool ripples
            for (int i=0; i<3; i++) {
                float r = fmodf(GetTime() * 20.0f + i * 20.0f, 60.0f);
                DrawCircle3D({0,0,0}, r, {0,1,0}, 90.0f, Fade(currentLineCol, 0.1f * (1.0f - r/60.0f)));
            }
        }

        // Vices
        for (const auto& v : vices) {
            Color vCol = (v.stunned) ? GRAY : (v.type == CARDINAL_SIN) ? COL_BOSS : currentViceCol;
            if (v.redeemed) vCol = GOLD;
            
            Vector3 drawPos = v.pos;
            if (v.type == CARDINAL_SIN) {
                // Boss Visual: Massive Fractured Core
                if (v.state == VICE_TELEGRAPH) {
                    drawPos.x += (float)GetRandomValue(-10, 10) * 0.05f;
                    drawPos.z += (float)GetRandomValue(-10, 10) * 0.05f;
                    vCol = ColorLerp(vCol, RED, 0.5f + 0.5f * sinf(GetTime() * 20.0f));
                } else if (v.state == VICE_RECOVER) {
                    vCol = Fade(GRAY, 0.6f);
                }

                DrawDivineMesh(sphereMesh, drawPos, {v.scale, v.scale, v.scale}, vCol, 0.6f);
                
                // Orbiting Great Shards
                for (int i=0; i<5; i++) {
                    float t = GetTime() * 2.0f + i * 2.0f;
                    Vector3 offset = {sinf(t)*v.scale*1.5f, cosf(t*0.7f)*v.scale*1.5f, cosf(t)*v.scale*1.5f};
                    DrawDivineMesh(polyMesh, Vector3Add(drawPos, offset), {v.scale*0.4f, v.scale*0.4f, v.scale*0.4f}, vCol, 0.4f);
                }
                
                DrawSphere(drawPos, v.scale * 2.2f, Fade(vCol, 0.15f)); 
            } else {
                // Regular Vice: Floating Shards
                DrawDivineMesh(sphereMesh, v.pos, {v.scale * 0.8f, v.scale * 0.8f, v.scale * 0.8f}, vCol, 0.4f);
                
                // 3 Orbiting Shards
                for (int i=0; i<3; i++) {
                    float t = GetTime() * 3.0f + i * (2*PI/3);
                    Vector3 offset = {sinf(t)*v.scale, sinf(t*2.0f)*v.scale*0.5f, cosf(t)*v.scale};
                    DrawDivineMesh(polyMesh, Vector3Add(v.pos, offset), {v.scale*0.3f, v.scale*0.3f, v.scale*0.3f}, vCol, 0.4f);
                }
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
                // Radiant Orb of Truth (Lit)
                DrawDivineMesh(sphereMesh, t.pos, {0.8f, 0.8f, 0.8f}, WHITE, 0.0f);
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
        
        // Motes (Batching Candidate - Keep simple for now but cull)
        for (const auto& m : motes) {
            if (m.size < 0.05f) continue; // Culling
            Color mCol = Fade(m.color, m.life);
            if (m.type == MOTE_DOVE) {
                // Simple Dove Shape
                DrawCube(m.pos, m.size, m.size*0.2f, m.size*0.5f, mCol);
                DrawCube(Vector3Add(m.pos, {0,0,m.size*0.3f}), m.size*0.2f, m.size*0.2f, m.size*0.8f, mCol); // Wings
            } else {
                DrawCube(m.pos, m.size, m.size, m.size, mCol);
            }
        }
        
        // Cursor
        Vector3 aimPos = {0};
        Ray ray = GetMouseRay(GetMousePosition(), camera);
        float t = -ray.position.y / ray.direction.y;
        aimPos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
        DrawCircle3D(aimPos, 1.2f, {1, 0, 0}, 90.0f, Fade(WHITE, 0.5f));
        DrawCircle3D(aimPos, 0.6f, {1, 0, 0}, 90.0f, Fade(WHITE, 0.3f));

    EndMode3D();
    EndTextureMode();
    
    // Post-Process Pass (Screen Space)
    BeginDrawing();
    ClearBackground(BLACK);
    
    BeginShaderMode(postShader);
    // Chromatic Aberration tied to screenShake
    float aberration = screenShake * 1.5f;
    SetShaderValue(postShader, GetShaderLocation(postShader, "aberrationStrength"), &aberration, SHADER_UNIFORM_FLOAT);
    
    // Draw the scene texture, flipping it vertically
    DrawTextureRec(target.texture, (Rectangle){ 0, 0, (float)target.texture.width, (float)-target.texture.height }, (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    
    // UI (On top of post-process)
    DrawText("LUMEN FIDEI", 20, 20, 40, COL_GRACE);
    DrawText(TextFormat("VIGIL %d", vigilCount), 20, 60, 30, WHITE);
    
    // The Sacred Heart (Grace/Love)
    float pulseSpeed = 1.0f + (1.0f - ((float)guardian.grace.load() / guardian.maxGrace)) * 4.0f;
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
    DrawRectangle(140, SCREEN_HEIGHT - 65, 200 * ((float)guardian.grace.load() / guardian.maxGrace), 20, PINK);
    DrawText("LOVE", 150, SCREEN_HEIGHT - 63, 16, BLACK);
    
    // The Son (Mercy)
    DrawRectangle(140, SCREEN_HEIGHT - 90, 180, 15, Fade(BLACK, 0.5f));
    DrawRectangle(140, SCREEN_HEIGHT - 90, 180 * ((float)guardian.spirit.load() / guardian.maxSpirit), 15, COL_SPIRIT);
    DrawText("MERCY", 150, SCREEN_HEIGHT - 90, 12, BLACK);

    // The Holy Spirit (Praise)
    float fervorPct = (float)guardian.fervor.load() / guardian.maxFervor;
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
    
    // Debug Overlay
    if (debugMode) {
        DrawRectangle(SCREEN_WIDTH - 320, 100, 300, 240, Fade(BLACK, 0.7f));
        DrawText("--- GUARDIAN DEBUG ---", SCREEN_WIDTH - 300, 110, 20, GOLD);
        DrawText(TextFormat("God Mode: %s", godMode ? "ON" : "OFF"), SCREEN_WIDTH - 300, 140, 18, godMode ? GREEN : GRAY);
        DrawText(TextFormat("Current Vigil: %d", vigilCount), SCREEN_WIDTH - 300, 160, 18, WHITE);
        DrawText(TextFormat("FPS: %d", GetFPS()), SCREEN_WIDTH - 300, 180, 18, LIME);
        
        DrawText("F1-F4: Jump Vigils", SCREEN_WIDTH - 300, 210, 16, LIGHTGRAY);
        DrawText("G: God Mode | K: Redeem All", SCREEN_WIDTH - 300, 230, 16, LIGHTGRAY);
        DrawText("M: +1000 Joy | P: Max Praise", SCREEN_WIDTH - 300, 250, 16, LIGHTGRAY);
        DrawText("TAB: Close Debug", SCREEN_WIDTH - 300, 280, 16, GOLD);
    }
    
    // Mansion Title Overlay
    if (mansionTitleTimer > 0) {
        float alpha = (mansionTitleTimer > 1.0f) ? 1.0f : mansionTitleTimer;
        int fontSize = 60;
        int textW = MeasureText(currentMansionName.c_str(), fontSize);
        DrawText(currentMansionName.c_str(), SCREEN_WIDTH/2 - textW/2, SCREEN_HEIGHT/2 - 100, fontSize, Fade(WHITE, alpha));
        DrawRectangle(SCREEN_WIDTH/2 - textW/2, SCREEN_HEIGHT/2 - 30, textW, 2, Fade(GOLD, alpha));
    }

    // Divine Leveling HUD (F20)
    DrawText(TextFormat("SANCTITY LEVEL %d", guardian.level), 20, 140, 24, GOLD);
    float xpProgress = (float)guardian.merit.load() / guardian.nextLevelJoy;
    DrawRectangle(20, 170, 200, 6, Fade(BLACK, 0.5f));
    DrawRectangle(20, 170, 200 * xpProgress, 6, GOLD);

    if (guardian.spiritPoints > 0) {
        int panelW = 750, panelH = 220;
        int pX = 20, pY = 200;
        
        DrawRectangle(pX, pY, panelW, panelH, Fade(BLACK, 0.75f));
        DrawRectangleLines(pX, pY, panelW, panelH, Fade(GOLD, 0.9f));
        
        DrawText(TextFormat("DIVINE GIFTS (%d SPIRIT POINTS)", guardian.spiritPoints), pX + 20, pY + 15, 24, GOLD);
        
        // Column 1
        DrawText("1. PRUDENCE (+18% Vision)", pX + 30, pY + 60, 18, SKYBLUE);
        DrawText("   Widen your divine defense window.", pX + 30, pY + 80, 14, LIGHTGRAY);
        
        DrawText("2. JUSTICE (+40% Speed)", pX + 30, pY + 110, 18, GOLD);
        DrawText("   Overwhelming force for Truth.", pX + 30, pY + 130, 14, LIGHTGRAY);
        
        DrawText("3. TEMPERANCE (+50 Love)", pX + 30, pY + 160, 18, PINK);
        DrawText("   Deepen your heart's capacity.", pX + 30, pY + 180, 14, LIGHTGRAY);

        // Column 2
        DrawText("4. FORTITUDE (+20% Acceleration)", pX + 380, pY + 60, 18, WHITE);
        DrawText("   Move faster, dash for less Spirit.", pX + 380, pY + 80, 14, LIGHTGRAY);
        
        DrawText("5. WISDOM (+25% Joy Gain)", pX + 380, pY + 110, 18, LIME);
        DrawText("   Grow faster from every redemption.", pX + 380, pY + 130, 14, LIGHTGRAY);
        
        DrawText("6. COUNSEL (+30% Praise Gain)", pX + 380, pY + 160, 18, ORANGE);
        DrawText("   The Holy Spirit's fire burns brighter.", pX + 380, pY + 180, 14, LIGHTGRAY);
    }

    // Active Blessings List
    if (guardian.hasCharity || guardian.hasPurity || guardian.hasFortitude) {
        DrawText("BLESSINGS:", 20, (guardian.spiritPoints > 0) ? 460 : 200, 20, GOLD);
        int offset = (guardian.spiritPoints > 0) ? 490 : 230;
        if (guardian.hasCharity) { DrawText("• Aura of Charity", 30, offset, 18, LIGHTGRAY); offset += 25; }
        if (guardian.hasPurity) { DrawText("• Veil of Purity", 30, offset, 18, LIGHTGRAY); offset += 25; }
        if (guardian.hasFortitude) { DrawText("• Spirit of Fortitude", 30, offset, 18, LIGHTGRAY); offset += 25; }
    }

    // Instructions
    DrawText("WASD Move • L-Click Swing • R-Click Shield • SPACE Cross • F Canticle • R PRAY", 20, 100, 20, Fade(WHITE, 0.5f));
    
    // Combo Counter
    if (guardian.comboStep > 0) {
        const char* stepName = (guardian.comboStep == 3) ? "CHARITY" : (guardian.comboStep == 2) ? "MERCY" : "HUMILITY";
        Color comboCol = (guardian.comboStep == 3) ? GOLD : (guardian.comboStep == 2) ? WHITE : COL_GRACE;
        DrawText(stepName, SCREEN_WIDTH/2 - MeasureText(stepName, 50)/2, SCREEN_HEIGHT - 180, 50, Fade(comboCol, 0.8f));
    }
    
    // Game State Overlays
    if (currentState == STATE_TITLE) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.6f));
        DrawText("LUMEN FIDEI", SCREEN_WIDTH/2 - MeasureText("LUMEN FIDEI", 80)/2, SCREEN_HEIGHT/2 - 80, 80, GOLD);
        DrawText("The Joy of the Trinity", SCREEN_WIDTH/2 - MeasureText("The Joy of the Trinity", 30)/2, SCREEN_HEIGHT/2 + 10, 30, WHITE);
        
        float pulse = 0.5f + 0.5f * sinf(GetTime() * 4.0f);
        DrawText("PRESS ENTER TO BEGIN", SCREEN_WIDTH/2 - MeasureText("PRESS ENTER TO BEGIN", 20)/2, SCREEN_HEIGHT - 100, 20, Fade(WHITE, pulse));
    }
    else if (currentState == STATE_PAUSE) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.4f));
        DrawText("- PAUSED -", SCREEN_WIDTH/2 - MeasureText("- PAUSED -", 40)/2, SCREEN_HEIGHT/2 - 20, 40, WHITE);
        
        // Show Controls in Pause
        int cX = SCREEN_WIDTH/2 - 150, cY = SCREEN_HEIGHT/2 + 40;
        DrawText("Controls:", cX, cY, 20, GOLD);
        DrawText("WASD: Move (Drift)", cX, cY + 30, 18, LIGHTGRAY);
        DrawText("L-Click: Censer Swing", cX, cY + 55, 18, LIGHTGRAY);
        DrawText("R-Click: Shield of Faith", cX, cY + 80, 18, LIGHTGRAY);
        DrawText("Space: Sign of the Cross", cX, cY + 105, 18, LIGHTGRAY);
        DrawText("E: Pax (Freeze Time)", cX, cY + 130, 18, LIGHTGRAY);
        DrawText("F: Canticle of Joy", cX, cY + 155, 18, LIGHTGRAY);
        DrawText("R (Hold): Prayer", cX, cY + 180, 18, LIGHTGRAY);
    }
    else if (currentState == STATE_VICTORY) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
        DrawText("ASCENSION ACHIEVED", SCREEN_WIDTH/2 - MeasureText("ASCENSION ACHIEVED", 60)/2, SCREEN_HEIGHT/2 - 60, 60, GOLD);
        DrawText(TextFormat("Final Joy: %d", guardian.merit.load()), SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2 + 20, 30, BLACK);
        DrawText("Thank you for playing.", SCREEN_WIDTH/2 - MeasureText("Thank you for playing.", 20)/2, SCREEN_HEIGHT - 100, 20, GRAY);
    }
    else if (currentState == STATE_DESOLATION) {
        DrawRectangle(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.8f));
        DrawText("DESOLATION", SCREEN_WIDTH/2 - MeasureText("DESOLATION", 60)/2, SCREEN_HEIGHT/2 - 50, 60, RED);
        DrawText("The light has faded...", SCREEN_WIDTH/2 - MeasureText("The light has faded...", 30)/2, SCREEN_HEIGHT/2 + 20, 30, GRAY);
        DrawText("Press R to Rekindle", SCREEN_WIDTH/2 - MeasureText("Press R to Rekindle", 20)/2, SCREEN_HEIGHT/2 + 60, 20, WHITE);
    }
    
    EndDrawing();
}

// ======================================================================
// Main
// ======================================================================
int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "LumenFidei – The Shield of Saints");
    SetTargetFPS(60);
    SetExitKey(0); // Disable default ESC key closing the game
    // HideCursor(); // Use custom cursor
    
    InitLumenFidei();
    
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    g_pool = new ThreadPool(numThreads);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        
        // Hit Stop Logic (Juice)
        if (hitStop > 0) {
            hitStop -= dt;
            // Draw frozen frame
            DrawFrame();
            continue;
        }

        // Global State Management
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (currentState == STATE_VIGIL) currentState = STATE_PAUSE;
            else if (currentState == STATE_PAUSE) currentState = STATE_VIGIL;
        }

        if (currentState == STATE_TITLE) {
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                StartVigil(true);
                currentState = STATE_VIGIL;
            }
            // Animate title background
            UpdateGuardian(dt); // Keep guardian moving for title visual
        } else if (currentState == STATE_VICTORY) {
            if (IsKeyPressed(KEY_ENTER)) {
                StartVigil(true);
                currentState = STATE_TITLE;
            }
        } else if (currentState == STATE_DESOLATION) {
            if (IsKeyPressed(KEY_R)) {
                StartVigil(true);
                currentState = STATE_VIGIL;
            }
        } else if (currentState == STATE_PAUSE) {
            // No update, just draw
        } else {
            UpdateFrame(dt);
        }
        
        DrawFrame();
    }
    
    delete g_pool;
    CloseWindow();
    return 0;
}
