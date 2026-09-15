#pragma once
#include "input.h"

void tetrisInit();
void tetrisUpdate(const InputState& in, float dt);
void tetrisOnExit();
void tetrisRenderSnapshot();
bool tetrisWantsExit(const InputState& in);
