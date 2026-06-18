# Motion-Triggered Audio Installation

An Arduino-based interactive art installation: a PIR motion sensor detects audiences and triggers preset audio playlists, output in stereo through a power amplifier and speakers.
> Inherited from an earlier version built by someone else (originally using a Wave Shield, mono only), the project has since been upgraded into a stereo system.

---

## Contents

- [How It Works](#how-it-works)
- [Bill of Materials](#bill-of-materials-bom)
- [SD Card Setup](#sd-card-setup)
- [Code Logic](#code-logic)
- [Enclosure & Waterproofing](#enclosure--waterproofing)
- [Known Issues](#known-issues)
- [Wiring](#wiring)

---

## How It Works

```
Audience passes by  →  PIR detects motion  →  sustained for 5 s (false-trigger gate)
                    →  Arduino enters PLAYING state  →  VS1053 plays audio
                    →  playback ends / times out  →  COOLDOWN  →  back to IDLE
```

- **LIST1**: 6 short tracks, played sequentially in a loop (`1.mp3` → `6.mp3`). Between each track has a 30-seconds COOLDOWN period.
- **LIST2**: 1 long track.
- Playback alternates between the two lists: LIST1 completes a full cycle or time out during COOLDOWN → switch to LIST2 → switch back to LIST1

---

## Bill of Materials

| Component | Model / Spec | Role | Notes |
|-----------|-------------|------|-------|
| Microcontroller | Arduino Uno | Runs the state machine, controls playback | Powered separately via USB / 5V |
| Audio decoder | Adafruit VS1053 Codec + MicroSD Breakout (#1381) | MP3 decoding + SD card reading | The audio core https://www.adafruit.com/product/1381|
| Motion sensor | Parallax PIR #555-28027 Rev B | Detects motion | Inherited from the previous project version |
| Amplifier | Pyle PLMRA400 (4-channel car amp) | Amplifies the audio signal | The LEVEL knob is input sensitivity, NOT volume https://www.amazon.com/dp/B000N5T0T4 |
| Speakers | AudioSource LS425 / 445 (8Ω passive) | Sound output | |
| Amp power | AOYADAISU 12V 10A switching supply | Powers the amplifier | Arduino is powered separately, not shared https://www.amazon.com/100V-240V-Adapter-Converter-Transformer-Printer/dp/B0D5CKC9V1/ |
| Wiring | 22–26 AWG stranded (sensor runs) / 14–16 AWG stranded (speaker runs) | | |

---

## SD Card Setup

1. **Format as FAT32**. 128GB cards default to exFAT so SD card with 32GB or less prefered; on Windows, use **guiformat** to reformat to FAT32.
2. Audio transcoding: use **ffmpeg** to convert to **stereo MP3, 320kbps**. WAV is playable but unreliable on the VS1053 (fragile parsing).
3. File structure:
   ```
   /LIST1/1.mp3  ...  /LIST1/6.mp3
   /LIST2/1.mp3
   ```
   Both LIST files can be added up to 99. **The numbering must follow a sequential order**.
4. File extensions must be **lowercase** `.mp3`; folder names must be uppercase.

---

## Code Logic

State machine: `IDLE → PLAYING → COOLDOWN`

- **IDLE**: waits for a PIR trigger; requires motion **sustained for 5 seconds** before entering PLAYING (polling-based, used to filter out momentary false triggers)
- **PLAYING**: plays in list order; after LIST1's six tracks complete, switches to LIST2, then back
- **COOLDOWN**: 30-second window after a track ends. Motion detected → resume immediately (next track, same list). Timeout → switch list, return to IDLE.
- **Playback watchdog**: if the PLAYING state persists beyond ~**11 minutes (660,000 ms)**, it force-stops and transitions to COOLDOWN. This is a safety net — when the SD card slot is loose, intermittent read errors can hang playback, and the watchdog brings the system back.

---

## Enclosure & Waterproofing

- Mounted in a metal enclosure **beneath** the outdoor platform structure → heat dissipation, waterproofing, and maintenance access are all ongoing challenges
- **Primary seal**: TAPEBEAR Butyl Tape (https://www.amazon.com/dp/B0B2D9CHK3)
- It is normal for butyl tape to become soft and sticky at high temperatures, and this generally does not indicate failure.
- If it flows and shifts, moving away from the original joint, a re-apply might be necessary.

---

## Known Issues

- [ ] **Loose SD card slot** (currently taped in place) — if fails in the future, it should be properly secured  using other methods.
- [ ] **Butyl tape heat-tackiness not permanently solved** — permanent options: switch to Permatex High-Temp RTV Silicone (non-tacky), or cover with aluminum foil tape; temporary mitigation: dust with talcum powder.
- [ ] **Low-frequency hum** — might be a ground loop issue; the potentional fix is a **ground loop isolator** between the VS1053 output and the amp's RCA input; not yet confirmed.

---

## Wiring

- For example,  refer to the Arduino documentation
https://learn.adafruit.com/adafruit-vs1053-mp3-aac-ogg-midi-wav-play-and-record-codec-tutorial

```
Arduino Side (powered by 5V / USB)
  ┌─────────────────────────────────────────────────────────────┐
  │                                                             │
  │   PARALLAX PIR #555-28027              ARDUINO UNO          │
  │   ┌──────────────┐                  ┌──────────────────┐    │
  │   │  VCC ────────┼──── 5V ──────────┤ 5V               │    │
  │   │  GND ────────┼──── GND ─────────┤ GND              │    │
  │   │  OUT ────────┼──── signal ──────┤ Pin 2            │    │  
  │   └──────────────┘                  │                  │    │
  │                                     │                  │    │
  │   ADAFRUIT VS1053                   │                  │    │
  │   ┌──────────────┐                  │                  │    │
  │   │  VCC ────────┼──── 5V ──────────┤ 5V               │    │
  │   │  GND ────────┼──── GND ─────────┤ GND              │    │
  │   │  SCLK ───────┼──────────────────┤ Pin 13           │    │
  │   │  MISO ───────┼──────────────────┤ Pin 12           │    │
  │   │  MOSI ───────┼──────────────────┤ Pin 11           │    │
  │   │  XCS  (CS) ──┼──────────────────┤ Pin 10           │    │
  │   │  XDCS (DCS) ─┼──────────────────┤ Pin 8            │    │
  │   │  DREQ ───────┼──────────────────┤ Pin 3            │    │
  │   │  RST  ───────┼──────────────────┤ Pin 9            │    │
  │   │  SDCS ───────┼──────────────────┤ Pin 4            │    │
  │   │              │                  └──────────────────┘    │
  │   │  [MicroSD]   │                                          │
  │   │              │                                          │
  │   │ 3.5mm OUT ●  │                                          │
  │   └──────┬───────┘                                          │
  └──────────┼──────────────────────────────────────────────────┘
             │
             │  3.5mm → Dual RCA cable
             │  White = L   Red = R
             ▼
                         Audio Side (powered using 12V 10A)
  ┌─────────────────────────────────────────────────────────────┐
  │   PYLE PLMRA400 Amplifier                                   │
  │   ┌───────────────────────────────────────────┐             │
  │   │  LOW INPUT  L ◄── white(L)                │             │
  │   │  LOW INPUT  R ◄── red(R)                  │             │
  │   │                                           │             │
  │   │  +12V ──────┬──────────── 12Vpower (+) ───┼──┐          │
  │   │  REM  ──────┘                             │  │ AOYADAISU│
  │   │  GND  ───────────────────── 12Vpower (−) ─┼──┘ 12V 10A  │
  │   │                                           │             │
  │   │  SPEAKER 1CH/L  (+ / −) ──► speaker 1     │             │
  │   │  SPEAKER 2CH/R  (+ / −) ──► speaker 2     │             │
  │   └───────────────────────────────────────────┘             │
  │                                                             │
  └─────────────────────────────────────────────────────────────┘
  
```