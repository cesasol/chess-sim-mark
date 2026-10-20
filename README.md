# I Started as a Pawn

A 3D dating sim in Unreal Engine 5.8 where chess is how you win someone's heart.
It aims for an anime / Modern-JRPG presentation and lets you choose between
a female and a male opponent. See [IDEA.md](IDEA.md) for the vision.

**MVP status:** one complete loop with one opponent per route.

1. **Title / select:** enter your name, choose whose heart to challenge
   (Akane, tsundere ♀, or Ren, yandere ♂), pick an AI difficulty and a
   language (English or Spanish).
2. **Conversation:** a visual-novel scene in the university chess club. Your
   choices change affection, and the character reacts live with facial
   expressions, gaze, posture and lip flap.
3. **Chess:** a full standard game against Stockfish at your chosen strength.
   The opponent reacts live to blunders, good moves, captures, checks,
   castling, promotion and evaluation swings. Hints and resign are available.
4. **Outcome:** win and the story advances to an ending chosen by affection
   (`true_love`, `sweet_start`, `rivals`). Lose or draw and you get a rematch,
   as many times as you need.

All characters are adults (18+, enforced at data load). Romance stays PG-13.

## Requirements (Linux-first)

- Unreal Engine 5.8 at `~/UnrealEngine`, or set `UE_ROOT`
- [`just`](https://github.com/casey/just), `python3`, `clang++` (for the core tests)
- Stockfish on `PATH` (`pacman -S stockfish` / `apt install stockfish`), or run
  `just fetch-stockfish` to bundle the official binary. If neither is
  available, the game falls back to a built-in AI and says so.
- Placeholder art under `private/assets/` (gitignored, licences in
  [CREDITS.md](CREDITS.md)). Without it the game still runs, using
  basic-shape pieces and stand-in characters.

## Quick start

```sh
just setup               # build-editor + import-assets + build-map
just play                # run the game (windowed)
```

`Content/Art`, `Content/Materials` and `Content/Maps` are generated and are not
committed. `import-assets` reads `private/assets/`, which is listed in
[Tools/assets-manifest.json](Tools/assets-manifest.json) with source URLs and
licences. `build-map` regenerates materials and the level from
`Tools/build_map.py`.

| Recipe | What it does |
|---|---|
| `just test` | Runs every check: core chess tests, CSV validation, and Unreal automation tests |
| `just test-core` | Standalone chess rules tests (perft, SAN, draws) — no engine needed, ~1 s |
| `just validate-data` | Checks `Content/Data` CSVs against [the data spec](docs/specs/data-format.md) |
| `just test-ue` | Unreal automation tests `ChessSim.*`: UCI, live Stockfish, data, narrative playthrough, reactions |
| `just smoke [ren\|akane]` | Headless (`-nullrhi`) end-to-end run of the whole loop; passes when the loop completes |
| `just autopilot [akane\|ren]` | Plays the whole loop unattended offscreen and saves screenshots to `Saved/AutoPilot/` |
| `just editor` | Open the project in the Unreal Editor |
| `just package` | Cook and stage a Linux Development build into `Packaged/` |

## Controls (mouse and keyboard)

| Input | Action |
|---|---|
| Left click / Space / Enter | Advance dialogue; in chess, select a piece and then its destination |
| 1–4 | Pick a dialogue choice |
| H | Hint (highlights Stockfish's best move) |
| R, R | Resign (press twice) |
| Q / T / B / N | Promote to queen / rook / bishop / knight, or click the picker |
| Esc | On the ending screen, return to the title |

## Architecture

```
Source/ChessSimMark/
  Chess/ChessCore.*            engine-independent rules: FEN, legal moves, SAN, draws, fallback AI
  Chess/UciEngine.*            Stockfish child process over UCI pipes (non-blocking, game thread)
  Chess/ChessEngineSubsystem.* opponent engine (strength-limited) + analyst engine (full strength)
  Chess/ChessMatch.*           turn loop; turns eval swings into reaction triggers
  Chess/ChessBoardActor.*      3D board, piece animation, highlights, mouse picking
  Characters/                  opponent actor + native procedural anim instance (VRoid morphs)
  Data/                        RFC 4180 CSV reader, characters/lines/script/reactions, culture fallback
  Narrative/ScriptRunner.*     interpreter for Script.csv (lines, choices, branches, chess, endings)
  UI/ChessSimHud.*             all UI in code-built Slate
  Game/                        game mode (flow director) and player controller (input)
Content/Data/                  narrative CSVs (loaded at runtime, staged as loose files)
Tools/                         editor-Python import/map scripts, CSV validator
Tests/Core/                    standalone chess-core tests
```

Design records:

- [0001 Stockfish out of process / Apache-2.0](docs/decisions/0001-stockfish-out-of-process.md)
- [0002 Code-built presentation](docs/decisions/0002-code-built-presentation.md)

### Writing content

All story text, characters, scripted flows and reactions live in CSV under
`Content/Data/`. The format is in [docs/specs/data-format.md](docs/specs/data-format.md).
Edit the files, run `just validate-data`, then restart the game. No editor step
is needed. To add a language, create `Content/Data/Lines/<culture>/Lines.csv`;
missing keys fall back to English.

## Licensing

- Code: **Apache-2.0** ([LICENSE](LICENSE)). GPLv3 is not used, because the
  Unreal EULA §6(c) forbids combining Engine code with GPL. See ADR 0001.
- Stockfish (GPLv3) runs as a separate program over UCI and is never linked.
  **Get legal review before distributing a build that bundles it.**
- Placeholder art is CC0/CC-BY only; ShareAlike is excluded (also UE EULA §6(c)).
  Attributions are in [CREDITS.md](CREDITS.md). It will be replaced by custom
  assets.
