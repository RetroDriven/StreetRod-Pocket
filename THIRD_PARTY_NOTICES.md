# Third-party licenses and notices

## Street Rod SDL3 reverse-engineered C engine

- Project: [kylofon/street-rod-sdl3](https://github.com/kylofon/street-rod-sdl3)
- Author and copyright: **Copyright (c) 2026 Krzysztof Kania**
- License: **MIT** — see [the upstream LICENSE](https://github.com/kylofon/street-rod-sdl3/blob/master/LICENSE).
- The build fetches the engine source as a separate Git checkout and adapts a *staged copy* for the Pocket. The port does not claim authorship of that game engine.
- When redistributing a combined engine binary or source, include the original MIT copyright and license notice. The build checks that the upstream license is present.

## openfpgaOS SDK and runtime

- Project: [openfpgaOS/openfpgaSDK](https://github.com/openfpgaOS/openfpgaSDK)
- License: **Apache-2.0**, including relevant dependency notices and license files in the SDK repository.
- Built Pocket packages use the SDK-provided FPGA runtime and loader. Respect the SDK's Apache-2.0 license and any additional notices when distributing compiled releases.

## Street Rod (1989) game

- Original developer: **Logical Design Works**.
- Original publisher/brand: **California Dreams**.
- Original executable, picture libraries, audio, text, gameplay art, and other DOS data are **not licensed by this project's MIT license**.
- This source archive does not include the commercial DOS executable/data or the original title artwork. Users must provide their own copies of `SR.EXE`, `LIB1`, `LIB2`, `HOT_DATA` (and their SE counterparts, if used).
- A neutral platform banner is generated automatically for a source build unless you supply a banner you have permission to use at `artwork/streetrod.bin`.

## Analogue Pocket

Analogue Pocket and openFPGA are trademarks/technologies of their respective owners. This project is not affiliated with or endorsed by Analogue or the original game's rights holders.
