#pragma once
#include "input.h"

void bubblebobbleInit();
void bubblebobbleUpdate(const InputState& in, float dt);
void bubblebobbleOnExit();
void bubblebobbleRenderSnapshot();
bool bubblebobbleWantsExit(const InputState& in);
