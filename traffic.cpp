#include "raylib.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>

//constants
static const int   SCREEN_W  = 1280;
static const int   SCREEN_H  = 720;
static const int   SIDEBAR_W = 280;
static const int   SIM_W     = SCREEN_W - SIDEBAR_W;

static const float CENTER_X  = SIM_W  / 2.f;
static const float CENTER_Y  = SCREEN_H / 2.f;

static const float LANE_W    = 22.f;
static const float ROAD_HALF = LANE_W * 2.f;
static const float MIN_GAP   = 6.f;
static const float STOP_OFFSET = 5.f;
static const int   MAX_VEHICLES = 60;
static const float TWO_PI    = 6.28318f;

enum Direction { NORTH = 0, SOUTH, EAST, WEST };
enum LightState { LT_GREEN = 0, LT_YELLOW, LT_RED };
enum VehicleType { CAR = 0, AMBULANCE, FIRETRUCK };

static const Color CAR_COLORS[] = {
    {52,101,220,255}, {34,177,76,255},  {163,73,4,255},   {153,217,234,255},
    {112,48,160,255}, {255,127,39,255}, {0,162,232,255},   {185,122,87,255},
    {200,191,231,255},{239,228,176,255}
};
static const int NUM_CAR_COLORS = 10;

static float laneCenterFor(Direction d) {
    switch (d) {
        case NORTH: return CENTER_X + LANE_W * 0.5f;
        case SOUTH: return CENTER_X - LANE_W * 0.5f;
        case EAST:  return CENTER_Y + LANE_W * 0.5f;
        default:    return CENTER_Y - LANE_W * 0.5f;
    }
}

class LaneQueue {
public:
    static const int CAPACITY = MAX_VEHICLES;

    LaneQueue() : head(0), count(0) {}

    void clear() { head = 0; count = 0; }
    bool isFull()  const { return count == CAPACITY; }
    int  size()    const { return count; }

    void push(int vehicleIndex) {
        if (!isFull()) {
            ids[(head + count) % CAPACITY] = vehicleIndex;
            count++;
        }
    }

    void sortByPosition(const float* positions, bool ascending) {
        for (int i = 0; i < count - 1; i++) {
            for (int j = 0; j < count - i - 1; j++) {
                int slotA = (head + j)     % CAPACITY;
                int slotB = (head + j + 1) % CAPACITY;
                bool shouldSwap = ascending
                    ? positions[ids[slotA]] > positions[ids[slotB]]
                    : positions[ids[slotA]] < positions[ids[slotB]];
                if (shouldSwap) {
                    int tmp = ids[slotA];
                    ids[slotA] = ids[slotB];
                    ids[slotB] = tmp;
                }
            }
        }
    }

    int at(int k) const { return ids[(head + k) % CAPACITY]; }

private:
    int ids[CAPACITY];
    int head;
    int count;
};

class EmergencyPQ {
public:
    struct Entry {
        int   direction;
        int   priorityRank;
        int   sequence;
        float registeredAt;
    };

    EmergencyPQ() : heapSize(0) {}

    void reset() { heapSize = 0; }
    bool isEmpty()           const { return heapSize == 0; }
    bool containsDirection(int dir) const {
        for (int i = 0; i < heapSize; i++)
            if (heap[i].direction == dir) return true;
        return false;
    }

    void enqueue(int dir, VehicleType type, int seq, float simTime) {
        if (heapSize >= 8 || containsDirection(dir)) return;
        int rank = (type == AMBULANCE) ? 0 : 1;
        heap[heapSize++] = { dir, rank, seq, simTime };
        siftUp(heapSize - 1);
    }

    void dequeue(int dir) {
        for (int i = 0; i < heapSize; i++) {
            if (heap[i].direction != dir) continue;
            heap[i] = heap[--heapSize];
            siftDown(i);
            siftUp(i);
            return;
        }
    }

    int pruneOrphans() {
        return 0;
    }

    int  topDirection() const { return (heapSize > 0) ? heap[0].direction : -1; }

    int         size()              const { return heapSize; }
    const Entry& entryAt(int i)    const { return heap[i]; }

private:
    Entry heap[8];
    int   heapSize;

    bool higherPriority(int a, int b) const {
        if (heap[a].priorityRank != heap[b].priorityRank)
            return heap[a].priorityRank < heap[b].priorityRank;
        return heap[a].sequence < heap[b].sequence;
    }
    void swapEntries(int a, int b) {
        Entry tmp = heap[a]; heap[a] = heap[b]; heap[b] = tmp;
    }
    void siftUp(int i) {
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (higherPriority(i, parent)) { swapEntries(i, parent); i = parent; }
            else break;
        }
    }
    void siftDown(int i) {
        while (true) {
            int left = 2*i+1, right = 2*i+2, best = i;
            if (left  < heapSize && higherPriority(left,  best)) best = left;
            if (right < heapSize && higherPriority(right, best)) best = right;
            if (best == i) break;
            swapEntries(i, best); i = best;
        }
    }
};

class TrafficLight {
public:
    LightState state;
    bool       override;

    TrafficLight() : state(LT_RED), override(false) {}

    bool isGreen() const { return state == LT_GREEN; }

