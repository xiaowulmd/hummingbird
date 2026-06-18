/****************************************************
  Hardware:
    - Arduino Uno
    - Adafruit VS1053 Codec Breakout (#1381)
    - Parallax PIR Sensor (#555-28027)

  Pin assignments:
    VS1053 RESET → Pin 9
    VS1053 CS    → Pin 10
    VS1053 DCS   → Pin 8
    VS1053 DREQ  → Pin 3
    SD CS        → Pin 4
    PIR OUT      → Pin 2
    SPI shared   → Pins 11 (MOSI), 12 (MISO), 13 (CLK)

  SD card structure:
    /LIST1/1.mp3  (track 1)
    /LIST1/2.mp3  (track 2)
    ...
    /LIST1/6.mp3  (track 6)
    /LIST2/1.mp3  (single 14-min file)

  Playback logic:
    - IDLE: PIR must be continuously HIGH for TRIGGER_DELAY_MS before
            playback starts; any drop to LOW resets the timer
    - PLAYING: play file, ignore PIR
    - COOLDOWN: 30s window after file ends
        PIR triggered → play next file immediately (no delay)
        timeout       → go to IDLE, keep list/track for next trigger
    - "No next file" = switch list (LIST1→LIST2 or LIST2→LIST1)

  v4 changes from v3:
    - Replaced interrupt-based PIR detection with digitalRead() polling
    - Added sustained-detection gate in IDLE: PIR must stay HIGH for
      TRIGGER_DELAY_MS (default 5s) before playback begins
    - COOLDOWN re-trigger remains immediate (person already engaged)

  v5 changes from v4:
    - Added playback timeout (PLAYBACK_TIMEOUT_MS, default 11 min)
    - If VS1053 stops reporting stopped() within timeout — e.g. due to
      SD card dropout or file read error — the state machine forces
      itself into COOLDOWN rather than freezing indefinitely
****************************************************/

#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>

// ----- Pin definitions -----
#define BREAKOUT_RESET  9
#define BREAKOUT_CS     10
#define BREAKOUT_DCS    8
#define DREQ            3
#define CARDCS          4
#define PIR_PIN         2

// ----- Timing -----
#define COOLDOWN_MS           30000UL   // 30s cooldown after track ends
#define WARMUP_MS             20000UL   // 20s PIR settle time at boot
#define TRIGGER_DELAY_MS       5000UL   // PIR must stay HIGH this long to trigger
                                        // playback from IDLE; lower = more sensitive
#define PLAYBACK_TIMEOUT_MS  660000UL   // 11 min max playback; safety net in case
                                        // VS1053 stalls and stopped() never fires

// ----- List sizes -----
const int LIST_COUNT[3] = { 0, 6, 1 };  // index 1=LIST1(6tracks), 2=LIST2(1track)

// ----- VS1053 -----
Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(BREAKOUT_RESET, BREAKOUT_CS, BREAKOUT_DCS, DREQ, CARDCS);

// ----- State machine -----
enum State { IDLE, PLAYING, COOLDOWN };
State state = IDLE;

// ----- Playback position -----
int currentList  = 1;   // 1 or 2
int currentTrack = 1;   // 1-based index within list

// ----- Cooldown timer -----
unsigned long cooldownStart = 0;

// ----- Did we already switch list at end of playlist? -----
bool switchedOnEnd = false;

// ----- Playback watchdog timer -----
// Set when playback begins; if PLAYBACK_TIMEOUT_MS elapses without
// stopped() firing, the state machine forces a COOLDOWN transition.
unsigned long playbackStart = 0;

// ----- Sustained-detection timer (IDLE only) -----
// Records when PIR first went HIGH in IDLE state.
// Reset to 0 whenever PIR drops LOW or playback starts.
unsigned long pirHighStart = 0;

// ---- Helper: build file path e.g. "/LIST1/3.mp3" ----
void buildPath(char* buf, int list, int track) {
  sprintf(buf, "/LIST%d/%d.mp3", list, track);
}

// ---- Helper: switch to other list, reset to track 1 ----
void switchList() {
  currentList  = (currentList == 1) ? 2 : 1;
  currentTrack = 1;
}

