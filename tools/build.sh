#!/usr/bin/env bash
set -e

project_path=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SDK_PATH=${SDK_PATH:-$HOME/esp/esp-idf-v5.5.6}
export IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-$HOME/.espressif}
commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' "$project_path/sdk.lock.json")
if [ "$(git -C "$SDK_PATH" rev-parse HEAD)" != "$commit" ]; then
    echo 'SDK commit differs from sdk.lock.json' >&2
    exit 1
fi
. "$SDK_PATH/export.sh"
if [ "$#" -eq 0 ]; then set -- build; fi
exec python "$SDK_PATH/tools/idf.py" -C "$project_path" \
    -B "${BUILD_PATH:-$project_path/build/linux}" "$@"
