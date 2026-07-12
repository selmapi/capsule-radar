// IMU gesture module (device-only, ~30 Hz). Layers four independent detectors on top
// of imu_qmi8658's raw accel reads: shake-to-refresh, auto-rotate, wake-on-motion, and
// a ported orientation-agnostic face-down detector (see imu_qmi8658.cpp's imu_facedown()
// for the original single-axis version this generalizes).
#include "motion.h"
#include "imu_qmi8658.h"
#include <Arduino.h>
#include <math.h>

// Uncomment (or pass -DMOTION_DEBUG as a build flag) to Serial.printf every sample and
// every fired event. Left OFF by default — this is a 30 Hz log and will flood the console.
// #define MOTION_DEBUG

// ---------------------------------------------------------------------------------------
// Tunables — ALL on-device-tuned starting values. Nothing below should be hardcoded
// inline elsewhere; change these constants during the tuning session.
// ---------------------------------------------------------------------------------------
constexpr uint32_t SAMPLE_MS       = 33;    // ~30 Hz
constexpr float    SHAKE_G         = 0.45f; // |‖a‖-1| beyond this = a strong swing
constexpr int      SHAKE_SWINGS    = 3;     // strong alternating swings needed
constexpr uint32_t SHAKE_WINDOW_MS = 800;
constexpr uint32_t SHAKE_COOLDOWN  = 2000;
constexpr float    TILT_G          = 0.40f; // in-plane gravity below this = "flat", hold orientation
constexpr uint32_t ROT_HOLD_MS     = 1000;  // candidate orientation must hold this long to commit
constexpr float    WAKE_G          = 0.30f; // frame-to-frame |a| deviation to count as motion
constexpr float    FD_THRESH_G     = 0.55f; // face-down swing (matches old 9000/16384≈0.55g)
// Auto-rotate axis mapping — CALIBRATED ON-DEVICE. The screen-plane axes and their signs
// depend on how the QMI8658 is mounted; flip these during the tuning session until turning
// the device the "expected" way rotates the UI the "expected" way.
constexpr float AX_SIGN = 1.0f;   // sign of accel-X as screen-X
constexpr float AY_SIGN = 1.0f;   // sign of accel-Y as screen-Y

// ---------------------------------------------------------------------------------------
// Sample-rate gate
// ---------------------------------------------------------------------------------------
static uint32_t s_lastMs = 0;

// ---------------------------------------------------------------------------------------
// Feature enables (default all on)
// ---------------------------------------------------------------------------------------
static bool s_enShake  = true;
static bool s_enRotate = true;
static bool s_enWake   = true;

// ---------------------------------------------------------------------------------------
// Face-down: ported from imu_facedown()'s orientation-agnostic baseline. Slow EMA of the
// *resting* az; face-down fires when az swings to the far opposite side of that baseline,
// so it works regardless of which way "up" faces once the board is in its case.
// ---------------------------------------------------------------------------------------
static float s_fdRef    = 0.0f;
static bool  s_haveFdRef = false;
static bool  s_facedown = false;

static void detectFacedown(float az) {
    if (!s_haveFdRef) { s_fdRef = az; s_haveFdRef = true; }
    const bool down = (s_fdRef < 0) ? (az > FD_THRESH_G) : (az < -FD_THRESH_G);
    if (!down) s_fdRef += (az - s_fdRef) * (1.0f / 16.0f);   // EMA toward resting orientation
    s_facedown = down;
}

// ---------------------------------------------------------------------------------------
// Shake: fire when SHAKE_SWINGS strong, alternating-sign deviations of |a| from 1g all fall
// inside a *rolling* SHAKE_WINDOW_MS span. A "strong swing" is |‖a‖-1| > SHAKE_G with a sign
// opposite the previously recorded swing (same-sign strong readings in a row are still the
// same swing and are not double-counted). We keep the timestamps of the last SHAKE_SWINGS
// alternating swings in a ring buffer; on each new swing we check whether the oldest of
// those is within SHAKE_WINDOW_MS of now. This is a sliding span rather than a fixed anchor
// at swing #1 — so a sustained shake whose individual swings each sit just under the window
// still fires (the old first-swing anchor could reset-and-restart forever and never fire).
// ---------------------------------------------------------------------------------------
static uint32_t s_swingTimes[SHAKE_SWINGS];
static int      s_swingHead    = 0;
static int      s_swingLen     = 0;
static int      s_lastSwingSign = 0;   // 0 = no swing recorded yet
static uint32_t s_lastShake    = 0;
static bool     s_everShaken   = false;
static bool     s_shakePending = false;

static void detectShake(float dev, uint32_t now) {
    if (fabsf(dev) > SHAKE_G) {
        const int sign = (dev > 0) ? 1 : -1;
        if (sign != s_lastSwingSign) {   // alternating (also true on the very first swing, sign 0)
            s_lastSwingSign = sign;
            s_swingTimes[s_swingHead] = now;
            s_swingHead = (s_swingHead + 1) % SHAKE_SWINGS;
            if (s_swingLen < SHAKE_SWINGS) s_swingLen++;

            if (s_swingLen == SHAKE_SWINGS) {
                // head now points at the oldest of the last SHAKE_SWINGS swings
                const uint32_t oldest = s_swingTimes[s_swingHead];
                const bool cooldownOk = (!s_everShaken || now - s_lastShake > SHAKE_COOLDOWN);
                if (now - oldest <= SHAKE_WINDOW_MS && cooldownOk) {
                    s_shakePending = true;
                    s_lastShake = now;
                    s_everShaken = true;
                    s_swingLen = 0;   // start fresh
#ifdef MOTION_DEBUG
                    Serial.printf("[motion] EVENT shake\n");
#endif
                }
            }
        }
        // same-sign strong reading: ignore, still inside the same swing
    }
}

