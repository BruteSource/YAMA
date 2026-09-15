#pragma once
#include "input.h"

void arkanoidInit();
void arkanoidUpdate(const InputState& in, float dt);
void arkanoidOnExit();
void arkanoidRenderSnapshot();
bool arkanoidWantsExit(const InputState& in);