    void draw(float px, float py, bool hz) const {
        float bw = hz ? 50.f : 16.f;
        float bh = hz ? 16.f : 50.f;
        DrawRectangle((int)(px - bw/2), (int)(py - bh/2), (int)bw, (int)bh, DARKGRAY);

        Color redDot    = (state != LT_GREEN)  ? RED    : Color{60,0,0,255};
        Color yellowDot = (state == LT_YELLOW) ? YELLOW : Color{60,60,0,255};
        Color greenDot  = (state == LT_GREEN)  ? GREEN  : Color{0,60,0,255};

        if (hz) {
            DrawCircle((int)(px-16), (int)py, 5, redDot);
            DrawCircle((int)px,      (int)py, 5, yellowDot);
            DrawCircle((int)(px+16), (int)py, 5, greenDot);
        } else {
            DrawCircle((int)px, (int)(py-16), 5, redDot);
            DrawCircle((int)px, (int)py,      5, yellowDot);
            DrawCircle((int)px, (int)(py+16), 5, greenDot);
        }

        if (override)
            DrawRectangleLinesEx({px-bw/2, py-bh/2, bw, bh}, 2, ORANGE);
    }
};

class IntersectionManager {
public:
    TrafficLight lights[4];
    int  queueCount[4];
    bool boxOccupiedNS;
    bool boxOccupiedEW;

    IntersectionManager()
        : queueCount{0,0,0,0}
        , boxOccupiedNS(false), boxOccupiedEW(false)
        , greenPair(0), cycleTimer(0.f)
        , sequenceCounter(0), activeEmDir(-1)
    {}

    void initialize() {
        for (int i = 0; i < 4; i++) { lights[i].state = LT_RED; lights[i].override = false; }
        lights[NORTH].state = LT_GREEN;
        lights[SOUTH].state = LT_GREEN;
        greenPair   = 0;
        cycleTimer  = 0.f;
        emergencyQueue.reset();
        sequenceCounter = 0;
        activeEmDir     = -1;
        boxOccupiedNS = boxOccupiedEW = false;
    }

    void registerEmergency(int dir, VehicleType type, float simTime) {
        emergencyQueue.enqueue(dir, type, sequenceCounter++, simTime);
        rebuildEmergencyLights();
    }

    void releaseEmergency(int dir) {
        emergencyQueue.dequeue(dir);
        rebuildEmergencyLights();
    }

    void pruneOrphanEntries(int liveDirectionMask) {
        bool changed = false;
        for (int d = 0; d < 4; d++) {
            bool inQueue = emergencyQueue.containsDirection(d);
            bool hasLiveVehicle = (liveDirectionMask >> d) & 1;
            if (inQueue && !hasLiveVehicle) {
                emergencyQueue.dequeue(d);
                changed = true;
            }
        }
        if (changed) rebuildEmergencyLights();
    }

    bool anyEmergencyActive() const { return !emergencyQueue.isEmpty(); }

    bool canGo(int dir) const {
        if (!lights[dir].isGreen()) return false;
        bool isNS = (dir == NORTH || dir == SOUTH);
        if (isNS  && boxOccupiedEW) return false;
        if (!isNS && boxOccupiedNS) return false;
        return true;
    }

    bool canEmergencyGo(int dir) const {
        bool isNS = (dir == NORTH || dir == SOUTH);
        if (isNS  && boxOccupiedEW) return false;
        if (!isNS && boxOccupiedNS) return false;
        return true;
    }

    void update(float dt) {
        if (anyEmergencyActive()) {
            applyEmergencyLights();
            return;
        }

        for (int i = 0; i < 4; i++) lights[i].override = false;

        float greenDuration = computeAdaptiveGreen();
        cycleTimer += dt;

        if      (cycleTimer < greenDuration)               { setPair(greenPair, LT_GREEN);  setPair(1-greenPair, LT_RED); }
        else if (cycleTimer < greenDuration + YELLOW_TIME) { setPair(greenPair, LT_YELLOW); setPair(1-greenPair, LT_RED); }
        else    { greenPair = 1 - greenPair; cycleTimer = 0.f; }
    }

    void drawLights() const {
        float offset = ROAD_HALF + 18.f;
        lights[NORTH].draw(laneCenterFor(NORTH), CENTER_Y - offset, false);
        lights[SOUTH].draw(laneCenterFor(SOUTH), CENTER_Y + offset, false);
        lights[EAST ].draw(CENTER_X + offset, laneCenterFor(EAST),  true);
        lights[WEST ].draw(CENTER_X - offset, laneCenterFor(WEST),  true);
    }

    int         getTopEmDir()  const { return emergencyQueue.topDirection(); }
    const EmergencyPQ& getEmPQ() const { return emergencyQueue; }

private:
    static constexpr float YELLOW_TIME = 1.4f;

    int   greenPair;
    float cycleTimer;

    EmergencyPQ emergencyQueue;
    int         sequenceCounter;
    int         activeEmDir;

    float computeAdaptiveGreen() const {
        int greenQ = (greenPair == 0)
            ? queueCount[NORTH] + queueCount[SOUTH]
            : queueCount[EAST]  + queueCount[WEST];
        int redQ = (greenPair == 0)
            ? queueCount[EAST]  + queueCount[WEST]
            : queueCount[NORTH] + queueCount[SOUTH];
        int total = greenQ + redQ;
        if (total == 0) return 5.f;
        return 3.f + ((float)greenQ / (float)total) * 9.f;
    }

    void rebuildEmergencyLights() {
        activeEmDir = emergencyQueue.topDirection();
        applyEmergencyLights();
    }

    void applyEmergencyLights() {
        for (int i = 0; i < 4; i++) {
            if (activeEmDir < 0) { lights[i].override = false; continue; }
            lights[i].override = true;
            lights[i].state    = (i == activeEmDir) ? LT_GREEN : LT_RED;
        }
    }

