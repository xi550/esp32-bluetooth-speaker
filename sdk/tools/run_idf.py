"""Run IDF actions; a combined build/flash may skip an absent serial port."""
import argparse
import os
import subprocess
import sys

from serial.tools import list_ports


def port_key(port):
    if os.name == 'nt':
        return (port[4:] if port.startswith('\\\\.\\') else port).casefold()
    return os.path.realpath(port)


def run(idf, args):
    command = [sys.executable, idf]
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('-C', '--project-dir')
    parser.add_argument('-B', '--build-dir')
    parser.add_argument('-p', '--port', default=os.environ.get('ESPPORT'))
    options, actions = parser.parse_known_args(args)
    if actions[-2:] != ['build', 'flash']:
        return subprocess.call(command + args)

    common = []
    for flag, value in (('-C', options.project_dir), ('-B', options.build_dir), ('-p', options.port)):
        if value:
            common += [flag, value]
    result = subprocess.call(command + common + actions[:-1])
    if result:
        return result

    ports = {port_key(port.device) for port in list_ports.comports()}
    if not ports or (options.port and port_key(options.port) not in ports):
        print('Build succeeded; serial port {} not connected. Skipping flash.'.format(options.port or '(auto)'), flush=True)
        return 0
    return subprocess.call(command + common + actions[:-2] + ['flash'])


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit('Usage: run_idf.py <idf.py> [IDF arguments]')
    sys.exit(run(sys.argv[1], sys.argv[2:]))
