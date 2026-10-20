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
