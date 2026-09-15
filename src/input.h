// Per-frame input snapshot handed to whichever app is active. Populated once
// by main.cpp so every game/app shares one debounce/edge-detection pass.
#pragma once

struct InputState {
  long encDelta = 0;     // encoder ticks since last frame (signed)
  bool encSwHeld = false;
  bool encSwEdge = false; // true only on the frame it became pressed
  bool key0Edge = false;  // note: KEY0 is intercepted globally as "back to menu"
                          // before an app's update() is called, unless the app
                          // provides GameEntry::wantsExit (see apps.h) to take
                          // over KEY0 itself — R-Type does this to use KEY0 as
                          // its fire button.
  bool key0Held = false;
  bool btn1Held = false;
  bool btn1Edge = false;
  bool btn2Held = false;
  bool btn2Edge = false;

  // 2-pot analog thumbstick, normalized -1.0..1.0 (0 = centered), already
  // deadzoned/smoothed by hardware.cpp. Its push switch follows the same
  // held/edge pattern as every other button.
  float thumbX = 0.0f;
  float thumbY = 0.0f;
  bool thumbSwHeld = false;
  bool thumbSwEdge = false;
};
