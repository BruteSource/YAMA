// Registry of installed games/apps. Add a new game by giving it its own
// <name>.h/.cpp exposing init/update/onExit, then adding one row in
// games_registry.cpp.
#pragma once
#include "input.h"

enum GameId { GAME_ARKANOID = 0, GAME_TETRIS, GAME_BUSTAMOVE, GAME_RTYPE, GAME_BUBBLEBOBBLE, GAME_COUNT };

struct GameEntry {
  const char* name;
  bool available;                              // false = show as "COMING SOON"
  void (*init)();                               // called once when launched
  void (*update)(const InputState& in, float dt); // called every frame while active
  void (*onExit)();                             // called when returning to menu; may be nullptr
  void (*renderSnapshot)();                     // re-draws current state for screenshot capture
  bool landscape = false;                       // true = console rotates the display for this game
  bool (*wantsExit)(const InputState& in) = nullptr; // nullptr = default: in.key0Edge exits immediately.
                                                 // Set this to take over KEY0 for gameplay instead;
                                                 // main.cpp calls this each frame and exits to the
                                                 // menu only when it returns true (see R-Type).
};

extern GameEntry GAMES[GAME_COUNT];
