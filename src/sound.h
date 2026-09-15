// Simple piezo buzzer SFX shared by the menu and every game.
#pragma once

void soundInit();

void sfxUiMove();     // menu navigate
void sfxUiConfirm();  // menu select / launch / ball launch
void sfxHit();        // bounce / rotate / brick damaged
void sfxBreak();      // brick destroyed / piece locked
void sfxPowerup();    // good power-up caught
void sfxBad();        // bad power-up / life lost
void sfxLevelClear(); // Arkanoid level cleared
void sfxGameOver();
void sfxLineClear(int lines); // Tetris: 1-4 lines, pitch/length scale with count
