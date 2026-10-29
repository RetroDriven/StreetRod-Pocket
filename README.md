# Street Rod for Analogue Pocket

An openFPGA port of **Street Rod (1989)** and its **Special Edition (SE) data-disk cars**, maintained by **RetroDriven**. Built with the [openfpgaOS SDK](https://github.com/openfpgaOS/openfpgaSDK), using [Krzysztof Kania's reverse-engineered C engine](https://github.com/kylofon/street-rod-sdl3).

> **Bring your own original DOS game files.** Neither this repository nor the Pocket core release includes the original game executable, car libraries, or game resources.

## Games

| Pocket selection | DOS data you supply | Save location |
| --- | --- | --- |
| **Street Rod** | `SR.EXE`, `LIB1`, `LIB2`, `HOT_DATA` | `SR/streetrod_*.sav` |
| **Street Rod SE** | `SRSE.EXE`, `LIB1`, `LIB2`, `HOT_DATA` | `SE/streetrod_se_*.sav` |

Both editions use one Pocket core: `RetroDriven.Streetrod`. The Pocket game selector contains `Street Rod.json` and `Street Rod SE.json`.

## Install on Analogue Pocket

1. Download the **Latest Pocket release ZIP** from the repository's **Releases** tab.
2. Extract the files to the root of your Pocket SD card.
3. Put your own unmodified game files in the correct locations.(See below)
4. Select **openFPGA → Computer → Street Rod**, then choose **Street Rod** or **Street Rod SE**.

Expected locations on the SD card:

```text
Cores/RetroDriven.Streetrod/
  core.json
  icon.bin
Assets/streetrod/RetroDriven.Streetrod/
  Street Rod.json
  Street Rod SE.json
Assets/streetrod/common/
  SR.EXE
  LIB1
  LIB2
  HOT_DATA
  SRSE/
    SRSE.EXE
    LIB1
    LIB2
    HOT_DATA
Platforms/streetrod.json
Saves/streetrod/       # generated at runtime
```

Keep `SR.EXE` and `SRSE.EXE` filenames unchanged. Game save files are separate for both editions.

## Controls

See [CONTROLS.md](CONTROLS.md) for the cursor, on-screen keyboards and shortcuts.

## Current Status

- Street Rod and Street Rod SE are playable on physical Pocket hardware.
- Balanced 5:4 is the default presentation.
- Working driver saves, separate SR/SE save namespaces and missing-save RAM initialization.
- Pocket-native Memories/save states **are not supported at this time**. I will attempt to get this working but no promises.
- The runtime does not emulate a full DOS computer; it adapts a reverse-engineered C implementation to Pocket hardware.

## Credits and licensing

See [CREDITS.md](CREDITS.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The original game and artwork belong to their respective rights holders. The port-specific source is provided under [MIT](LICENSE), while the separately fetched upstream engine and SDK retain their own licenses.

This is an independent community project, not an official Analogue or Street Rod release.
