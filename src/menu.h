// The game-selector launcher screen.
#pragma once
#include "input.h"

void menuInit();
// Returns the GameId to launch this frame, or -1 if the user hasn't picked yet.
int menuUpdate(const InputState& in);
void menuRenderSnapshot();
