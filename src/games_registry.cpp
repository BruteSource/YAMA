// Add a new game: give it its own <name>.h/.cpp exposing init/update/onExit,
// then add one row below.
#include "apps.h"
#include "arkanoid.h"
#include "tetris.h"
#include "bustamove.h"
#include "rtype.h"
#include "bubblebobble.h"

GameEntry GAMES[GAME_COUNT] = {
  { "ARKANOID",     true, arkanoidInit,     arkanoidUpdate,     arkanoidOnExit,     arkanoidRenderSnapshot,     false, arkanoidWantsExit },
  { "TETRIS",       true, tetrisInit,       tetrisUpdate,       tetrisOnExit,       tetrisRenderSnapshot,       false, tetrisWantsExit },
  { "BUST-A-MOVE",  true, bustamoveInit,    bustamoveUpdate,    bustamoveOnExit,    bustamoveRenderSnapshot,    false, bustamoveWantsExit },
  { "R-TYPE",       true, rtypeInit,        rtypeUpdate,        rtypeOnExit,        rtypeRenderSnapshot,        false, rtypeWantsExit },
  { "BUBBLE BOBBLE",true, bubblebobbleInit, bubblebobbleUpdate, bubblebobbleOnExit, bubblebobbleRenderSnapshot, false, bubblebobbleWantsExit },
};
