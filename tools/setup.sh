#!/usr/bin/env bash
set -e

project_path=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SDK_PATH=${SDK_PATH:-$HOME/esp/esp-idf-v5.5.6}
export IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-$HOME/.espressif}
read -r repository tag commit < <(python3 -c 'import json,sys; s=json.load(open(sys.argv[1])); print(s["repository"], s["tag"], s["commit"])' "$project_path/sdk.lock.json")

if [ ! -e "$SDK_PATH" ]; then
    mkdir -p -- "$(dirname -- "$SDK_PATH")"
    git clone --branch "$tag" --depth 1 --recursive --shallow-submodules "$repository" "$SDK_PATH"
fi
if [ "$(git -C "$SDK_PATH" rev-parse HEAD)" != "$commit" ]; then
    echo 'SDK commit differs from sdk.lock.json; no files overwritten' >&2
    exit 1
fi
git -C "$SDK_PATH" submodule update --init --recursive --depth 1
. "$SDK_PATH/tools/detect_python.sh"
"$ESP_PYTHON" "$SDK_PATH/tools/idf_tools.py" install cmake
bash "$SDK_PATH/install.sh" esp32
