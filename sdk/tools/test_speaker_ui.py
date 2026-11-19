"""在主机上用实际 LVGL 运行 UI 检查，并输出四个页面的 PPM 图像。"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path,
                        default=Path('E:/Codex/work/speaker-ui-test' if os.name == 'nt'
                                     else '/mnt/e/Codex/work/speaker-ui-test'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    ui = root / 'sdk/apps/esp_app/ui'
    lvgl = root / 'sdk/apps/esp_app/managed_components/lvgl__lvgl'
    if not (lvgl / 'CMakeLists.txt').exists():
        raise SystemExit('请先构建固件，下载锁定的 LVGL 组件')
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(speaker_ui_test C CXX)
set(LV_BUILD_CONF_PATH "{ui.as_posix()}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("{lvgl.as_posix()}" lvgl)
add_executable(check_ui "{root.as_posix()}/sdk/tools/test_speaker_ui.c"
    "{ui.as_posix()}/fonts/speaker_font_16.c")
target_include_directories(check_ui PRIVATE "{ui.as_posix()}")
target_link_libraries(check_ui PRIVATE lvgl m)
if(UNIX)
    target_compile_options(check_ui PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(check_ui PRIVATE -fsanitize=address,undefined)
endif()
''')
    build = work / 'build'
    subprocess.run(['cmake', '-S', str(work), '-B', str(build), '-G', 'Ninja'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'check_ui', '-j', '4'], check=True)
    executable = build / ('check_ui.exe' if os.name == 'nt' else 'check_ui')
    subprocess.run([str(executable)], cwd=work, check=True)


if __name__ == '__main__':
    main()