// ---- Start playing currentList / currentTrack ----
void startPlaying() {
  char path[20];
  buildPath(path, currentList, currentTrack);
  Serial.print(F("Playing: "));
  Serial.println(path);
  musicPlayer.startPlayingFile(path);
  pirHighStart  = 0;       // clear sustained-detection timer
  playbackStart = millis(); // start watchdog timer
  state = PLAYING;
}

// ====================================================
void setup() {
  Serial.begin(9600);
  Serial.println(F("Installation v5 starting..."));

  pinMode(PIR_PIN, INPUT);

  if (!musicPlayer.begin()) {
    Serial.println(F("VS1053 not found! Check wiring."));
    while (1);
  }
  Serial.println(F("VS1053 found."));

  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed! Check card and CARDCS pin."));
    while (1);
  }
  Serial.println(F("SD ready."));

  musicPlayer.setVolume(20, 20);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

  // ---- Warmup period ----
  Serial.print(F("Warming up "));
  Serial.print(WARMUP_MS / 1000);
  Serial.println(F("s..."));
  unsigned long warmupStart = millis();
  unsigned long nextTick    = warmupStart + 5000;
  while (millis() - warmupStart < WARMUP_MS) {
    if (millis() >= nextTick) {
      unsigned long remaining = (WARMUP_MS - (millis() - warmupStart)) / 1000;
      Serial.print(F("  "));
      Serial.print(remaining);
      Serial.println(F("s remaining..."));
      nextTick += 5000;
    }
  }

  Serial.println(F("Warmup complete. Waiting for motion..."));
}

// ====================================================
void loop() {
  bool pir = (digitalRead(PIR_PIN) == HIGH);

  switch (state) {

    // --------------------------------------------------
    case IDLE:
      if (pir) {
        if (pirHighStart == 0) {
          // First HIGH this cycle — start the timer
          pirHighStart = millis();
          Serial.println(F("Motion detected. Waiting for sustained signal..."));
        } else if (millis() - pirHighStart >= TRIGGER_DELAY_MS) {
          // PIR has been HIGH continuously for TRIGGER_DELAY_MS → trigger
          Serial.println(F("Sustained motion confirmed. Starting playback."));
          startPlaying();
        }
      } else {
        // PIR dropped LOW — reset timer if it was running
        if (pirHighStart != 0) {
          Serial.println(F("Motion lost before threshold. Resetting timer."));
          pirHighStart = 0;
        }
      }
      break;

    // --------------------------------------------------
    case PLAYING:
      // PIR during playback is ignored.
      if (musicPlayer.stopped()) {
        Serial.println(F("Playback finished. Entering cooldown."));

        if (currentTrack < LIST_COUNT[currentList]) {
          currentTrack++;
          switchedOnEnd = false;
          Serial.print(F("Next track ready: "));
          Serial.print(currentList);
          Serial.print(F("/"));
          Serial.println(currentTrack);
        } else {
          switchList();
          switchedOnEnd = true;
          Serial.print(F("End of list. Switched to LIST"));
          Serial.println(currentList);
        }

        cooldownStart = millis();
        state = COOLDOWN;

      } else if (millis() - playbackStart >= PLAYBACK_TIMEOUT_MS) {
        // VS1053 has been "playing" longer than any track should take.
        // Likely caused by SD card dropout or file read error.
        // Force-stop and fall through to COOLDOWN to keep installation alive.
        Serial.println(F("WARNING: Playback timeout. Forcing cooldown."));
        musicPlayer.stopPlaying();
        // Do NOT advance track — retry the same track next trigger.
        cooldownStart = millis();
        switchedOnEnd = false;
        state = COOLDOWN;
      }
      break;

    // --------------------------------------------------
    case COOLDOWN:
      // Re-trigger during cooldown is immediate (no sustained-detection delay)
      // because the person is already present and engaged.
      if (pir) {
        Serial.println(F("Motion during cooldown. Continuing playback."));
        startPlaying();
      } else if (millis() - cooldownStart >= COOLDOWN_MS) {
        if (!switchedOnEnd) {
          switchList();
          Serial.print(F("Cooldown expired. Switched to LIST"));
          Serial.println(currentList);
        } else {
          Serial.println(F("Cooldown expired. Back to IDLE."));
        }
        state = IDLE;
      }
      break;
  }

  delay(50);
}