// ---------------------------------------------------------------------------------------
// Orientation: snap the in-plane gravity direction to the nearest 0/90/180/270 and commit
// it only once the same candidate has held for ROT_HOLD_MS (debounces the flip while the
// device is mid-turn). "Flat" (in-plane gravity below TILT_G, i.e. mostly face-up/down) is
// not a real candidate — it just holds whatever orientation was last committed.
// ---------------------------------------------------------------------------------------
constexpr int ORIENTATION_UNSET = -2;   // sentinel: no candidate tracked yet (distinct from -1 = flat)
static int      s_lastCandidate = ORIENTATION_UNSET;
static uint32_t s_candidateSince = 0;
static int      s_orientation = 0;

static void detectOrientation(float ax, float ay, uint32_t now) {
    const float ip = sqrtf(ax * ax + ay * ay);
    int candidate;
    if (ip < TILT_G) {
        candidate = -1;   // flat — hold last committed orientation
    } else {
        float deg = atan2f(AY_SIGN * ay, AX_SIGN * ax) * 180.0f / PI;
        if (deg < 0.0f) deg += 360.0f;
        int snap = ((int)roundf(deg / 90.0f) * 90) % 360;
        if (snap < 0) snap += 360;
        candidate = snap;
    }

    if (candidate == -1) {
        s_lastCandidate = -1;
        s_candidateSince = now;
        return;   // s_orientation left unchanged
    }

    if (candidate == s_lastCandidate) {
        if (now - s_candidateSince >= ROT_HOLD_MS && s_orientation != candidate) {
            s_orientation = candidate;
#ifdef MOTION_DEBUG
            Serial.printf("[motion] EVENT rotate -> %d\n", s_orientation);
#endif
        }
    } else {
        s_lastCandidate = candidate;
        s_candidateSince = now;
    }
}

// ---------------------------------------------------------------------------------------
// Wake: slow baseline of |a|; any frame-to-frame deviation beyond WAKE_G latches a pending
// wake event. Baseline always eases toward the current reading so a real orientation change
// doesn't leave wake permanently "hot".
// ---------------------------------------------------------------------------------------
static float s_wakeBase     = 1.0f;
static bool  s_haveWakeRef  = false;
static bool  s_wakePending  = false;

static void detectWake(float m) {
    if (!s_haveWakeRef) { s_wakeBase = m; s_haveWakeRef = true; return; }
    if (fabsf(m - s_wakeBase) > WAKE_G) {
        s_wakePending = true;
#ifdef MOTION_DEBUG
        Serial.printf("[motion] EVENT wake\n");
#endif
    }
    s_wakeBase += (m - s_wakeBase) * (1.0f / 8.0f);
}

// ---------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------
void motion_begin() {
    imu_begin();
    s_lastMs = 0;

    s_haveFdRef = false;
    s_facedown = false;

    s_swingHead = 0;
    s_swingLen = 0;
    s_lastSwingSign = 0;
    s_lastShake = 0;
    s_everShaken = false;
    s_shakePending = false;

    s_lastCandidate = ORIENTATION_UNSET;
    s_candidateSince = 0;
    s_orientation = 0;

    s_haveWakeRef = false;
    s_wakePending = false;
}

void motion_update() {
    if (millis() - s_lastMs < SAMPLE_MS) return;
    s_lastMs = millis();

    float ax, ay, az;
    if (!imu_read_accel(&ax, &ay, &az)) return;   // noisy shared bus — a failed read just skips this tick

    detectFacedown(az);

    const float m = sqrtf(ax * ax + ay * ay + az * az);
    const float dev = m - 1.0f;

    // Detectors only run while enabled — avoids accumulating stale swing/candidate state
    // while a feature is off, which would otherwise surface as a surprise event the
    // instant it's re-enabled.
    if (s_enShake)  detectShake(dev, s_lastMs);
    if (s_enRotate) detectOrientation(ax, ay, s_lastMs);
    if (s_enWake)   detectWake(m);

#ifdef MOTION_DEBUG
    Serial.printf("[motion] ax=%.3f ay=%.3f az=%.3f |a|=%.3f\n", ax, ay, az, m);
#endif
}

bool motion_facedown() {
    return s_facedown;
}

bool motion_take_shake() {
    if (!s_enShake) return false;
    const bool v = s_shakePending;
    s_shakePending = false;
    return v;
}

int motion_orientation() {
    if (!s_enRotate) return -1;   // feature off: caller should do nothing
    return s_orientation;
}

bool motion_take_wake() {
    if (!s_enWake) return false;
    const bool v = s_wakePending;
    s_wakePending = false;
    return v;
}

void motion_set_enabled(bool shake, bool autorotate, bool wake) {
    // On OFF->ON, clear stale state so a re-enabled detector can't fire a phantom event
    // built from a pre-disable baseline (e.g. toggle wake off then on -> instant false wake).
    if (wake  && !s_enWake)  { s_haveWakeRef = false; s_wakePending = false; }   // fresh baseline on re-enable
    if (shake && !s_enShake) { s_swingLen = 0; s_shakePending = false; }
    s_enShake  = shake;
    s_enRotate = autorotate;
    s_enWake   = wake;
}
