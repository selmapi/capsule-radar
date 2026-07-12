#pragma once
// IMU gesture module (device-only). Runs on top of imu_qmi8658's raw accel reads:
// shake-to-refresh, auto-rotate (screen-plane orientation), wake-on-motion, and a
// ported orientation-agnostic face-down detector. Call motion_update() every loop();
// it self-limits to ~30 Hz so it's cheap to call unconditionally.
#include <stdbool.h>

void motion_begin();
void motion_update();                 // call every loop(); self-limits to ~30 Hz
bool motion_facedown();               // orientation-agnostic face-down
bool motion_take_shake();             // edge: true once per shake, then clears
int  motion_orientation();            // 0/90/180/270, or -1 = flat/unknown (hold last)
bool motion_take_wake();              // edge: true once if motion seen since last call
void motion_set_enabled(bool shake, bool autorotate, bool wake);
