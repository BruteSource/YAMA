#pragma once
#include "input.h"

void bustamoveInit();
void bustamoveUpdate(const InputState& in, float dt);
void bustamoveOnExit();
void bustamoveRenderSnapshot();
bool bustamoveWantsExit(const InputState& in);
