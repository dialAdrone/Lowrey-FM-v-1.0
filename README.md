# Lowrey FM

A firmware for the M-VAVE FM-1 that turns it into a 70s home organ, built with FM1 Studio (`fm1.h`).

## What it does

- **Bank A, LOWREY:** flute, harpsichord, horn, reed, strings, vox, vibes and bass, stacked like organ
  stops, plus a Mellotron flute layer.
- **Bank B and Bank C:** more organ and character voices, including PIANO (a soft analog electric
  piano in the style of the late-70s electronic pianos) and M.BOX (a vibraphone with motor tremolo).
- **WAVES:** a wavetable synth.
- **POCKET:** after Critter & Guitari's Pocket Piano, with seven modes including its two-octave
  arpeggiator and octave cascade.
- **RHYTHM:** an organ rhythm unit with eight built-in beats (two can play at once), a drum sequencer,
  and an automatic bass: on bank A with BASS on and a beat running, the lowest octave of keys plays
  the bass on every kick, with only the mellow voices (no harpsichord or strings) held under it.
  The drums' volume and tone are remembered.
- **Also:** a note sequencer, MIDI in (single or multi-channel) and MIDI out, and USB audio, with an
  option to put the organ on the left channel and the drums on the right.

## POCKET knobs

Laid out like the other banks: K1 reverb, K2 the mode's first control (speed), K3 volume, K4 the
mode's second control (gate). The octave buttons tune. The eighth voice button is the Pocket Piano's
BUTTON (waveform, or octave jump).

## Drum sequencer

Hold SEQ on the RHYTHM bank. 16 steps, laid out on the 16 white keys, like a Volca:

- **Step mode (REC off):** SELECT picks a drum; the white keys turn its steps on and off and light up
  to show them. Steps 1, 5, 9 and 13 glow at half brightness; the playhead runs along the keys.
  Black keys play the picked drum.
- **Live mode (REC on):** the keys play the drums, recorded onto the nearest step while it runs.
- PLAY starts and stops, hold REC to clear, ALGORITHM sets the tempo, K2 the length.

## Getting around

| Hold | Goes to |
|---|---|
| HOME | Bank A |
| ENV | Bank B |
| LFO | Bank C |
| ARP | WAVES |
| SCL | POCKET |
| PLAY | RHYTHM |
| GLO | GLOBAL (MIDI and USB settings) |
| FX | Pictures |

SELECT steps through the banks. Tap a voice button to switch it on or off.

## Build

Open `main.c` in [FM1 Studio](https://fm1studio.com), then COMPILE, and DOWNLOAD or INSTALL.
Everything is synthesized in code except the Mellotron flute layer on bank A.

## Credits and licence

- GPL-3.0 (see `LICENSE`), built with FM1 Studio by Tim Samshuijzen.
- Mellotron M400S flute tapes: recorded by Taijiguy (Bernie Kornowicz), free to use but not in
  commercial software. Because of them, this firmware may be shared but not sold.
- The tape preparation and playback follow FMEL-1 by AntonioAltea (GPL-3.0).
- Reverb: the Freeverb design (Jezar at Dreampoint, public domain).
