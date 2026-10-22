# Prism (Geode mod for Geometry Dash 2.2081)

Working name. Supported game version: Geometry Dash 2.2081. The build uses Geode SDK v5.11.2.

## Build and artifacts

Push changes to GitHub or manually start **Build Prism** from Actions. Core tests run first, followed
by three platform builds using Geode SDK v5.11.2:

| Artifact | Target | Intended platform |
|---|---|---|
| `Prism-Windows` | Win64 | Windows x86-64 |
| `Prism-Android64` | Android64 (`arm64-v8a`) | Android ARM 64-bit |
| `Prism-Android32` | Android32 (`armeabi-v7a`) | Android ARM 32-bit |

Open a successful workflow run, download the matching artifact, and extract the downloaded archive.
Install the `.geode` file using the Geode Android launcher, or copy it to
`Android/media/com.geode.launcher/game/geode/mods/`. Restart Geometry Dash or use Geode's reload
option after installation.

The official Geode build action maps Android64 to `arm64-v8a` and Android32 to `armeabi-v7a`; it
does not provide an x86_64 Android target. An Intel Chromebook can load one of these only if its
Android/Geode environment supports the corresponding ARM ABI or compatible translation. That has
not been verified on a connected device.

### Troubleshooting

- If a build fails, open the failed platform job and inspect its first CMake or compiler error. No
   platform build starts unless the test job succeeds.
- If Geode reports an unsupported platform or ABI, use the artifact matching the installed game's
   ABI. Prism currently has no x86_64 Android binary.
- If the mod does not load, verify Geometry Dash 2.2081 and Geode compatibility with SDK v5.11.2,
   install the extracted `.geode` file in Geode's mods folder, then restart/reload the game.
- A successful build confirms compilation and packaging only. It does not verify in-game behavior,
   macro timing, custom TPS/FPS behavior, or safe-mode behavior on a device.

For engine-independent tests, run `make test`. A local Geode build requires the Geode CLI, SDK, and
platform binaries installed for the desired target.

## Interface

The pause screen adds one compact **Prism** button. It opens a module hub with Frame Counter, Pathfinder,
and Settings entries. Frame Counter contains the existing Record / Play / Stop / Save / Load / Mark
controls; Settings opens Geode's settings page for Prism. Pathfinder currently exposes only its status:
the solver core exists, but an in-game Geometry Dash physics simulator and route UI are not implemented.

The in-game HUD can show an **FB COUNTER** label, latest physical click number/player/frame, and a
colored histogram of observed press-to-press gaps using the 0-1, 1-2, 2-3, 3-4, 4-5, 5-6, 6-8,
8-10, and 10+ frame bands. The adjacent millisecond value is estimated from the active TPS. This is
input history only; it does not determine a click's successful timing window. The concept image at
[`assets/prism-ui-concept.svg`](assets/prism-ui-concept.svg) illustrates the intended arrangement,
not a captured or verified game screen.

## Implemented in source (not yet verified in-game)

- **Frame counter HUD** in-level: frame, optional time, optional FPS/TPS, REC/PLAY status, guard badge.
  Position, scale and opacity are settings.
- **Macro recorder/player**: Pause menu buttons Rec / Play / Stop / Save / Load / Mark.
  Macros are saved per level in the mod's save folder (`macros/*.pmacro`).
- **Custom TPS** and **Custom FPS** (settings). EXPERIMENTAL.
- **Safe mode**: attempts that used macro playback or custom TPS don't save progress.

## In-game verification checklist

These behaviors still need testing in Geometry Dash on compatible platforms:

1. **Mod loads**: Geode lists "Prism" with no error.
2. **Frame counter**: start any level. The "Frame N" counter should count up about 240 per second
   (a 30-second level ends near frame 7200). If it stays at 0 or counts in a different rate, the
   frame source is wrong.
3. **Recording**: Pause > Rec, restart the attempt, click a few times, pause > Stop. The HUD
   shows REC with a click count while recording.
4. **Playback**: Pause > Play, restart. The icon should repeat your clicks and the HUD shows PLAY n/N.
   If it is consistently off by one frame, tell me (the record/playback sampling point needs a shift).
5. **Safe mode**: after a playback attempt, finish a level. Progress should not be saved and the
   Geode log should say "Prism safe mode".
6. **Custom TPS**: only test in an offline/practice context. Turn on Custom TPS = 240 first. Behaviour
   should be identical to vanilla. Then try 480.
7. **FPS**: turn on Custom FPS = 144 and watch the HUD "Show FPS / TPS" reading.

## Layout

- `core/`: engine-independent logic, unit tested (`make test`).
- `src/`: Geode hooks and UI.
- `STATUS.md`: feature-by-feature status.
