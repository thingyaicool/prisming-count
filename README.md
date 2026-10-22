# Prism (Geode mod for Geometry Dash 2.2081)

Working name. Windows, GD 2.2081, Geode v5.11.2.

## Build (no local setup needed)

1. Create a GitHub repo and push this whole folder (keep `.github/`).
2. Open the **Actions** tab. The "Build Prism (Windows)" run starts on push.
3. When it is green, download the **Build Output** artifact. It contains `local.prism.geode`.
4. Put that file in `<GD folder>\geode\mods\` and start the game.

If the build is red, open the failed step and send me the error text. I haven't been able to compile
the Geode side yet (the sandbox I work in has no Geode SDK), so the first CI run is the first real
compile. Expect a few API-name fixes.

Local build instead: install Geode CLI, `geode sdk install`, `geode sdk install-binaries`, then `geode build`.

## What works right now (once it compiles)

- **Frame counter HUD** in-level: frame, optional time, optional FPS/TPS, REC/PLAY status, guard badge.
  Position, scale and opacity are settings.
- **Macro recorder/player**: Pause menu buttons Rec / Play / Stop / Save / Load / Mark.
  Macros are saved per level in the mod's save folder (`macros/*.pmacro`).
- **Custom TPS** and **Custom FPS** (settings). EXPERIMENTAL.
- **Safe mode**: attempts that used macro playback or custom TPS don't save progress.

## First-run checklist (these are the assumptions I could not check)

Do these in order. If one fails, tell me which and what you saw.

1. **Mod loads**: Geode lists "Prism" with no error.
2. **A1, frame counter**: start any level. The "Frame N" counter should count up about 240 per second
   (a 30-second level ends near frame 7200). If it stays at 0 or counts in a different rate, the
   frame source is wrong.
3. **A2, recording**: Pause > Rec, restart the attempt, click a few times, pause > Stop. The HUD
   shows REC with a click count while recording.
4. **Playback**: Pause > Play, restart. The icon should repeat your clicks and the HUD shows PLAY n/N.
   If it is consistently off by one frame, tell me (the record/playback sampling point needs a shift).
5. **A4, guard**: after a playback attempt, finish a level. Progress should not be saved and the
   Geode log should say "Prism safe mode".
6. **A3, custom TPS**: only test in an offline/practice context. Turn on Custom TPS = 240 first. Behaviour
   should be identical to vanilla. Then try 480.
7. **FPS**: turn on Custom FPS = 144 and watch the HUD "Show FPS / TPS" reading.

## Layout

- `core/`: engine-independent logic, unit tested (`make test`).
- `src/`: Geode hooks and UI.
- `STATUS.md`: feature-by-feature status.
