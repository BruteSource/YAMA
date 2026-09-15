// R-Type III (The Third Lightning) homage — landscape side-scrolling shmup.
// Stage 1 ("Catapult Dimension") only; see README for controls and scope.
#pragma once
#include "input.h"

void rtypeInit();
void rtypeUpdate(const InputState& in, float dt);
void rtypeOnExit();
void rtypeRenderSnapshot();

// Takes over KEY0 (normally global "back to menu") for use as the fire
// button. main.cpp calls this every frame instead of checking in.key0Edge
// directly; only returns true from the pause screen.
bool rtypeWantsExit(const InputState& in);