    void setPair(int pair, LightState s) {
        Direction a = (pair == 0) ? NORTH : EAST;
        Direction b = (pair == 0) ? SOUTH : WEST;
        if (!lights[a].override) lights[a].state = s;
        if (!lights[b].override) lights[b].state = s;
    }
};


struct VehicleTextures {
    Texture2D car       = {};
    Texture2D ambulance = {};
    Texture2D firetruck = {};
    bool      loaded    = false;
};
static VehicleTextures g_vehicleTextures;

class Vehicle {
public:
    bool        active;
    VehicleType type;
    Direction   dir;
    bool        isEmergency;

    float x, y;
    float speed;
    float baseSpeed;
    float width, height;   // logical extents along travel axis
    Color color;

    bool  waiting;
    float waitTime;

    bool  inBox;
    bool  passed;
    bool  exitedBox;

    float flashTimer;
    bool  flashOn;

    bool  emRegistered;
    bool  emReleased;

    float lateral;
    float targetLateral;

    bool yiedling;

    Vehicle()
        : active(false), type(CAR), dir(NORTH), isEmergency(false)
        , x(0), y(0), speed(0), baseSpeed(0), width(0), height(0)
        , color(WHITE), waiting(false), waitTime(0)
        , inBox(false), passed(false), exitedBox(false)
        , flashTimer(0), flashOn(false)
        , emRegistered(false), emReleased(false)
        , lateral(0.f), targetLateral(0.f), yiedling(false)
    {}

    void spawn(VehicleType t, Direction d, int colorIndex,
               const Vehicle* allVehicles, int vehicleCount)
    {
        active       = true;
        type         = t;
        dir          = d;
        isEmergency  = (t == AMBULANCE || t == FIRETRUCK);
        waiting      = false;
        waitTime     = 0.f;
        inBox        = false;
        passed       = false;
        exitedBox    = false;
        flashTimer   = 0.f;
        flashOn      = false;
        emRegistered = false;
        emReleased   = false;
        lateral      = 0.f;
        targetLateral= 0.f;
        yiedling      = false;

        baseSpeed = (t == AMBULANCE) ? 160.f
                  : (t == FIRETRUCK) ? 140.f
                  : 75.f + (float)(rand() % 35);
        speed = baseSpeed;

        color = (t == AMBULANCE) ? WHITE
              : (t == FIRETRUCK) ? Color{220,20,20,255}
              : CAR_COLORS[colorIndex % NUM_CAR_COLORS];

        bool horizontal = (d == EAST || d == WEST);

        // ---------------------------------------------------------------
        // FIX: Larger vehicles with proper aspect ratios.
        // crossAxis  = girth of the vehicle (fits inside the lane)
        // lengthAxis = bumper-to-bumper length (elongated along travel)
        //
        // width/height are stored as the LOGICAL box in screen space:
        //   horizontal travel ? width=long side, height=short side
        //   vertical travel   ? width=short side, height=long side
        // DrawTexturePro then uses a corrected dst rect (see draw()) so the
        // sprite is never squashed after its 90�/270� rotation.
        // ---------------------------------------------------------------
        float crossAxis, lengthAxis;
        switch (t) {
            case AMBULANCE:
                crossAxis  = 28.f;
                lengthAxis = crossAxis * (237.f / 139.f);  // � 47.8
                break;
            case FIRETRUCK:
                crossAxis  = 27.f;
                lengthAxis = crossAxis * (252.f / 128.f);  // � 53.2
                break;
            default:  // CAR
                crossAxis  = 26.f;
                lengthAxis = crossAxis * (245.f / 153.f);  // � 41.6
                break;
        }

        // Store logical extents; draw() will pass the correct dst to raylib.
        width  = horizontal ? lengthAxis : crossAxis;
        height = horizontal ? crossAxis  : lengthAxis;

        float laneCenter = laneCenterFor(d);
        float spawnX = laneCenter, spawnY = laneCenter;
        switch (d) {
            case NORTH: spawnX = laneCenter; spawnY = (float)SCREEN_H + height + 4; break;
            case SOUTH: spawnX = laneCenter; spawnY = -height - 4;                  break;
            case EAST:  spawnY = laneCenter; spawnX = -width  - 4;                  break;
            case WEST:  spawnY = laneCenter; spawnX = (float)SIM_W + width + 4;     break;
        }

        for (int i = 0; i < vehicleCount; i++) {
            const Vehicle& v = allVehicles[i];
            if (!v.active || v.dir != d) continue;
            float gap = (horizontal ? width : height) + MIN_GAP + 4;
            switch (d) {
                case NORTH: { float b = v.y + v.height/2 + gap; if (b > spawnY) spawnY = b; break; }
                case SOUTH: { float b = v.y - v.height/2 - gap; if (b < spawnY) spawnY = b; break; }
                case EAST:  { float b = v.x - v.width /2 - gap; if (b < spawnX) spawnX = b; break; }
                case WEST:  { float b = v.x + v.width /2 + gap; if (b > spawnX) spawnX = b; break; }
            }
        }
        x = spawnX;
        y = spawnY;
    }

    float frontEdge() const {
        switch (dir) {
            case NORTH: return y - height/2;
            case SOUTH: return y + height/2;
            case EAST:  return x + width/2;
            default:    return x - width/2;
        }
    }

    float backEdge() const {
        switch (dir) {
            case NORTH: return y + height/2;
            case SOUTH: return y - height/2;
            case EAST:  return x - width/2;
            default:    return x + width/2;
        }
    }

