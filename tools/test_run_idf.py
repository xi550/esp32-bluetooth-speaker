"""Run with the ESP-IDF environment's Python; no device is accessed."""
import os
from types import SimpleNamespace
from unittest.mock import patch

from run_idf import run


def main():
    port = 'COM5' if os.name == 'nt' else '/dev/ttyUSB0'
    missing = 'COM99' if os.name == 'nt' else '/dev/ttyUSB99'
    cases = [
        # arguments, connected ports, child results, expected exit, expected actions
        (['build', 'flash'], [], [0], 0, ['build']),
        (['build', 'flash'], [port], [7], 7, ['build']),
        (['build', 'flash'], [port], [0, 0], 0, ['build', 'flash']),
        (['build', 'flash'], [port], [0, 8], 8, ['build', 'flash']),
        (['-p', missing, 'build', 'flash'], [port], [0], 0, ['build']),
        (['--port=' + port, 'build', 'flash'], [port], [0, 0], 0, ['build', 'flash']),
        (['build', 'flash', '-p', port], [port], [0, 0], 0, ['build', 'flash']),
        (['flash'], [], [2], 2, ['flash']),
    ]
    with patch.dict(os.environ, {'ESPPORT': ''}):
        for args, ports, results, expected, actions in cases:
            with patch('run_idf.subprocess.call', side_effect=results) as child, \
                 patch('run_idf.list_ports.comports', return_value=[SimpleNamespace(device=p) for p in ports]) as scan:
                assert run('idf.py', ['-C', 'app', '-B', 'build'] + args) == expected
                assert [call.args[0][-1] for call in child.call_args_list] == actions
                if results[0] != 0:
                    scan.assert_not_called()
        with patch.dict(os.environ, {'ESPPORT': missing}), \
             patch('run_idf.subprocess.call', return_value=0) as child, \
             patch('run_idf.list_ports.comports', return_value=[SimpleNamespace(device=port)]):
            assert run('idf.py', ['build', 'flash']) == 0
            assert child.call_count == 1
    print('PASS: absent/configured ports, successful flash, build/flash failure propagation, explicit flash')


if __name__ == '__main__':
    main()
