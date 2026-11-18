#!/usr/bin/env python3
"""Validate release metadata and print the matching changelog entry."""

import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', args.version):
        parser.error('version must be MAJOR.MINOR.PATCH')

    root = Path(__file__).resolve().parents[1]
    header = (root / 'src/version.h').read_text()
    match = re.search(r'^#define APP_VERSION\s+"([^"]+)"\s*$', header, re.M)
    if not match or match.group(1) != args.version:
        parser.error('requested release version does not match src/version.h')
    components = []
    for part in ('MAJOR', 'MINOR', 'PATCH'):
        match = re.search(r'^#define APP_VER_' + part + r'\s+(\d+)\s*$', header, re.M)
        if not match:
            parser.error('missing APP_VER_' + part)
        components.append(match.group(1))
    if '.'.join(components) != args.version:
        parser.error('APP_VER_* components disagree with APP_VERSION')

    lines = (root / 'CHANGELOG.md').read_text().splitlines()
    if not lines or lines[0] != '# ' + args.version:
        parser.error('the first changelog heading must match the release version')
    stop = next((i for i, line in enumerate(lines[1:], 1) if line.startswith('# ')), len(lines))
    notes = '\n'.join(lines[1:stop]).strip()
    if not notes:
        parser.error('the release changelog entry is empty')
    print(notes)
    if re.search(r'^#define APP_KIND\s+"proxy"', header, re.M):
        print('\nArchives include the TLS-enabled Stratum proxy and example configurations.')
    else:
        print('\nArchives include the CPU miner, daemon, CC server and example configurations.')
    print('SHA-256 checksums are provided in `SHA256SUMS.txt`.')


if __name__ == '__main__':
    main()