    float stopLinePosition() const {
        switch (dir) {
            case NORTH: return CENTER_Y + ROAD_HALF + STOP_OFFSET;
            case SOUTH: return CENTER_Y - ROAD_HALF - STOP_OFFSET;
            case EAST:  return CENTER_X - ROAD_HALF - STOP_OFFSET;
            default:    return CENTER_X + ROAD_HALF + STOP_OFFSET;
        }
    }

    bool isInsideBox() const {
        return x > CENTER_X - ROAD_HALF && x < CENTER_X + ROAD_HALF
            && y > CENTER_Y - ROAD_HALF && y < CENTER_Y + ROAD_HALF;
    }

    void update(float dt, IntersectionManager& im,
                const Vehicle* allVehicles, int vehicleCount,
                const LaneQueue& myLaneQueue)
    {
        if (!active) return;

        if (isEmergency) {
            flashTimer += dt;
            if (flashTimer >= 0.22f) { flashTimer = 0.f; flashOn = !flashOn; }
        }

        if (isEmergency && !emRegistered && !passed) {
            im.registerEmergency((int)dir, type, (float)GetTime());
            emRegistered = true;
        }

        if (!isEmergency)
        {
            bool emergencyNearby = false;

            for (int i = 0; i < vehicleCount; i++)
            {
                const Vehicle& v = allVehicles[i];
                if (!v.active || !v.isEmergency) continue;
                if (v.dir != dir) continue;

                float dist = (dir == NORTH || dir == SOUTH)
                    ? fabsf(y - v.y)
                    : fabsf(x - v.x);

                if (dist < 180.f)
                {
                    emergencyNearby = true;
                    break;
                }
            }

            float shift = (dir == NORTH || dir == EAST) ? LANE_W : -LANE_W;

            if (emergencyNearby)
                targetLateral = shift;
            else
                targetLateral = 0.f;
        }
        const float LATERAL_SPEED = 120.f;
        if      (lateral < targetLateral) lateral = fminf(lateral + LATERAL_SPEED * dt, targetLateral);
        else if (lateral > targetLateral) lateral = fmaxf(lateral - LATERAL_SPEED * dt, targetLateral);

        float allowedStop = computeAllowedStopPosition(im, allVehicles, vehicleCount, myLaneQueue);

        float myFront     = frontEdge();
        float distToStop  = 0.f;
        bool  mustStop    = false;
        switch (dir) {
            case NORTH: distToStop = myFront - allowedStop; mustStop = (myFront <= allowedStop + 1.f); break;
            case SOUTH: distToStop = allowedStop - myFront; mustStop = (myFront >= allowedStop - 1.f); break;
            case EAST:  distToStop = allowedStop - myFront; mustStop = (myFront >= allowedStop - 1.f); break;
            case WEST:  distToStop = myFront - allowedStop; mustStop = (myFront <= allowedStop + 1.f); break;
        }

        if (mustStop) {
            waiting = true;
            speed   = 0.f;
            waitTime += dt;
        } else {
            waiting = false;
            const float BRAKE_ZONE = 90.f;
            if (!passed && distToStop < BRAKE_ZONE && distToStop > 0)
                speed = baseSpeed * (0.2f + 0.8f * (distToStop / BRAKE_ZONE));
            else
                speed = baseSpeed;
        }

        switch (dir) {
            case NORTH: y -= speed * dt; break;
            case SOUTH: y += speed * dt; break;
            case EAST:  x += speed * dt; break;
            case WEST:  x -= speed * dt; break;
        }

        float laneCenter = laneCenterFor(dir);
        if (dir == NORTH || dir == SOUTH) x = laneCenter + lateral;
        else                              y = laneCenter + lateral;

        bool wasInBox = inBox;
        inBox = isInsideBox();
        if (!passed && inBox) passed = true;

        if (passed && wasInBox && !inBox) {
            exitedBox = true;
            if (isEmergency && !emReleased) {
                im.releaseEmergency((int)dir);
                emReleased = true;
            }
        }

        if (y < -(float)SCREEN_H || y > 2.f * SCREEN_H
         || x < -(float)SIM_W   || x > 2.f * SIM_W)
            active = false;
    }

