# Loader menu

The game list, themes, cover art, and memory cards live in the loader. They do not change `nincfg.bin`'s layout. The kernel still decides which virtual card file to open from the Memory Card Emulation and Multi settings.

## What you see

* **Cover flow** is the default. Left and right flip games. A plays. B opens the old settings page. Y opens Options. Home goes back.
* **List** is the original text list. Y still opens Options.
* Options are saved in `loader.ini` next to `boot.dol`: `view`, `theme`, `covers`, and `art` (`cover` or `disc`).

## Themes

Built in names are `gamecube` and `classic`. Add another by creating `themes/<name>/theme.ini` next to `boot.dol`. Copy `nintendont/themes/example/theme.ini`. Colors are `RRGGBB` or `RRGGBBAA`. `background` is an optional PNG in that same folder.

Built-in themes are `fill_gamecube` and `fill_classic` in `loader/source/theme.c`.

## Covers

`loader/source/covers.c` has the folder list (`kCoverPatterns`). A new loader's cover directory is one more row. GameTDB is the download used when nothing local is found and Download covers is on. GameCube art is at `http://art.gametdb.com/wii/cover|disc/{region}/{ID6}.png`. The `/gc/` path on that server is empty, so downloads must use `wii`. Use HTTP. This loader does not complete that site's HTTPS handshake. Files are saved on the game device as `sd:/covers/<ID6>.png` or `usb:/covers/<ID6>.png`, and disc art in `discs` on that same device. Options switches which one is drawn. The menu prints the path it wrote. Pictures larger than 512 pixels on a side are skipped.

## Memory cards

Virtual cards are the `.raw` files in `/saves` on the game device:

| File | Who writes to it |
| --- | --- |
| `GMSE.raw` | Only the game whose ID is `GMSE`, when Multi is off. Slot B is `GMSE_B.raw`. |
| `ninmem.raw` | Every USA and PAL game, when Multi is on. Slot B is `ninmemb.raw`. |
| `ninmemj.raw` | Every Japan and Korea game, when Multi is on. Slot B is `ninmemjb.raw`. |

The card screen opens the file the selected game will actually use. Y, then the first task, switches that game between its own card and the shared card. Shared mode is global: every game of that region uses the same file. The kernel in `kernel/GCNCard.c` is what opens these names. Do not invent a third naming scheme without changing that file too.

On a Wii, Slot A or Slot B is listed only when a card is plugged in. If both slots are empty, the list is just the virtual cards, and a launch that was set to real cards uses the virtual card for that boot.

## Where to edit

| Change | File |
| --- | --- |
| Cover folders | `loader/source/covers.c` |
| Theme colors and `theme.ini` keys | `loader/source/theme.c` |
| Cover-flow layout | `loader/source/flow.c` |
| Card screen layout (`MC_*` positions) | `loader/source/memcard_mgr.c` |
| Options page | `loader/source/loader_menu.c` |
