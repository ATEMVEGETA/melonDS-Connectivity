# melonDS Connectivity

An unofficial Windows x64 build based on melonDS 1.1. It combines local wireless,
IR, and Wii connectivity.

**[Download melonDS 1.1 Connectivity r2](https://github.com/ATEMVEGETA/melonDS-Connectivity/releases/tag/v1.1-connectivity-r2)**

- Local IR for Pokemon Black 2 / White 2, including the fix for a stall before saving.
- Pokewalker support with nonblocking PocketWalker connection and an IR settings menu.
- Per-game wireless compatibility for Pokemon Ranger, Relocator, and tested event
  distribution cartridges. ROMs can open in either order.
- Union Room routing for independent local multiplayer groups and up to 100 local
  instances in one process. Each game still enforces its own player limit.
- Wii connectivity with the matching [Wii-DS-Link Dolphin build](https://github.com/ATEMVEGETA/Wii-DS-Link/releases/tag/v0.2.0-beta.4).
  Stock Dolphin does not implement this bridge. The Wii-DS-Link paired release is unchanged.

See [connection setup and save behavior](CONNECTIVITY.md), [Windows release build
instructions](RELEASE-BUILD.md), and [the upstream license](LICENSE). Keep backups of
in-game saves when switching builds. Savestates are not interchangeable across radio profiles.

Windows testing confirmed the Ranger transfer with Ranger opened second and ROM
reload after a Wii connection. Linux, macOS, physical serial adapters, and every
game or region are not claimed as tested. No games, BIOS, firmware dumps, or saves
are included.

This project is not affiliated with the melonDS or Dolphin teams or Nintendo.