    void draw() const {
        if (!active) return;

        // Sprites are authored facing EAST (nose pointing +X / right).
        // DrawTexturePro rotates clockwise in screen space, so:
        //   EAST = 0 deg, SOUTH = 90 deg, WEST = 180 deg, NORTH = 270 deg.
        float rotation = 0.f;
        switch (dir) {
            case EAST:  rotation = 0.f;   break;
            case SOUTH: rotation = 90.f;  break;
            case WEST:  rotation = 180.f; break;
            case NORTH: rotation = 270.f; break;
        }

        const Texture2D* tex = &g_vehicleTextures.car;
        if (type == AMBULANCE)      tex = &g_vehicleTextures.ambulance;
        else if (type == FIRETRUCK) tex = &g_vehicleTextures.firetruck;

        if (g_vehicleTextures.loaded && tex->id != 0) {
            Rectangle src = { 0, 0, (float)tex->width, (float)tex->height };

            // ---------------------------------------------------------------
            // KEY FIX: DrawTexturePro rotates the dst rect in-place.
            // For 90� / 270� rotations the rendered visual dimensions are:
            //   screen-X extent = dst.height, screen-Y extent = dst.width
            // So for N/S vehicles we must pass dst (width=lengthAxis, height=crossAxis)
            // which means swapping our stored width/height for the dst rect only.
            // The origin must also use the dst rect dimensions (pre-rotation).
            // ---------------------------------------------------------------
            float dstW, dstH;
            if (dir == NORTH || dir == SOUTH) {
                // stored: width=crossAxis, height=lengthAxis
                // pass to dst: width=lengthAxis, height=crossAxis
                // ? after 90�/270� rotation raylib renders it
                //   lengthAxis tall � crossAxis wide on screen  ?
                dstW = height;   // lengthAxis
                dstH = width;    // crossAxis
            } else {
                dstW = width;    // lengthAxis (already correct for E/W)
                dstH = height;   // crossAxis
            }

            Rectangle dst    = { x, y, dstW, dstH };
            Vector2   origin = { dstW / 2.f, dstH / 2.f };

            DrawTexturePro(*tex, src, dst, origin, rotation, WHITE);
        } else {
            // Fallback: draw a properly oriented rectangle
            float drawW = (dir == NORTH || dir == SOUTH) ? width  : width;
            float drawH = (dir == NORTH || dir == SOUTH) ? height : height;
            Rectangle body = { x - drawW/2, y - drawH/2, drawW, drawH };
            DrawRectangleRec(body, color);
            DrawRectangleLinesEx(body, 1.5f, DARKGRAY);
        }

        if (isEmergency && flashOn) {
            Color flash1 = (type == AMBULANCE) ? BLUE : Color{255, 70,  0, 255};
            Color flash2 = (type == AMBULANCE) ? RED  : Color{255,220,  0, 255};
            bool vertical = (dir == NORTH || dir == SOUTH);
            if (vertical) {
                DrawRectangle((int)(x-width/2+2),  (int)(y-3), 5, 5, flash1);
                DrawRectangle((int)(x+width/2-7),  (int)(y-3), 5, 5, flash2);
            } else {
                DrawRectangle((int)(x-3), (int)(y-height/2+2), 5, 5, flash1);
                DrawRectangle((int)(x-3), (int)(y+height/2-7), 5, 5, flash2);
            }
        }

        if (waiting && !isEmergency)
            DrawCircle((int)x, (int)(y - height/2 - 5), 3, RED);
    }

private:

    float farAway() const {
        return (dir == NORTH || dir == WEST) ? -1e9f : 1e9f;
    }

    float moreRestrictive(float current, float candidate) const {
        switch (dir) {
            case NORTH: case WEST: return (candidate > current) ? candidate : current;
            default:
                float fa = farAway();
                return (current == fa || candidate < current) ? candidate : current;
        }
    }

    void updateOvertakeDecision(const Vehicle* allVehicles, int count)
    {
        if (!isEmergency) return;
        targetLateral = 0.f;
    }

    float computeAllowedStopPosition(const IntersectionManager& im,
                                     const Vehicle* allVehicles, int count,
                                     const LaneQueue& laneQueue) const
    {
        if (passed && exitedBox) return farAway();

        if (isEmergency && emRegistered) {
            if (!im.canEmergencyGo((int)dir)) return stopLinePosition();

            float myFront = frontEdge();
            float best    = farAway();

            for (int i = 0; i < count; i++) {
                const Vehicle& other = allVehicles[i];
                if (!other.active || &other == this || other.dir != dir) continue;

                float otherBack = other.backEdge();
                bool  isAhead   = false;
                float dist      = 0.f;

                switch (dir) {
                    case NORTH: isAhead=(otherBack<myFront); dist=myFront-otherBack; break;
                    case SOUTH: isAhead=(otherBack>myFront); dist=otherBack-myFront; break;
                    case EAST:  isAhead=(otherBack>myFront); dist=otherBack-myFront; break;
                    case WEST:  isAhead=(otherBack<myFront); dist=myFront-otherBack; break;
                }
                if (!isAhead || dist > 400.f) continue;

                float target = 0.f;
                switch (dir) {
                    case NORTH: target = otherBack + MIN_GAP; break;
                    case SOUTH: target = otherBack - MIN_GAP; break;
                    case EAST:  target = otherBack - MIN_GAP; break;
                    case WEST:  target = otherBack + MIN_GAP; break;
                }
                best = moreRestrictive(best, target);
            }
            return best;
        }

        float stopPos = farAway();

        if (!passed && !im.canGo((int)dir))
            stopPos = stopLinePosition();

        float myFront = frontEdge();
        for (int k = 0; k < laneQueue.size(); k++) {
            int vid = laneQueue.at(k);
            if (vid < 0 || vid >= count) continue;
            const Vehicle& other = allVehicles[vid];
            if (!other.active || &other == this) continue;

            float otherBack = other.backEdge();
            bool  isAhead   = false;
            float dist      = 0.f;

            switch (dir) {
                case NORTH: isAhead=(otherBack<myFront); dist=myFront-otherBack; break;
                case SOUTH: isAhead=(otherBack>myFront); dist=otherBack-myFront; break;
                case EAST:  isAhead=(otherBack>myFront); dist=otherBack-myFront; break;
                case WEST:  isAhead=(otherBack<myFront); dist=myFront-otherBack; break;
            }
            if (!isAhead || dist > 350.f) continue;

            float target = 0.f;
            switch (dir) {
                case NORTH: target = otherBack + MIN_GAP; break;
                case SOUTH: target = otherBack - MIN_GAP; break;
                case EAST:  target = otherBack - MIN_GAP; break;
                case WEST:  target = otherBack + MIN_GAP; break;
            }
            stopPos = moreRestrictive(stopPos, target);
        }
        return stopPos;
    }
};

