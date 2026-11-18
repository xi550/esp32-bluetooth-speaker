#!/usr/bin/env bash
set -e

project_path=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SDK_PATH=${SDK_PATH:-$project_path/sdk/esp-idf}
export IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-$HOME/.espressif}
python3 "$project_path/tools/check_sdk.py" "$SDK_PATH"
. "$SDK_PATH/tools/detect_python.sh"
"$ESP_PYTHON" "$SDK_PATH/tools/idf_tools.py" install cmake
bash "$SDK_PATH/install.sh" esp32
