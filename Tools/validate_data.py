#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate Content/Data CSVs against docs/specs/data-format.md.

Usage: python3 Tools/validate_data.py [Content/Data]
Exit code 0 when valid; prints every problem otherwise.
"""
import csv
import re
import sys
from pathlib import Path

OPS = {"LINE", "CHOICE", "OPTION", "AFFECTION", "FLAG", "IF", "GOTO", "CAMERA", "CHESS", "END"}
EMOTIONS = {"", "Neutral", "Happy", "Smug", "Annoyed", "Flustered", "Surprised", "Sad", "Thinking", "Determined", "Dark"}
TRIGGERS = {
    "GameStart", "Thinking", "PlayerBlunder", "PlayerMistake", "PlayerGoodMove", "PlayerCapture",
    "PlayerCaptureQueen", "OpponentCapture", "PlayerCheck", "OpponentCheck", "PlayerCastle", "PlayerPromote",
    "PlayerWinning", "OpponentWinning", "Hint", "PlayerWin", "OpponentWin", "Draw",
}
CAMERAS = {"conversation", "closeup", "board", "wide"}
DIFFICULTIES = {"Novice", "Casual", "Club", "Strong", "Champion"}
COND = re.compile(r"^(affection(>=|<)-?\d+|!?flag:[A-Za-z0-9_]+|result=(win|lose|draw))$")

errors: list[str] = []


def err(msg: str) -> None:
    errors.append(msg)


def read(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    return [{(k or "").strip().lower(): (v or "").strip() for k, v in r.items()} for r in rows]


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "Content/Data")
    chars = read(root / "Characters.csv")
    script = read(root / "Script.csv")
    reactions = read(root / "Reactions.csv")
    cultures = {p.name: read(p / "Lines.csv") for p in (root / "Lines").iterdir() if (p / "Lines.csv").exists()}
    if "en" not in cultures:
        err("Lines/en/Lines.csv missing")
        return report()

    lines = {c: {} for c in cultures}
    for c, rows in cultures.items():
        for r in rows:
            if r["key"] in lines[c]:
                err(f"[{c}] duplicate line key {r['key']}")
            lines[c][r["key"]] = r["text"]
    en = lines["en"]

    opponents = []
    ids = set()
    for r in chars:
        ids.add(r["id"])
        if int(r.get("age") or 0) < 18:
            err(f"character {r['id']}: age must be >= 18")
        if r["gender"] not in ("Female", "Male"):
            err(f"character {r['id']}: bad gender {r['gender']}")
        for k in ("namekey", "blurbkey"):
            if r.get(k) and r[k] not in en:
                err(f"character {r['id']}: missing line {r[k]}")
        if r["role"] == "Opponent":
            opponents.append(r["id"])
            if r.get("difficulty") not in DIFFICULTIES:
                err(f"character {r['id']}: bad difficulty {r.get('difficulty')}")
    genders = {r["gender"] for r in chars if r["role"] == "Opponent"}
    if genders != {"Female", "Male"}:
        err(f"need at least one Female and one Male opponent, got {genders}")

    def resolve(key: str, opponent: str) -> bool:
        return f"{opponent}.{key}" in en or key in en

    scenes: dict[str, list[dict]] = {}
    for r in script:
        scenes.setdefault(r["scene"], []).append(r)
    if "prologue" not in scenes:
        err("Script.csv: scene 'prologue' missing")

    def target_ok(scene: str, nxt: str) -> bool:
        if ":" in nxt:
            s, st = nxt.split(":", 1)
            return s in scenes and (st == "" or any(x["step"] == st for x in scenes[s]))
        return any(x["step"] == nxt for x in scenes[scene])

    for scene, rows in scenes.items():
        seen = set()
        for i, r in enumerate(rows):
            where = f"Script {scene}/{r['step']}"
            if not r["step"]:
                err(f"{where}: empty step id")
            if r["step"] in seen:
                err(f"{where}: duplicate step")
            seen.add(r["step"])
            op = r["op"]
            if op not in OPS:
                err(f"{where}: unknown op {op}")
            if r.get("emotion", "") not in EMOTIONS:
                err(f"{where}: unknown emotion {r['emotion']}")
            if r["next"] and not target_ok(scene, r["next"]):
                err(f"{where}: bad jump target {r['next']}")
            if op in ("LINE", "OPTION") or (op in ("CHOICE",) and r["textkey"]):
                for opp in opponents:
                    if not resolve(r["textkey"], opp):
                        err(f"{where}: line {r['textkey']} missing for opponent {opp}")
            if op == "LINE":
                sp = r["speaker"]
                if sp not in ids and sp not in ("{opponent}", "player", "narrator"):
                    err(f"{where}: unknown speaker {sp}")
            if op == "CHOICE":
                if i + 1 >= len(rows) or rows[i + 1]["op"] != "OPTION":
                    err(f"{where}: CHOICE must be followed by OPTION rows")
            if op == "OPTION":
                if not r["next"]:
                    err(f"{where}: OPTION needs Next")
                if not re.fullmatch(r"-?\d+", r["arg"] or "0"):
                    err(f"{where}: OPTION Arg must be an integer")
            if op == "AFFECTION" and not re.fullmatch(r"-?\d+", r["arg"]):
                err(f"{where}: AFFECTION Arg must be an integer")
            if op == "IF":
                if not COND.match(r["arg"]):
                    err(f"{where}: bad condition {r['arg']}")
                if not r["next"]:
                    err(f"{where}: IF needs Next")
            if op == "GOTO" and not r["next"]:
                err(f"{where}: GOTO needs Next")
            if op == "CAMERA" and r["arg"] not in CAMERAS:
                err(f"{where}: bad camera {r['arg']}")
            if op == "CHESS" and r["arg"] not in ("white", "black", "random"):
                err(f"{where}: bad colour {r['arg']}")
            if op == "END":
                for opp in opponents:
                    for part in ("title", "body"):
                        if not resolve(f"ending.{r['arg']}.{part}", opp):
                            err(f"{where}: ending.{r['arg']}.{part} missing for {opp}")

    for i, r in enumerate(reactions):
        where = f"Reactions row {i + 2}"
        if r["character"] != "*" and r["character"] not in opponents:
            err(f"{where}: unknown character {r['character']}")
        if r["trigger"] not in TRIGGERS:
            err(f"{where}: unknown trigger {r['trigger']}")
        if r.get("emotion", "") not in EMOTIONS:
            err(f"{where}: unknown emotion {r['emotion']}")
        if r["textkey"] not in en:
            err(f"{where}: missing line {r['textkey']}")
    for opp in opponents:
        have = {r["trigger"] for r in reactions if r["character"] in (opp, "*")}
        for t in sorted(TRIGGERS - have):
            err(f"reactions: opponent {opp} has no line for trigger {t}")

    for c, table in lines.items():
        if c == "en":
            continue
        missing = sorted(set(en) - set(table))
        if missing:
            print(f"note: culture {c} is missing {len(missing)} keys (falls back to en), e.g. {missing[:3]}")

    return report()


def report() -> int:
    for e in errors:
        print("ERROR", e)
    print(f"{len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
