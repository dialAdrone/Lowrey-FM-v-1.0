# How Lowrey FM v 1.0 is built

The whole firmware is one C file, **`main.c`**, written against the `fm1.h` API of
[FM1 Studio](https://fm1studio.com). FM1 Studio compiles it and wraps it in the platform that every
`fm1.h` program runs on. This page lists every part of the released package `LowreyFM_v1.fwsc`, where
its source is, and how to build the same package yourself.

## What is in the package

| Part | What it does | Source | Licence |
| --- | --- | --- | --- |
| `main.c` (this repository) | The whole firmware: voices, Mellotron layer, effects, screens | This repository | GPL-3.0-only |
| Mellotron flute tapes (inside `main.c`) | The flute samples of the Mellotron layer | Taijiguy's (Bernie Kornowicz) M400S recordings, as prepared by [FMEL-1](https://github.com/AntonioAltea/fmel-1) | Free to use, not in commercial or for-profit software |
| The `fm1.h` adapter | Gives a program the `fm1.h` API on the FM-1 | [FM1 Studio](https://github.com/TimSamshuijzen/fm1studiocom), `firmware/adapter` | GPL-3.0-only |
| fm1-base | Platform: screen, keys, LEDs, sound, USB MIDI, update entry, safe mode | [charlesvestal/fm1-base](https://github.com/charlesvestal/fm1-base), commit `c8e05997ce970875038156d2bbd69e712eac1543` | MPL-2.0 |
| felucca-bsp | Chip and board layer, and the update loader (`ota.bin`) | [hugelton/felucca-bsp](https://github.com/hugelton/felucca-bsp), commit `18eadadb8c364abd026accd2b1ea1c14ca347f06` | MPL-2.0 |
| Compiler | clang for the FM-1's processor (pi32v2) | [jvitkauskas/pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm), commit `60b0340f47083cf088903030a9b55d8a65525dde` (release `pi32v2-23.1.5-5`) | Apache-2.0 with LLVM exceptions |
| Three SDK files (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | Files every FM-1 package must carry | JieLi AC79 SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13`, [gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) | Apache-2.0 |

These are the versions that FM1 Studio's firmware compiler carries (its `src/fwsc/platform.json`,
listed in [src/fwsc/README.md](https://github.com/TimSamshuijzen/fm1studiocom/blob/main/src/fwsc/README.md)).
Only `main.c` was written for this firmware; everything else is used unchanged.

## Build it in the browser (how the release was made)

1. Open [fm1studio.com](https://fm1studio.com) in Chrome or Edge.
2. Make a new project, set its **NAME** to `LowreyFM_v1`, and paste in the whole of `main.c`.
3. Press **COMPILE** (it runs in the simulator), then **DOWNLOAD**.

The result is `LowreyFM_v1.fwsc`, with the package identity **`FM-1_58500`** (FM1 Studio makes the
identity from the project name) and felucca-bsp's update loader.

## Build it on your own computer

FM1 Studio's repository has the same build as a script, with JieLi's own compiler. Save the project as
a zip (FM1 Studio's **SAVE**, or a zip with `main.c` in it), then run, from a checkout of
[fm1studiocom](https://github.com/TimSamshuijzen/fm1studiocom):

```
firmware/build.sh PROJECT.zip          (Linux)
firmware\build.cmd PROJECT.zip         (Windows, in WSL)
```

See [firmware/README.md](https://github.com/TimSamshuijzen/fm1studiocom/blob/main/firmware/README.md)
there for what it needs.
