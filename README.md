# Smart Traffic Grid Simulator

> A real-time C++ intersection simulator that solves wasted idle time, emergency-vehicle gridlock, and rush-hour overload with adaptive, priority-aware signal control.

## What It Does

Most traffic signals in use today still run on a fixed clock. They treat every intersection the same way at 3 AM and at 5 PM rush hour. The result is a set of predictable, everyday failures that drivers, pedestrians, and emergency responders all feel directly: time wasted at empty junctions, ambulances stuck behind a red light they cannot legally run, and gridlock that starts at one intersection and ripples backward through an entire corridor.

This project models a smarter alternative. It does not just animate cars — it implements the actual decision logic a smart-signal controller would need: per-lane queue tracking, proportional green-time allocation, and a binary-heap priority system for emergency vehicles.

---

## The Three Problems It Solves

### Problem 1 — The "Ghost Intersection" Wait

**The Problem:** You stop at a red light late at night. You look left, you look right — not a single car in sight. Yet the signal is blindly counting down its timer, wasting your time and fuel.

**The Smart Fix:** The simulator replaces the fixed timer with live occupancy awareness. Every frame, a `LaneQueue` for each of the four directions is rebuilt from the vehicles actually present. The `IntersectionManager` reads each queue's length and scales the green window to match real demand:

```cpp
greenDuration = 3.0f + (greenQueueCount / totalQueueCount) * 9.0f;
// busy approach → up to 12s green · empty approach → as little as 3s
```

If a direction has zero cars queued, its share of the cycle collapses toward the minimum — the digital equivalent of a sensor-triggered light that skips an empty lane rather than blindly making everyone wait.

---

### Problem 2 — The Emergency Blockage

**The Problem:** An ambulance rushes toward a busy intersection with sirens blaring — but the cars ahead are stopped at a red light with nowhere safe to go. The signal has no idea an emergency vehicle exists. Every second lost here matters at the destination.

**The Smart Fix:** The moment an ambulance or fire truck spawns, it registers itself with an `EmergencyPQ` — a binary-heap priority queue. Ambulances rank above fire trucks, and ties are broken by arrival order, so the system always knows the highest-priority direction in O(1) time, every frame.

Once a direction reaches the top of the heap, the `IntersectionManager` overrides all four lights — three go red, one goes green — and suspends normal timing entirely until the vehicle clears the box. Civilian cars in the same lane don't just sit there either: each one detects the emergency vehicle within 180 pixels and smoothly slides one lane width sideways, clearing a physical path. When the emergency vehicle exits the box, it releases its claim and normal cycling resumes automatically.

---

### Problem 3 — "Ghost Waves" and Artificial Gridlock

**The Problem:** At rush hour, traffic surges heavily from one direction — commuters leaving a campus or office park all at once. A fixed-timer light keeps giving the quiet side streets the same green window as the jammed main road. Cars backed up at one light spill back far enough to block the intersection behind it, and the jam cascades through the whole corridor.

**The Smart Fix:** The same adaptive-timing engine from Problem 1 scales in both directions — the busier approach actively gets up to 12 seconds of green while a near-empty cross street is held to as little as 3. Because queue counts are recalculated every single frame rather than once per cycle, the system reacts to a surge as it builds, not after it has already backed up. A `boxOccupancy` check (tracking whether N/S or E/W traffic is currently inside the intersection square) also prevents faster cycling from ever releasing conflicting flows into the box at the same time.

---

## Architecture

| Problem | Core Mechanism | Responsible Class |
|---|---|---|
| Ghost intersection wait | Per-lane queue counts drive proportional green time (3–12s) | `LaneQueue` + `IntersectionManager` |
| Emergency blockage | Min-heap priority dispatch + full signal override + lane yielding | `EmergencyPQ` + `Vehicle` |
| Rush-hour gridlock | Frame-by-frame adaptive scaling + box-occupancy collision guard | `IntersectionManager` |
| Visual state & safety | Three-state light machine with orange "overridden" indicator | `TrafficLight` |

---

## Key Stats

| | |
|---|---|
| Lines of C++ | ~850 |
| Major classes | 7 |
| Emergency direction lookup | O(1) every frame |
| Adaptive green range | 3 – 12 seconds |
| Max concurrent vehicles | 60 |
| Target frame rate | 60 FPS |

---

## Limitations

**Instant Signal Switching:** The simulator currently flips the relevant light from red to green the moment an emergency vehicle registers, with no transition buffer. In a real intersection this would be unsafe — a driver already committed to crossing on a green has no warning that right-of-way is about to change.

A practical version of this system would need a **sound sensor** mounted at the intersection that detects an approaching siren's distinctive frequency sweep and identifies which direction it is coming from. Instead of switching instantly, the controller would first alert conflicting approaches — flashing beacons or a "yield ahead" warning — and begin a short transition (e.g. 3 seconds of yellow / all-red) before the emergency vehicle's direction turns green. This trades a small amount of response speed for a buffer that keeps the system safe for drivers already inside the intersection.

---

## Controls

| Key | Action |
|---|---|
| `E` | Spawn a random emergency vehicle (ambulance or fire truck) from a random direction |
| `C` | Spawn a regular car from a random direction |
| `ESC` | Quit |

---
