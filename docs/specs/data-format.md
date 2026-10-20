# Data format: characters, lines, scripted flows, reactions

All narrative data lives in `Content/Data/` as UTF-8 CSV (RFC 4180: comma
separated, fields containing commas, quotes or newlines are wrapped in double
quotes, embedded quotes doubled). The first row is the header. Column order is
free; columns are matched by header name (case-insensitive). Unknown columns
are ignored, so translators/writers may add `Notes` columns.

The game loads these files at runtime (they are staged as non-UFS files), so
editing a CSV and restarting the game is enough — no editor import step.

```
Content/Data/
  Characters.csv
  Script.csv
  Reactions.csv
  Lines/<culture>/Lines.csv     # en is the fallback culture
```

## Characters.csv

| Column | Meaning |
|---|---|
| `Id` | Stable id, lowercase (`akane`, `ren`, `mika`). |
| `Gender` | `Female` or `Male`. Romanceable opponents of each gender are offered on the title screen. |
| `Role` | `Opponent` (romanceable chess rival) or `Support`. |
| `Archetype` | Free text label (`Tsundere`, `Yandere`, ...), shown on the select screen. |
| `NameKey` | Line key for the display name. |
| `BlurbKey` | Line key for a one-sentence select-screen description. |
| `AccentColor` | `#RRGGBB` used for the name plate and UI accents. |
| `Age` | Integer, must be **18 or older** for every character. |
| `Model` | Optional package folder with `Body`, `Face`, `Hair` skeletal meshes (VRoid-style, `Fcl_*` face morphs). Empty = primitive stand-in. |
| `ModelScale` | Optional uniform scale for the model (default 1). |
| `Difficulty` | Default AI preset for an opponent: `Novice`, `Casual`, `Club`, `Strong`, `Champion`. |

## Lines/<culture>/Lines.csv

| Column | Meaning |
|---|---|
| `Key` | Line key. |
| `Text` | Display text. May contain `{player}` and `{opponent}` tokens. |

Lookup for a key `K` while the active opponent is `akane`:

1. `akane.K` in the current culture, then in `en`.
2. `K` in the current culture, then in `en`.
3. Missing → the key itself is shown wrapped in `!!` and a warning is logged.

Shared lines (UI, narrator, support cast) use plain keys. Opponent-specific
lines use `<opponentId>.<key>`, so one `Script.csv` drives both routes and each
opponent supplies their own voice for the same beat. UI keys start with `ui.`.

## Script.csv (scripted flows)

One row per step. Steps run top to bottom inside a scene unless redirected.

| Column | Meaning |
|---|---|
| `Scene` | Scene id. The game starts at scene `prologue`. |
| `Step` | Step id, unique within the scene; jump target for `Next`. |
| `Op` | Operation, see below. |
| `Speaker` | Character id, `{opponent}`, `player` or `narrator` (LINE only). |
| `TextKey` | Line key (LINE, CHOICE prompt, OPTION label). |
| `Emotion` | Opponent expression applied with the line, see *Emotions*. Empty = unchanged. |
| `Arg` | Op argument (see table). |
| `Next` | Jump target: `step` in the same scene or `scene:step` / `scene:` for another scene. Empty = following row. |

| Op | Behaviour |
|---|---|
| `LINE` | Show `TextKey` spoken by `Speaker`; waits for the player to advance. |
| `CHOICE` | Shows `TextKey` as an optional prompt plus the `OPTION` rows that immediately follow it. |
| `OPTION` | One choice. `Arg` = affection delta (integer, may be negative). Picking it jumps to `Next` (required). |
| `AFFECTION` | Add `Arg` (integer) to affection with the current opponent. |
| `FLAG` | Set flag `Arg` (a name) to true. |
| `IF` | If condition `Arg` holds, jump to `Next`; otherwise continue. Conditions: `affection>=N`, `affection<N`, `flag:name`, `!flag:name`, `result=win`, `result=lose`, `result=draw`. |
| `GOTO` | Jump to `Next`. |
| `CAMERA` | Linear camera transition. `Arg` = `conversation`, `closeup`, `board`, `wide`. |
| `CHESS` | Start a full chess game against the current opponent. `Arg` = player colour `white`, `black` or `random`. When the game ends, `result` is set and the script continues with the following row. |
| `END` | Show the relationship outcome screen. `Arg` = ending id; the ending title/body come from line keys `ending.<id>.title` and `ending.<id>.body` (opponent-prefixed lookup applies). |

Runtime semantics:

- Falling off the last row of a scene with no `Next`, `GOTO` or `END` is a script
  error: the game shows it and returns to the title. End every scene explicitly.
- Speaker display names: a character id shows that character's `NameKey`,
  `player` shows the player's chosen name, and `narrator` shows no name plate.
- `Emotion` always sets the **opponent's** expression, whoever is speaking.
  It is shared by both routes. For per-opponent nuance, put the feeling in the
  opponent-prefixed line text.
- A `CAMERA` step blends linearly over `CameraBlendSeconds` (Config/DefaultGame.ini)
  and does not wait for the player.

## Reactions.csv (live reactions during chess)

| Column | Meaning |
|---|---|
| `Character` | Opponent id, or `*` for a generic fallback. |
| `Trigger` | Trigger id, see below. |
| `TextKey` | Line key. |
| `Emotion` | Expression to show. |
| `Weight` | Relative pick weight (default 1). |

Triggers (derived from the board and Stockfish evaluation, from the opponent's
point of view):

| Trigger | When |
|---|---|
| `GameStart` | The match begins. |
| `Thinking` | The opponent starts thinking (sampled, not every move). |
| `PlayerBlunder` | Player's move lost ≥ 250 centipawns. |
| `PlayerMistake` | Player's move lost 100–249 centipawns. |
| `PlayerGoodMove` | Player's move gained ≥ 100 centipawns relative to expectation. |
| `PlayerCapture` | Player captured a piece. |
| `PlayerCaptureQueen` | Player captured the opponent's queen. |
| `OpponentCapture` | Opponent captured a piece. |
| `PlayerCheck` | Player gives check. |
| `OpponentCheck` | Opponent gives check. |
| `PlayerCastle` | Player castled. |
| `PlayerPromote` | Player promoted a pawn. |
| `PlayerWinning` | Evaluation crosses to ≥ 300 cp in the player's favour. |
| `OpponentWinning` | Evaluation crosses to ≥ 300 cp in the opponent's favour. |
| `Hint` | Player asked for a hint. |
| `PlayerWin` | Player checkmated the opponent (or the opponent resigned). |
| `OpponentWin` | Opponent won. |
| `Draw` | Game drawn. |

## Emotions

`Neutral`, `Happy`, `Smug`, `Annoyed`, `Flustered`, `Surprised`, `Sad`,
`Thinking`, `Determined`, `Dark` (yandere stare). Unknown values fall back to
`Neutral`.

## Content rules

- Every character is an adult university student (`Age` ≥ 18) and is written as one.
- Romance stays PG-13. Yandere and tsundere tropes are played for comedy and
  drama. No sexual content and no real violence or self-harm.
