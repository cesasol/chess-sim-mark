bk_client_version := "v1.13.6"

# Install the official BlenderKit client into bin/bk_client (required by the BlenderKit MCP)
install-bk-client:
    #!/usr/bin/env bash
    set -euo pipefail
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    arch=$(uname -m); [ "$arch" = aarch64 ] && arch=arm64
    bin="bk_client-linux-$arch"
    curl -fsSL -o "$tmp/bk_client.zip" "https://github.com/BlenderKit/bk_client/releases/download/{{ bk_client_version }}/bk_client.zip"
    unzip -q "$tmp/bk_client.zip" -d "$tmp/x"
    sha=$(jq -er --arg f "$bin" '.binaries[] | select(.filename == $f) | .sha256' "$tmp/x/manifest.json")
    echo "$sha  $tmp/x/$bin" | sha256sum -c -
    mkdir -p bin/bk_client
    # --remove-destination: replacing a running client fails with "Text file busy" otherwise
    cp -r --remove-destination "$tmp/x/$bin" "$tmp/x/tools" "$tmp/x/VERSION" "$tmp/x/manifest.json" bin/bk_client/
    chmod +x "bin/bk_client/$bin"
    echo "Installed $(cat bin/bk_client/VERSION). Start: ./bin/bk_client/$bin -single-instance"

ue := env_var_or_default("UE_ROOT", env_var("HOME") / "UnrealEngine")
project := justfile_directory() / "ChessSimMark.uproject"

# Compile the editor target (C++ module)
build-editor config="Development":
    "{{ ue }}/Engine/Build/BatchFiles/Linux/Build.sh" ChessSimMarkEditor Linux {{ config }} -Project="{{ project }}" -WaitMutex -NoHotReloadFromIDE

# Compile the standalone game target
build-game config="Development":
    "{{ ue }}/Engine/Build/BatchFiles/Linux/Build.sh" ChessSimMark Linux {{ config }} -Project="{{ project }}" -WaitMutex

# Build and run the engine-independent chess core tests (perft, SAN, game end)
test-core:
    mkdir -p Intermediate/CoreTests
    clang++ -std=c++20 -O2 -Wall -Wextra -Werror -ISource/ChessSimMark Source/ChessSimMark/Chess/ChessCore.cpp Tests/Core/ChessCoreTests.cpp -o Intermediate/CoreTests/chess_core_tests
    ./Intermediate/CoreTests/chess_core_tests

# Run Unreal automation tests (filter defaults to the whole ChessSim suite)
test-ue filter="ChessSim": build-editor
    "{{ ue }}/Engine/Binaries/Linux/UnrealEditor-Cmd" "{{ project }}" -ExecCmds="Automation RunTests {{ filter }}; Quit" -unattended -nullrhi -nosplash -nosound -stdout -testexit="Automation Test Queue Empty" -ReportExportPath="{{ justfile_directory() }}/Saved/TestReport" 2>&1 | grep -E "Test Completed|Result=|Error:|error:|Warning: .*ChessSim|LogAutomationController: (Error|Display: (Test|Found))" ; jq -e '.failed == 0' Saved/TestReport/index.json > /dev/null && echo "UE tests passed"

# Run every test suite
test: test-core validate-data test-ue

# Validate narrative CSVs against docs/specs/data-format.md
validate-data:
    python3 Tools/validate_data.py Content/Data

stockfish_tag := "sf_19"

# Opt-in: bundle the official Stockfish Linux binary (GPLv3, see docs/decisions/0001) into Content/ThirdParty
fetch-stockfish:
    #!/usr/bin/env bash
    set -euo pipefail
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    arch=$(uname -m); [ "$arch" = x86_64 ] && arch=x86-64
    gh release download {{ stockfish_tag }} --repo official-stockfish/Stockfish --pattern "stockfish-linux-$arch-universal.tar.gz" --dir "$tmp"
    tar -xzf "$tmp"/*.tar.gz -C "$tmp"
    dest=Content/ThirdParty/Stockfish/Linux
    mkdir -p "$dest"
    bin=$(find "$tmp" -type f -name 'stockfish*' -perm -u+x ! -name '*.tar.gz' | head -1)
    cp "$bin" "$dest/stockfish" && chmod +x "$dest/stockfish"
    find "$tmp" -iname 'Copying.txt' -exec cp {} "$dest/COPYING.txt" \; -quit
    echo "Stockfish {{ stockfish_tag }} source: https://github.com/official-stockfish/Stockfish/releases/tag/{{ stockfish_tag }}" > "$dest/SOURCE.txt"
    "$dest/stockfish" <<< "quit" | head -1

# Run an editor Python script headless (e.g. just editor-python Tools/build_map.py)
editor-python script:
    "{{ ue }}/Engine/Binaries/Linux/UnrealEditor-Cmd" "{{ project }}" -run=pythonscript -script="$(realpath {{ script }})" -unattended -nosplash -nosound -stdout 2>&1 | grep -E "Warning: (IMPORT|ASSET|MAP|INFO)|Error" | grep -v "LogLinux\|LogInit" || true

# Import third-party placeholder art (private/assets, see CREDITS.md) into /Game/Art
import-assets: build-editor
    just editor-python Tools/import_assets.py

# Generate materials and the club-room level from Tools/build_map.py
build-map: build-editor
    just editor-python Tools/build_map.py

# Play the full loop unattended (offscreen) and save screenshots to Saved/AutoPilot
autopilot opponent="akane" secs="900": build-editor
    rm -rf Saved/AutoPilot && mkdir -p Saved/AutoPilot
    timeout {{ secs }} "{{ ue }}/Engine/Binaries/Linux/UnrealEditor" "{{ project }}" -game -RenderOffScreen -ResX=1600 -ResY=900 -windowed -nosound -unattended -ChessSimAutoPilot={{ opponent }} -log=AutoPilot.log -stdout 2>&1 | grep -E "LogChessSim|Error:|Fatal" | grep -v "VeryVerbose" | tail -60
    ls Saved/AutoPilot

# Play the game windowed (uncooked, via the editor binary)
play: build-editor
    "{{ ue }}/Engine/Binaries/Linux/UnrealEditor" "{{ project }}" -game -windowed -ResX=1600 -ResY=900 -log

# Open the project in the Unreal Editor
editor: build-editor
    "{{ ue }}/Engine/Binaries/Linux/UnrealEditor" "{{ project }}"

# Cook + stage a Linux Development build into Packaged/
package:
    "{{ ue }}/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="{{ project }}" -platform=Linux -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="{{ justfile_directory() }}/Packaged" -unattended -utf8output

# Headless end-to-end smoke test: autopilot plays title -> story -> chess -> ending without a GPU
smoke opponent="ren": build-editor
    timeout 900 "{{ ue }}/Engine/Binaries/Linux/UnrealEditor" "{{ project }}" -game -nullrhi -nosound -unattended -ChessSimAutoPilot={{ opponent }} -log=Smoke.log -stdout > /dev/null 2>&1 || true
    grep -E "LogChessSim: (Warning|Error)|Script error" Saved/Logs/Smoke.log || true
    grep -q "AutoPilot finished: loop complete" Saved/Logs/Smoke.log && echo "smoke: loop complete ({{ opponent }})"

# One-shot setup after cloning (needs private/assets, see Tools/assets-manifest.json): build, import art, generate level
setup: build-editor import-assets build-map
