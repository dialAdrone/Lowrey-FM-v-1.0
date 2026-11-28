# Lowrey FM v 1.1

A firmware for the M-VAVE FM-1 that sounds like that f*cked up 70s home organ you found on the curb. includes the voices of a Lowrey Teenie Genie organ, an organ style drum machine, a Critter and Guitari pocket piano inspired synth, some lofi glitches, Yamaha PS-20 inspired sounds, and a mellotron flute sampled from original tapes. Designed for my own musical misadventures with an emphasis on the kind of sounds you might find at a thrift store if you're lucky. 

TOG organs were an early adventure in additive synthesis, you can layer the different tones on top of each other to create new sounds. Each image functions the same way the voice tabs do on an organ.

<img width="480" height="480" alt="lowrey_fm" src="https://github.com/user-attachments/assets/43de2e69-eab7-4bc2-b345-d0da87bead86" />

Based on the Lowrey Teenie Genie Organ and Critter and Guitari Pocket Piano. Built with FM1 Studio (`fm1.h`), flute pulled from the amazing FMEL-1 firmware.

## The Sounds

- **Bank A, is based on a Lowrey Teenie Genie top octave generator organ. :** flute, harpsichord, horn, reed, strings, vox, vibraphone and bass, stacked like organ stops, plus a Mellotron flute layer.
- **Bank B and Bank C:** Some glitchy sounds inspired by the band Broadcast, some softer sounds from the legendary Yamaha PS-20, a dial-up modem, and some other fun stuff..
- **WAVES:** a wavetable synth.
- **POCKET:** modeled after the early version of Critter & Guitari's Pocket Piano, with seven modes including its two-octave arpeggiator and octave cascade arpeggiator.
- **RHYTHM:** an organ rhythm unit with an automatic bass: on bank A with BASS on and a beat running, the lowest octave of keys plays the bass on every kick.
  The drums' volume and tone are remembered.
- **Also:** a note sequencer, MIDI in (single or multi-channel) and MIDI out, and USB audio, with an option to put the organ on the left channel and the drums on the right.

## Drum sequencer

Hold SEQ on the RHYTHM bank. 16 steps, laid out on the 16 white keys, like a Volca:

- **Step mode (REC off):** SELECT picks a drum; the white keys turn its steps on and off and light up
  to show them. Steps 1, 5, 9 and 13 glow at half brightness; the playhead runs along the keys.
  Black keys play the picked drum.
- **Live mode (REC on):** the keys play the drums, recorded onto the nearest step while it runs.
- PLAY starts and stops, hold REC to clear, ALGORITHM sets the tempo, K2 the length.

## Getting around

SELECT steps through all banks except the drums. Tap the corresponding 8 button on the right side of the FM-1 to turn voices on and off.

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


## Installing

Copy the text of [main.c](https://github.com/dialAdrone/Lowrey-FM-v-1.0/blob/main/main.c) into [FM1 Studio](https://fm1studio.com), then COMPILE, and DOWNLOAD or INSTALL.

or

download the firmware file and use your preferred firmware installer: 
https://github.com/dialAdrone/Lowrey-FM-v-1.0/releases/download/lowreyFM/lowrey_fm_v1_1.fwsc

The vibes are strong with this one! Install at your own risk! I have tested this on my own FM1 and play it daily.

## Credits and licence

- GPL-3.0 (see `LICENSE`), built with FM1 Studio by Tim Samshuijzen.
- Mellotron M400S flute tapes: recorded by Taijiguy (Bernie Kornowicz), free to use but not in
  commercial software. Because of them, this firmware may be shared but not sold.
- The tape preparation and playback follow FMEL-1 by AntonioAltea (GPL-3.0).
- Reverb: the Freeverb design (Jezar at Dreampoint, public domain).
- The Pocket Piano was created by Critter and Guitarri https://github.com/critterandguitari/201-PocketPiano
