"""用固定的 Noto Sans SC 字体和 lv_font_conv 1.5.3 重新生成 UI 字库。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', required=True, type=Path)
    parser.add_argument('--converter', required=True, type=Path,
                        help='lv_font_conv.js 的完整路径；另需 node')
    args = parser.parse_args()
    package = json.loads((args.converter.parent / 'package.json').read_text())
    if package.get('version') != '1.5.3':
        raise SystemExit('请使用 lv_font_conv 1.5.3，拒绝覆盖项目字库')
    expected = '7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda'
    if hashlib.sha256(args.font.read_bytes()).hexdigest() != expected:
        raise SystemExit('字体 SHA256 不匹配，拒绝覆盖项目字库')
    output = Path(__file__).resolve().parents[1] / 'apps/esp_app/ui/fonts/speaker_font_16.c'
    subprocess.run(['node', str(args.converter), '--font', str(args.font), '--size', '16',
                    '--bpp', '2', '--format', 'lvgl', '--range',
                    '0x20-0x7e,0x3000-0x3030,0x3033-0x303f,0x4e00-0x9fff,0xff00-0xffef',
                    '--symbols', '·…□↑↓×', '--no-kerning', '--lv-include', 'lvgl.h',
                    '--lv-font-name', 'speaker_font_16', '-o', str(output)], check=True)
    content = re.sub(r'/\*.*?\*/', '', output.read_text(), flags=re.S)
    content = re.sub(r'\n\s*\n', '\n\n', content)
    output.write_text('/* clang-format off */\n/* 自动生成：16 像素、2 位灰度；来源及许可证见 OFL.txt，'
                      '重新生成方式见 docs/HANDOFF.txt。 */\n' + content + '\n/* clang-format on */\n')


if __name__ == '__main__':
    main()
