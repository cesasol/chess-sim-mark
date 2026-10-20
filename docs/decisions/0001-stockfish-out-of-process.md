# 0001 — Stockfish runs out of process over UCI; game code is Apache-2.0

- Status: accepted (MVP). **Requires legal review before any public distribution.**
- Date: 2026-10-20

## Context

IDEA.md asks for Stockfish "as a library" and for the code to be GPLv3 or
Apache, with a licensing check before the chess integration is committed.

Facts:

- Stockfish is GPLv3 (`stockfish` ships its `Copying.txt`).
- The Unreal Engine EULA, §6(c) "Non-Compatible Licenses", says you may not
  "combine, Distribute, or otherwise use the Licensed Technology with any code
  or other content which is covered by a license that would directly or
  indirectly require that all or part of the Licensed Technology be governed
  under any terms other than those of this Agreement". It names GPL, LGPL
  (except dynamic linking of a shared library) and **CC-BY-SA**.
  Source: <https://www.unrealengine.com/en-US/eula/unreal>, retrieved 2026-10-20.

Interpretation (not legal advice):

- Linking Stockfish into the game module, statically or dynamically, would make
  the combined work GPLv3. That conflicts with §6(c).
- Licensing *our* game module as GPLv3 hits the same conflict, because the
  module links against Engine code.
- Under the GPL FAQ's "mere aggregation" reading, separate programs that talk
  over pipes using a public text protocol are separate works. Chess GUIs
  conventionally ship Stockfish this way.

## Decision

1. Game code is licensed **Apache-2.0**. GPLv3 is not used.
2. Stockfish is never linked. `FUciEngine` launches it as a child process and
   speaks UCI over stdin/stdout (`Source/ChessSimMark/Chess/UciEngine.*`).
3. Binary lookup order: `CHESSSIM_STOCKFISH` env var →
   `UChessSimSettings::StockfishPath` → bundled
   `Content/ThirdParty/Stockfish/<Platform>/stockfish` → `PATH`.
4. For the Linux MVP the default is the **system package** (`/usr/bin/stockfish`),
   so we distribute nothing GPL. Bundling is opt-in (`just fetch-stockfish`).
   If we bundle, we ship Stockfish's licence plus a written source offer or the
   matching source tarball, and the binary stays unmodified.
5. If Stockfish is missing or crashes, the built-in alpha-beta fallback
   (`chesscore::PickFallbackMove`) keeps the game playable, and the UI says so.
6. Board rules (legality, SAN, game end) live in our own engine-independent
   `chesscore`. They do not depend on Stockfish being present.

## Consequences

- A bit more code: a process manager and protocol parser instead of direct calls.
- Process start and IPC cost a few milliseconds per request, which is negligible.
- A second "analyst" process is cheap, so live reactions run on a full-strength
  evaluation while the opponent plays at a limited strength.
- CC-BY-SA assets are banned in this project for the same §6(c) reason
  (see CREDITS.md).

## Alternatives considered

| Option | Why not |
|---|---|
| Link Stockfish as a library | GPLv3 combined work conflicts with UE EULA §6(c). |
| GPLv3 game code | Same conflict; the module links Engine code. |
| A permissive engine (e.g. a MIT-licensed one) in-process | Viable fallback if legal review rejects aggregation; costs strength and the "Stockfish" requirement. |
| Only our own engine | Far weaker; Stockfish analysis drives reactions/hints. |
