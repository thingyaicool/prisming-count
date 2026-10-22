# Prism: build status

Working name. Rename freely (mod id / display name are not baked into the core).

## How the project is split

- `core/` is plain C++20 with no Geode or game dependency. It is compiled and tested on its own
  (`make test`: 212 checks, clean under AddressSanitizer, UBSan and ThreadSanitizer).
- `src/` (not written yet) will hold the Geode glue: hooks, UI nodes, the real `ISim`/`IRunner`
  implementations that drive Geometry Dash. It needs the Geode SDK and a running game to verify.

## Frame Counter (25) and Pathfinder (25): what exists

Legend: CORE = logic written and tested here. GLUE = needs Geode hooks / UI, not written yet.

Frame Counter
|  # | Feature                      | Status |
|----|------------------------------|--------|
|  1 | Live Frame Counter           | GLUE (reads sim frame) |
|  2 | Click Frame Windows          | CORE `frame_windows.hpp` |
|  3 | Frame Window Export          | CORE CSV + JSON |
|  4 | Frame Markers                | CORE `MarkerList` (UI = GLUE) |
|  5 | Frame Stepper                | GLUE |
|  6 | Native CBF Mode              | CORE `resolveInputStep` (hook = GLUE) |
|  7 | Custom TPS                   | CORE `StepScheduler` (hook = GLUE) |
|  8 | Custom FPS                   | CORE `StepScheduler`, `nearestDivisorFps` (hook = GLUE) |
|  9 | Seed Lock                    | GLUE |
| 10 | Latency Calibrator           | CORE `LatencyCalibrator` (measurement UI = GLUE) |
| 11 | Built-in Macro Recorder      | CORE `Macro` data model + file format (capture/replay = GLUE) |
| 12 | Frame-by-Frame Macro Editor  | CORE `MacroEditor` (timeline UI = GLUE) |
| 13 | Rewind and Re-record         | CORE `SnapshotStore` (state capture = GLUE) |
| 14 | Desync Detector              | CORE `firstDivergence` (trace capture = GLUE) |
| 15 | Physics Snapshot Diff        | CORE `diffTraces` (overlay = GLUE) |
| 16 | Consistency Map              | CORE buckets + color ramp (progress-bar drawing = GLUE) |
| 17 | Frame-Perfect Trainer        | CORE `Trainer` (HUD = GLUE) |
| 18 | Level Analyzer               | GLUE |
| 19 | Upcoming Strip               | GLUE |
| 20 | Lag Culprit Finder           | GLUE |
| 21 | Trigger Inspector            | GLUE |
| 22 | Fully Customizable UI        | GLUE |
| 23 | Settings Hub                 | GLUE |
| 24 | Command Palette              | CORE fuzzy matcher (popup UI = GLUE) |
| 25 | Leaderboard Guard            | CORE `LeaderboardGuard` (submission hook = GLUE) |

Pathfinder
Solver core (CORE, `pathfinder.hpp`): beam search, per-frame input choices, multi-route output,
max-clicks, CPS cap (min toggle gap), user hints, dual-player action bits, initial held input,
window-width ranking + humanness score, failure explainer, section stitching with anchored start
states, LRU solve cache, background job with cancel + time budget + memory guard.
Everything that needs the real game is GLUE: the `ISim` implementation over GD's physics,
gamemode / portal / orb handling (inherited from the real game step), ghost preview, risk heatmap,
route comparison, live coach, guide-mode marking UI, path-file sharing UI.

## Known design limits (stated up front)

1. `ISim::progress()` is the beam's ranking heuristic. In real GD, every route has the same x at the
   same frame, so the glue must return something more informative than raw x (e.g. clearance from
   the nearest hazard). The toy tests tie-break on click count instead.
2. Frame-window search assumes the success region around each click is contiguous.
3. Route history uses ~8 bytes per expanded node; `maxArenaNodes` guards memory.

## Not started

Modules 3, 5-20 (Level Lists, Auto Decorator, Training Coach, Stats Hub, Clip Studio, Themes,
Visual FX, Performance Lab, Level Browser Plus, Level Intel, Audio Toolkit, Goals, Share Hub,
Creator Tools, Accessibility, Scripting, Profiles/Cloud Sync) and all Geode glue.
Level Lists and Cloud Sync need a server if they are to sync between people/devices.

---
## Update: Geode glue, round 1 (written, NOT yet compiled)

| Frame Counter # | What was written | Files |
|---|---|---|
| 1 Live Frame Counter | HUD label (frame, time, FPS/TPS, REC/PLAY, guard badge) | `PlayLayerHooks.cpp` |
| 4 Frame Markers | "Mark" button stores the current frame | `PauseHooks.cpp` |
| 7 Custom TPS | `getModifiedDelta` hook (experimental) | `GameLayerHooks.cpp` |
| 8 Custom FPS | applied from settings | `PlayLayerHooks.cpp` |
| 11 Macro Recorder | record via `handleButton`, play via `processCommands`, save/load per level | `GameLayerHooks.cpp`, `PauseHooks.cpp`, `State.hpp` |
| 22 Customizable UI | HUD position / scale / opacity settings (drag editor not yet) | `mod.json` |
| 23 Settings Hub | Geode's built-in settings page (searchable hub not yet) | `mod.json` |
| 25 Leaderboard Guard | blocks progress saving when macro playback / custom TPS was active | `SubmitGuard.cpp` |

Still to write: Frame Window overlay + export UI, Native CBF, Seed Lock, Frame Stepper, Rewind,
Desync overlay, Consistency Map drawing, Trainer HUD, Level Analyzer, Upcoming Strip, Lag Culprit
Finder, Trigger Inspector, Command Palette UI, latency calibration UI, drag-to-place HUD editor.
Pathfinder: the in-game `ISim` (needs checkpoint-based state cloning; performance must be measured
in-game) and all its overlays.

Build/verify path: push to GitHub, let the Windows CI compile it against Geode v5.11.2, and send me
any compiler errors. Then run the first-run checklist in README.md.
