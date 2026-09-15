// Generic monophonic piezo-buzzer "sound engine," shared by every scene
// that wants sound (currently Pac-Man and the menu — see
// pacman_pr32_audio.h / menu_pr32_audio.h for their own note tables).
// Extracted out of pacman_pr32_audio.h once the menu needed the same
// sequencing machinery too.
//
// CRITICAL: every write here uses the 2-argument `tone(PIN_BUZZER,
// freq)` form, NEVER `tone(PIN_BUZZER, freq, duration)`. Passing a
// duration looks like the obviously-correct choice (it's what the
// parameter is FOR) but is real observed hardware breakage: the Arduino
// core's tone() queues each call through a separate FreeRTOS task
// (cores/esp32/Tone.cpp) that does a BLOCKING delay(duration) before
// it'll process the next queued call. Retrigger it faster than that —
// which a chomp/UI-blip SFX retriggering every ~100-150ms does
// routinely — and the bounded 128-entry queue backs up, then once full,
// tone()'s own xQueueSend(...,portMAX_DELAY) blocks the CALLING task
// (our own main loop) until space frees up. First found on Pac-Man's own
// chomp sound, reported live as "worked after restart, then only while
// holding a button, then stopped" — an intermittent-then-total freeze,
// not a crash. Passing NO duration makes the task's TONE_START handler
// skip delay(...) entirely and loop back for the next command
// immediately — no backlog possible — since NoteSequencer already
// tracks exactly how long each note should sound and calls tone() again
// (or noTone()) right on schedule. This exact combination (2-arg
// tone()/explicit noTone(), no duration ever) is CONFIRMED by the user
// to produce correct, reliable audible sound on the real board — don't
// change it without a live confirmation that whatever replaces it still
// makes sound, not just that it looks cleaner in code.
//
// (This core's noTone() is ALSO known to log a harmless-looking but
// very verbose "LEDC is not initialized" line on every call — chased
// twice this session trying to silence it (once with a fully separate
// raw-LEDC implementation, once by substituting `tone(pin, 0)` for
// noTone()) and BOTH attempts were followed by a live "there is no
// sound anymore" report, so both were reverted. Whatever is causing
// that log line, it is NOT safe to touch from here without audible
// confirmation each time — treat the log spam as a separate, lower-
// priority cosmetic issue and leave this file's actual tone()/noTone()
// calls alone.)
#pragma once
#include <Arduino.h>
#include "hardware.h"

namespace buzzer_audio {

struct Note {
    uint16_t freq;      // 0 = rest (silence for this slot)
    uint16_t durationMs;
};

// Every scene's sound goes through these two, never raw tone()/noTone()
// directly — this is the one place `audioMuted` (hardware.h) is
// checked, so muting the device silences every scene uniformly. When
// muted, buzzerTone() still calls noTone() (not a no-op) so a mid-note
// mute can't leave a tone stuck audible.
inline void buzzerTone(uint16_t freq) {
    if (audioMuted) noTone(PIN_BUZZER);
    else tone(PIN_BUZZER, freq);
}
inline void buzzerNoTone() {
    noTone(PIN_BUZZER);
}

// Advances a note table over successive update() calls without ever
// blocking the caller.
class NoteSequencer {
public:
    void play(const Note* notes, int count) {
        notes_ = notes;
        count_ = count;
        index_ = 0;
        noteStartMs_ = millis();
        applyCurrentNote();
    }

    void stop() {
        if (index_ >= 0) buzzerNoTone();
        index_ = -1;
    }

    bool isPlaying() const { return index_ >= 0; }

    void update() {
        if (index_ < 0) return;
        if (millis() - noteStartMs_ >= notes_[index_].durationMs) {
            ++index_;
            if (index_ >= count_) {
                buzzerNoTone();
                index_ = -1;
                return;
            }
            noteStartMs_ = millis();
            applyCurrentNote();
        }
    }

private:
    void applyCurrentNote() {
        if (notes_[index_].freq == 0) {
            buzzerNoTone();
        } else {
            buzzerTone(notes_[index_].freq);
        }
    }

    const Note* notes_ = nullptr;
    int count_ = 0;
    int index_ = -1;
    unsigned long noteStartMs_ = 0;
};

} // namespace buzzer_audio
