# Dragon Age Origins Recomp Android Duo

Dragon Age: Origins on the AYN Thor and its two screens (the app was Warden's Port
until 2026-11-01). Its package ids: `dev.daorecompandroid.duo` for debug builds and
`prod.daorecompandroid.duo` for release builds.

A static-recompilation port of the Xbox 360 game to an Android handheld. Version
**0.1.1-alpha, "Open Alpha"**: free software (GPL-3.0-or-later) in a public repository,
with a published APK that holds none of the game - the app makes the game's program on the
player's device from their own disc (PLAN Phase 12).
[`README.md`](README.md) is the user's manual. **To work on it, start with
[`RESUMING.md`](RESUMING.md)** (setup, the device, the daily loop, the next tasks), then
the header of [`STATUS.md`](STATUS.md) (**Next action**). A new machine is set up with
`python tools/bootstrap.py`.

## Standing instructions from the owner

- **Commit and push each finished piece of work without asking**, with the doc updates
  in the same commit: `STATUS.md` (header, phase board, §9 session log),
  `docs/PLAN.md`, `docs/BUGS.md` (every bug found, with what it causes), and
  `docs/IMPORTS.md`, `docs/TESTING.md`, `RESUMING.md`, `README.md` where they describe
  what changed. End commit messages with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Before every commit** this must print nothing - the repo never holds game material:
  `git diff --cached --name-only | grep -Ei '\.(iso|xex|xexp|erf|gda|tlk|apk|so)$|^TU_|^build/|^recomp/|^work/'`
- **The repository is public: nothing in it identifies anyone's private setup** - no
  server addresses, LAN IPs, device serials, account names in paths, or the games and
  apps people use. Machine-local settings go in gitignored files (for example
  `tools/game_check.local.txt`).
- **No online functionality in the game.** Network imports answer as a console with no
  cable; never open a host socket or resolve a name in the runtime. The app's one network
  use is the launcher's update check and download (`AppUpdate.java`, the owner's decision of
  2026-11-02, `docs/DECISIONS.md` D15): to GitHub, in the launcher only, sending nothing about
  the player. Nothing else may use the INTERNET permission.
- **A release that must not install itself** has `<!-- manual-update -->` in its notes.
- **Scope setup requests literally:** "install the tools" means the tools; ask before
  running the build or verification pipeline.
- **Never uninstall the app on the Thor** (`dev.daorecompandroid.duo`) - it deletes the
  6.4 GiB extracted game data and the saves. Install with `adb install -r -d`; a
  signing-key mismatch is fixed on the computer (`docs/SIGNING.md`), never by
  uninstalling.
- **On the owner's PC, check for a running game before any build** - Gradle,
  `bootstrap.py`, `recompile.py`, DAORecomp's installer - and ask before building if one
  is running. A launcher open with nothing launched is not a game.
  `python tools/game_check.py` looks the four ways (game libraries, launchers'
  children, GPU load and memory, windows) and names candidates.
- **DAORecomp's Windows build (the reference renderer for Phase 9) never disturbs the
  owner:** start it only with `python tools/dao_reference.py run ...`, which applies the
  same game check first and opens the game's window behind the active one
  (`DAO_BACKGROUND=1`), windowed, without its launcher or sound.
- Boot the device with `bash tools/boot_capture.sh <run> <settle> [--install]` as a
  short command of its own, and read `build/run<run>.log` with separate short greps.
- Bash heredocs mangle backslashes here (`"\n"` in C++ becomes a line break): edit
  source with the Edit tool or a script file written with the Write tool, and never run
  an edit and the Gradle build in the same parallel batch.
- The code is GPL-3.0-or-later (`LICENSE`); the game's material is never in the repo or
  an APK. The game's program (`libdaogame.so`) is made on the device, or pushed there from
  the computer for development, and never packaged; **only an APK that
  `python tools/release.py` passes may be published** (no game code or files, the device
  toolchain in it, the release key).
  The committed debug keystore (`app/debug.keystore`, password `android`) signs debug
  builds only; release builds are signed with the builder's own key, kept outside the
  repo at `~/.android/dao-release.jks` with its passwords in
  `~/.android/dao-release.properties` (`docs/SIGNING.md` §5; back both up - losing them
  means a release can never be updated in place).