class Simulator {
public:
    Simulator()
        : simTime(0), spawnTimer(0), spawnInterval(1.9f)
        , emTimer(0), emInterval(14.f)
        , totalSpawned(0), totalWaiting(0), totalCompleted(0)
        , spawnColorIndex(0), totalWaitAccum(0), avgWaitTime(0)
    {
        for (int i = 0; i < MAX_VEHICLES; i++) prevActive[i] = false;
        for (int i = 0; i < MAX_VEHICLES; i++) posBuffer[i]  = 0.f;
    }

    void initialize() {
        srand((unsigned)time(nullptr));
        InitAudioDevice();
        hornSound  = generateBeep(440.f, 0.18f);
        sirenSound = generateSiren();
        bgTexture  = LoadTexture("background.png");

        g_vehicleTextures.car       = LoadTexture("car.png");
        g_vehicleTextures.ambulance = LoadTexture("ambulance.png");
        g_vehicleTextures.firetruck = LoadTexture("firetruck.png");
        g_vehicleTextures.loaded = (g_vehicleTextures.car.id != 0
                                 && g_vehicleTextures.ambulance.id != 0
                                 && g_vehicleTextures.firetruck.id != 0);
        if (g_vehicleTextures.loaded) {
            SetTextureFilter(g_vehicleTextures.car,       TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(g_vehicleTextures.ambulance, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(g_vehicleTextures.firetruck, TEXTURE_FILTER_BILINEAR);
        }

        intersection.initialize();

        Direction startDirs[] = { NORTH, SOUTH, EAST, WEST };
        for (int i = 0; i < 6; i++) spawnCar(startDirs[i % 4]);
    }

    void unload() {
        UnloadSound(hornSound);
        UnloadSound(sirenSound);
        if (g_vehicleTextures.car.id       != 0) UnloadTexture(g_vehicleTextures.car);
        if (g_vehicleTextures.ambulance.id != 0) UnloadTexture(g_vehicleTextures.ambulance);
        if (g_vehicleTextures.firetruck.id != 0) UnloadTexture(g_vehicleTextures.firetruck);
        if (bgTexture.id != 0) UnloadTexture(bgTexture);
        CloseAudioDevice();
    }

    void update(float dt) {
        if (dt > 0.05f) dt = 0.05f;
        simTime += dt;

        for (int d = 0; d < 4; d++) laneQueues[d].clear();
        for (int i = 0; i < MAX_VEHICLES; i++) {
            if (!vehicles[i].active) continue;
            Direction d = vehicles[i].dir;
            laneQueues[(int)d].push(i);
            posBuffer[i] = (d == NORTH || d == SOUTH) ? vehicles[i].y : vehicles[i].x;
        }
        laneQueues[NORTH].sortByPosition(posBuffer, true);
        laneQueues[SOUTH].sortByPosition(posBuffer, false);
        laneQueues[EAST ].sortByPosition(posBuffer, true);
        laneQueues[WEST ].sortByPosition(posBuffer, false);

        for (int i = 0; i < 4; i++) intersection.queueCount[i] = 0;
        for (int i = 0; i < MAX_VEHICLES; i++) {
            if (!vehicles[i].active) continue;
            if (!vehicles[i].passed)
                intersection.queueCount[(int)vehicles[i].dir]++;
        }

        intersection.update(dt);

        {
            bool ns = false, ew = false;
            for (int i = 0; i < MAX_VEHICLES; i++) {
                if (!vehicles[i].active || !vehicles[i].isInsideBox()) continue;
                if (vehicles[i].dir == NORTH || vehicles[i].dir == SOUTH) ns = true;
                else                                                        ew = true;
            }
            intersection.boxOccupiedNS = ns;
            intersection.boxOccupiedEW = ew;
        }

        totalWaiting = 0;
        for (int i = 0; i < MAX_VEHICLES; i++) {
            if (!vehicles[i].active) continue;
            vehicles[i].update(dt, intersection, vehicles, MAX_VEHICLES,
                               laneQueues[(int)vehicles[i].dir]);
            if (vehicles[i].waiting && !vehicles[i].isEmergency) totalWaiting++;
        }

        {
            int liveMask = 0;
            for (int i = 0; i < MAX_VEHICLES; i++) {
                if (!vehicles[i].active)        continue;
                if (!vehicles[i].isEmergency)   continue;
                if (!vehicles[i].emRegistered)  continue;
                if ( vehicles[i].emReleased)    continue;
                liveMask |= (1 << (int)vehicles[i].dir);
            }
            intersection.pruneOrphanEntries(liveMask);
        }

        for (int i = 0; i < MAX_VEHICLES; i++) {
            if (prevActive[i] && !vehicles[i].active) {
                totalWaitAccum += vehicles[i].waitTime;
                totalCompleted++;
                if (vehicles[i].isEmergency && vehicles[i].emRegistered
                    && !vehicles[i].emReleased) {
                    intersection.releaseEmergency((int)vehicles[i].dir);
                    vehicles[i].emReleased = true;
                }
            }
            prevActive[i] = vehicles[i].active;
        }
        avgWaitTime = (totalCompleted > 0) ? totalWaitAccum / (float)totalCompleted : 0.f;

        if (intersection.anyEmergencyActive()) {
            if (!IsSoundPlaying(sirenSound)) PlaySound(sirenSound);
        } else {
            if (IsSoundPlaying(sirenSound)) StopSound(sirenSound);
        }

        spawnTimer += dt;
        if (spawnTimer >= spawnInterval) {
            spawnTimer = 0.f;
            spawnCar((Direction)(rand() % 4));
        }
        emTimer += dt;
        if (emTimer >= emInterval) {
            emTimer = 0.f;
            VehicleType emType = (rand() % 2 == 0) ? AMBULANCE : FIRETRUCK;
            spawnEmergency(emType, (Direction)(rand() % 4));
        }
    }

    void draw() const {
        ClearBackground({30, 110, 30, 255});

        if (bgTexture.id != 0) {
            DrawTexture(bgTexture, 0, 0, WHITE);
        }

        intersection.drawLights();

        for (int i = 0; i < MAX_VEHICLES; i++)
            if (vehicles[i].active && !vehicles[i].isEmergency) vehicles[i].draw();
        for (int i = 0; i < MAX_VEHICLES; i++)
            if (vehicles[i].active &&  vehicles[i].isEmergency) vehicles[i].draw();

        if (intersection.anyEmergencyActive())
            DrawRectangle(0, 0, SIM_W, SCREEN_H, {255, 80, 0, 9});

        drawSidebar();

        DrawText("N", (int)(CENTER_X-6), 6,              18, WHITE);
        DrawText("S", (int)(CENTER_X-6), SCREEN_H-24,    18, WHITE);
        DrawText("W", 6,                 (int)(CENTER_Y-9), 18, WHITE);
        DrawText("E", SIM_W-20,          (int)(CENTER_Y-9), 18, WHITE);
    }

    void spawnCar(Direction d) {
        int slot = findFreeSlot();
        if (slot < 0) return;
        vehicles[slot].spawn(CAR, d, spawnColorIndex++, vehicles, MAX_VEHICLES);
        totalSpawned++;
    }

    void spawnEmergency(VehicleType t, Direction d) {
        int slot = findFreeSlot();
        if (slot < 0) return;
        vehicles[slot].spawn(t, d, spawnColorIndex++, vehicles, MAX_VEHICLES);
        totalSpawned++;
    }

private:
    Vehicle            vehicles[MAX_VEHICLES];
    IntersectionManager intersection;
    LaneQueue          laneQueues[4];
    float              posBuffer[MAX_VEHICLES];

    Sound  hornSound, sirenSound;
    Texture2D bgTexture;
    float  simTime, spawnTimer, spawnInterval, emTimer, emInterval;
    int    totalSpawned, totalWaiting, totalCompleted, spawnColorIndex;
    float  totalWaitAccum, avgWaitTime;
    bool   prevActive[MAX_VEHICLES];

    int findFreeSlot() const {
        for (int i = 0; i < MAX_VEHICLES; i++)
            if (!vehicles[i].active) return i;
        return -1;
    }

    Sound generateBeep(float freq, float dur, int sampleRate = 44100) {
        int     n = (int)(sampleRate * dur);
        short*  buf = (short*)MemAlloc(n * 2);
        for (int i = 0; i < n; i++) {
            float t   = (float)i / sampleRate;
            float env = 1.f;
            if (t < 0.01f)       env = t / 0.01f;
            if (t > dur - 0.05f) env = (dur - t) / 0.05f;
            buf[i] = (short)(env * 14000 * sinf(TWO_PI * freq * t));
        }
        Wave w = {}; w.frameCount=n; w.sampleRate=sampleRate;
        w.sampleSize=16; w.channels=1; w.data=buf;
        Sound s = LoadSoundFromWave(w); MemFree(buf); return s;
    }

    Sound generateSiren(int sampleRate = 44100) {
        float   dur = 1.6f;
        int     n   = (int)(sampleRate * dur);
        short*  buf = (short*)MemAlloc(n * 2);
        for (int i = 0; i < n; i++) {
            float t     = (float)i / sampleRate;
            float phase = fmodf(t, 1.6f) / 1.6f;
            float freq  = 580.f + 440.f * fabsf(2.f*phase - 1.f);
            buf[i] = (short)(14000 * sinf(TWO_PI * freq * t));
        }
        Wave w = {}; w.frameCount=n; w.sampleRate=sampleRate;
        w.sampleSize=16; w.channels=1; w.data=buf;
        Sound s = LoadSoundFromWave(w); MemFree(buf); return s;
    }

    void drawSidebar() const {
        DrawRectangle(SIM_W, 0, SIDEBAR_W, SCREEN_H, {18,18,32,255});
        DrawLineEx({(float)SIM_W, 0}, {(float)SIM_W, (float)SCREEN_H}, 2, {70,70,115,255});

        int  x  = SIM_W + 12;
        int  y  = 14;
        char buf[96];

        DrawText("SMART TRAFFIC",  x, y, 17, {100,200,255,255}); y += 20;
        DrawText("GRID SIMULATOR", x, y, 17, {100,200,255,255}); y += 28;
        divider(y); y += 8;

        DrawText("SIM TIME", x, y, 11, GRAY); y += 13;
        snprintf(buf,96,"%02d:%02d",(int)(simTime/60),(int)fmodf(simTime,60));
        DrawText(buf, x, y, 24, WHITE); y += 30;
        divider(y); y += 8;

        DrawText("WAITING", x, y, 11, GRAY); y += 13;
        snprintf(buf,96,"%d", totalWaiting);
        DrawText(buf, x, y, 28, totalWaiting > 7 ? RED : YELLOW); y += 30;

        DrawText("AVG WAIT", x, y, 11, GRAY); y += 13;
        snprintf(buf,96,"%.1f s", avgWaitTime);
        DrawText(buf, x, y, 20, {255,200,100,255}); y += 24;

        snprintf(buf,96,"Spawned:%d  Done:%d", totalSpawned, totalCompleted);
        DrawText(buf, x, y, 11, {140,220,140,255}); y += 20;
        divider(y); y += 8;

        DrawText("LANE QUEUES", x, y, 11, GRAY); y += 13;
        static const char* dirLabels[] = {"N","S","E","W"};
        for (int d = 0; d < 4; d++) {
            int   q  = intersection.queueCount[d];
            Color qc = (q > 6) ? RED : (q > 3) ? YELLOW : GREEN;
            snprintf(buf,96,"%s  %2d", dirLabels[d], q);
            DrawText(buf, x, y, 12, qc);
            int barW = q * 7; if (barW > 110) barW = 110;
            DrawRectangle(x+38, y+2, barW, 9, qc);
            y += 15;
        }
        y += 4; divider(y); y += 8;

        DrawText("EMERGENCY STATUS", x, y, 11, GRAY); y += 13;
        if (intersection.anyEmergencyActive()) {
            Color flashColor = ((int)(GetTime()*5) % 2 == 0) ? RED : ORANGE;
            DrawText("!! PRIORITY ACTIVE !!", x, y, 14, flashColor); y += 18;

            struct DisplayEntry { int dir; int rank; int seq; };
            DisplayEntry entries[8]; int ne = 0;
            const EmergencyPQ& pq = intersection.getEmPQ();
            for (int i = 0; i < pq.size() && ne < 8; i++) {
                entries[ne++] = { pq.entryAt(i).direction,
                                  pq.entryAt(i).priorityRank,
                                  pq.entryAt(i).sequence };
            }
            for (int i = 0; i < ne-1; i++)
                for (int j = 0; j < ne-i-1; j++) {
                    bool swap = (entries[j].rank > entries[j+1].rank)
                             || (entries[j].rank == entries[j+1].rank
                                 && entries[j].seq > entries[j+1].seq);
                    if (swap) {
                        DisplayEntry tmp = entries[j];
                        entries[j] = entries[j+1];
                        entries[j+1] = tmp;
                    }
                }
            static const char* dirNames[] = {"NORTH","SOUTH","EAST","WEST"};
            for (int i = 0; i < ne; i++) {
                bool        isTop = (i == 0);
                const char* label = (entries[i].rank == 0) ? "AMBULANCE" : "FIRE TRUCK";
                snprintf(buf,96,"%s%s [%s]", isTop?"> ":"  ",
                         dirNames[entries[i].dir], label);
                DrawText(buf, x, y, 11, isTop ? ORANGE : Color{180,130,60,255});
                y += 13;
            }
        } else {
            DrawText("INACTIVE", x, y, 20, {70,180,70,255}); y += 26;
        }
        divider(y); y += 8;

        DrawText("LIGHTS", x, y, 11, GRAY); y += 13;
        for (int i = 0; i < 4; i++) {
            Color  lc  = RED;
            const char* ls = "LT_RED";
            switch (intersection.lights[i].state) {
                case LT_GREEN:  lc = GREEN;  ls = "GRN"; break;
                case LT_YELLOW: lc = YELLOW; ls = "YLW"; break;
                default: break;
            }
            snprintf(buf,96,"%s %s [%d]", dirLabels[i], ls, intersection.queueCount[i]);
            DrawCircle(x+5, y+6, 4, lc);
            DrawText(buf, x+13, y, 12, lc);
            if (intersection.lights[i].override) DrawText("EM", x+105, y, 10, ORANGE);
            y += 15;
        }

        snprintf(buf,96,"Box NS=%s EW=%s",
                 intersection.boxOccupiedNS ? "IN" : "--",
                 intersection.boxOccupiedEW ? "IN" : "--");
        DrawText(buf, x, y+4, 10, {130,130,190,255}); y += 18;

        divider(y); y += 8;

        DrawText("LEGEND", x, y, 11, GRAY); y += 12;
        DrawRectangle(x,y,13,8,{40,80,210,255}); DrawText("Car",              x+17,y,11,WHITE); y+=13;
        DrawRectangle(x,y,13,8,WHITE);            DrawText("Ambulance (HIGHEST)",x+17,y,10,WHITE); y+=13;
        DrawRectangle(x,y,13,8,{220,20,20,255}); DrawText("Fire Truck (HIGH)",x+17,y,10,WHITE); y+=18;
        divider(y); y += 8;

        DrawText("[E] Spawn Emergency", x, y, 11, {160,160,160,255}); y += 13;
        DrawText("[C] Spawn Car",       x, y, 11, {160,160,160,255}); y += 13;
        DrawText("[ESC] Quit",          x, y, 11, {160,160,160,255});
    }

    void divider(int y) const {
        DrawLine(SIM_W+5, y, SCREEN_W-5, y, {55,55,100,255});
    }
};

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Smart Traffic Grid Simulator");
    SetTargetFPS(60);

    Simulator sim;
    sim.initialize();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_E))
            sim.spawnEmergency(
                (rand() % 2 == 0) ? AMBULANCE : FIRETRUCK,
                (Direction)(rand() % 4));
        if (IsKeyPressed(KEY_C))
            sim.spawnCar((Direction)(rand() % 4));

        sim.update(dt);

        BeginDrawing();
        sim.draw();
        EndDrawing();
    }

    sim.unload();
    CloseWindow();
    return 0;
}
