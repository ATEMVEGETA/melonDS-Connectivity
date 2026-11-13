# melonDS 1.1 Connectivity (Unofficial)

Based on official melonDS 1.1, commit
`b86390e4428bf38ce4c1ce0e9ca446d6d25955e8`.
This is a separate connectivity build, not an official melonDS release.
The Windows x64 build uses the normal melonDS interface and controls.
It does not contain the Playnite launcher's custom controls, menus or microphone hotkey.

## Connections

- Local multiplayer supports up to 100 emulated consoles in one process.
  Use **System > Multiplayer > Launch new instance**. Games still enforce their own player limits.
- Union Room routing keeps simultaneous parent sessions separate, including reused client IDs.
- Each console selects its own radio backend from the inserted cartridge when it boots or resets.
  The three Ranger titles use Ranger; other carts and empty firmware boots use Legacy.
  ROMs can be opened in any order, without restarting other consoles.
  Both backends carry the RF channel for local multiplayer, including command acknowledgments
  and default replies. The legacy receiver and its timer fix are unchanged.
- **Config > IR settings** retains the IR fork's Compatibility, Serial port, TCP and UDP choices.
  Compatibility is local IR between adjacent instances: 1/2, 3/4, and so on.
  UDP uses ENet, not arbitrary raw UDP packets.
- For PocketWalker, select **TCP > Server**, port **8081**, then connect PocketWalker to that port.
  The game can reach its waiting screen before PocketWalker starts; no blocking accept is used.
  Other TCP/ENet peers can use Client with an IPv4 host address and matching port.
- Serial adapters use 115200 baud, 8 data bits, no parity, one stop bit and no flow control.
  Serial timeout/delay controls remain available. Physical serial hardware has not been tested
  with this build; the missing-device and nonblocking paths have been tested.
- **Config > Wifi settings > Connect to Dolphin** matches the Wii-DS-Link interface.
  It defaults off and can be changed while that instance's game is stopped.
  Use the compatible Wii-DS-Link Dolphin build with its **Connect to melonDS** option.
  The bridge includes per-parent identity and the native beacon record-length fix.
  Stock Dolphin does not implement this companion bridge.

## Black 2 / White 2 Stall Fix

The stalled game continued running but waited for a wireless timer interrupt before saving.
Software could rearm the comparator for the current 1024-microsecond interval after the timer
had already checked that interval. The legacy backend now checks for that match when the
comparator is armed and issues IRQ14 if the radio and timer are enabled. Future matches and
the Ranger backend retain their existing behavior. No connection success or save completion
is forced, and guest RAM is not patched.

## Saves and Validation

Normal game save files use the stock save manager. Use separate save copies during validation.
Save names follow the instance slot: slot 1 uses `.sav`, slot 2 uses `.sav.2`, and so on.
The lowest free slot is reused after an instance closes. Opening a different ROM in a window
does not change its slot. If a numbered save does not exist, the emulator can load the plain
`.sav` as a starting point, but writes that window's progress to its numbered file.
Save states carry new connectivity/profile flags; old or incompatible states are rejected.
This includes states for games that previously inherited Ranger but now run under Legacy.
Use normal in-game saves when moving between emulator builds.

Automated checks cover 100-instance identities and routing, 50 isolated IR pairs, TCP/ENet
late connection and reconnection, absent PocketWalker/serial peers, backend selection,
80 backend transitions, timer ownership and the Black 2/White 2 timer regression, Wii bridge
lifecycle and native beacon lengths, actual frontend settings, clean-profile startup and shutdown.
The r2 tests also cover Legacy/Ranger beacon delivery and a synthetic guest powering off
followed by ROM reload in the same window and a Ranger-profile cartridge opening second.
The maintainer also confirmed the connectivity checks in games on Windows, including
a Ranger transfer with Ranger opened second and ROM reload after a Wii connection.
These results do not certify every game, region, or save combination. Linux/macOS builds
and physical serial hardware are not validated by the Windows test run.

## Credits and Source

- [melonDS team](https://github.com/melonDS-emu/melonDS): emulator and official frontend.
- [Ceravia's IR fork](https://github.com/Ceravia/melonDS-IR) and
  [DaveuRrr's IR fork](https://github.com/DaveuRrr/melonDS): IR support and IR settings UI.
  The imported dialog retains its BarretKlics attribution.
- [Wii-DS-Link](https://github.com/ATEMVEGETA/Wii-DS-Link): paired Wii connection UI and bridge.
- The preserved custom melonDS checkpoints supplied the tested dual-radio integration and
  timer fix; the standalone multiplayer checkpoint supplied the 100-instance and routing changes.

Original notices remain in the source. See [LICENSE](LICENSE), the [dependency
notices](licenses), and [RELEASE-BUILD.md](RELEASE-BUILD.md) for matching sources
and the static Windows build recipe. This source adds no Qt SerialPort dependency.
