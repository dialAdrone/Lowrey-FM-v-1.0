# Lowrey FM v 1.0


Custom FM-1 firmware emulating my old Lowrey Teenie Geenie Organ with a bonus mode of some fun sounds: two banks of eight layerable voices, a real Mellotron flute (borrowed from the amazing fmel-1) Simple reverb & delay.

This was designed for personal use to be simple, musical and lofi, no sequencer, or screen diving!
<img width="480" height="480" alt="lowrey_fm" src="https://github.com/user-attachments/assets/3879d62c-2cde-412d-add2-260a1a111860" />
It is one C file, `main.c`, written for **[FM1 Studio](https://fm1studio.com)**.

## Install
Method ONE:
1. Open [fm1studio.com](https://fm1studio.com) in Chrome or Edge.
2. Paste the whole of `main.c` into the editor, replacing the demo.
3. Press **COMPILE** to try it in the simulator.
4. Press **DOWNLOAD** to get the `.fwsc` package, then **INSTALL** to write it to the FM-1 over USB.

To go back to the stock firmware, see FM1 Studio's notes on restoring M-VAVE's own package.

Method TWO: download that firmware file and use your prefered installer:
[Download the firmware (LowreyFM_v1.fwsc)](https://github.com/dialAdrone/Lowrey-FM-v-1.0/releases/download/firmware/LowreyFM_v1.fwsc)

## Voices

Each voice button turns its voice on or off. Voices stack, so you can layer as many as you like; the firmware lowers the number of notes you can hold when a heavy stack would overload the CPU.

**Bank A**

| Button | Voice | Icon |
| --- | --- | --- |
| ENV | FLUTE | crescent moon |
| LFO | HARPSI | flag |
| EDIT | HORN | daisy |
| GLO | REED | bubble |
| ARP | STRINGS | cigarette |
| SEQ | VOX | apple and worm |
| PLAY | VIBES | chimes |
| REC | BASS | truck |

**Bank B** (hold **HOME**)

| Button | Voice | Sound | Icon |
| --- | --- | --- | --- |
| ENV | LOWREY | the Lowrey organ flute, built from a real organ's harmonics | an eye that sheds a tear |
| LFO | CHIP | dial-up modem noise that loops forward and backward | a glitching floppy disk |
| EDIT | POCKET | lo-fi square-wave organ | a sun that sets into a moon while you play |
| GLO | PS-20 | breathy home-keyboard organ with vibrato | rolling waves |
| ARP | M.BOX | a soft electronic music box, an octave up | a rose dropping petals |
| SEQ | AOL | a whole dial-up call, always in tune with the notes you hold | a rotary phone being picked up |
| PLAY | WARM | a fat, mellow sine with a sub-octave | a police car that catches fire while you play |
| REC | HIGH | a soft pure sine an octave up | a spilling bottle |

## Controls

| Control | What it does |
| --- | --- |
| **HOME** tap / hold | home screen / switch between bank A and bank B |
| **SELECT** knob | home pages: voice cards, oscilloscope, truck scene (the bridge wobbles while you play) |
| **SAVE** tap | Mellotron screen: K1 flute level, K2 attack, K3 decay, K4 filter |
| **SAVE** hold | Mellotron flute on at 100% or off |
| **FX** | reverb screen, then delay screen, then home |
| **SCL** | HOLD (sustain) on or off |
| **OCT −/+** | octave down or up (MIDI notes always play at their own pitch) |
| **K1 / K2** | home: reverb / delay amount. Reverb screen: mix / decay. Delay screen: mix / time |
| **K3** | decay (on the delay screen: feedback) |
| **K4** | filter |
| **PRESET** knob | button colours |
| **ALGORITHM** knob | main screen colours |

MIDI in works over USB and TRS.

## Credits

- **[FM1 Studio](https://fm1studio.com)** by Tim Samshuijzen ([source](https://github.com/TimSamshuijzen/fm1studiocom), GPL-3.0), the editor, compiler, simulator and installer this firmware was written and built with, and its `fm1.h` API.
- **[Felucca](https://github.com/hugelton/Felucca)** and **[felucca-bsp](https://github.com/hugelton/felucca-bsp)** by Leo Kuroshita (@kurogedelic), Hügelton Instruments, the open FM-1 platform that `fm1.h` follows and that FM1 Studio's packages are built on, together with **[fm1-base](https://github.com/charlesvestal/fm1-base)** by Charles Vestal.
- **[FMEL-1](https://github.com/AntonioAltea/fmel-1)** by AntonioAltea (GPL-3.0), whose prepared Mellotron flute tapes (12 tapes, 11025 Hz, IMA ADPCM, loops) and playback approach are used for the Mellotron layer.
- **Taijiguy** (Bernie Kornowicz), for the Mellotron M400S flute recordings.
- **Freeverb** by Jezar at Dreampoint (public domain), the reverb design.
- The **M-VAVE FM-1**, the hardware. This project is not affiliated with M-VAVE.

## Licence

`main.c` is free software under the **GNU General Public License, version 3 only** (GPL-3.0-only); see [LICENSE](LICENSE). If you share a build of it, share its source too.

**One exception: the Mellotron flute samples are not GPL.** They are Taijiguy's recordings, "free to use, but cannot be used in any commercial, for profit software". So you may use, change and share this firmware freely, but **you may not sell it** or put it in anything sold.
