#!/usr/bin/env bash
set -e

project_path=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SDK_PATH=${SDK_PATH:-$project_path/sdk/esp-idf}
export IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-$HOME/.espressif}
python3 "$project_path/tools/check_sdk.py" "$SDK_PATH" --version-only
export IDF_PATH="$SDK_PATH"
. "$SDK_PATH/export.sh"
if [ "$#" -eq 0 ]; then set -- build; fi
exec python "$project_path/tools/run_idf.py" "$SDK_PATH/tools/idf.py" -C "$project_path/sdk/apps/esp_app" \
    -B "${BUILD_PATH:-$project_path/build/linux}" "$@"
