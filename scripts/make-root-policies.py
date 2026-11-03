#!/usr/bin/env python3
"""Sign the root windows of userdata-root.img with this board's owner key.

NativeUfs publishes a root window only when an owner-signed policy authenticates
it. A policy here is header-only: the signed header and its signature are linked
into the kernel, and the block hash table travels in the root image as a
NAME.vroot data object.

Reads the window layout from userdata-root.json. Writes:

  scripts/policies/ios-16.5-20F66/NAME.policy.bin   header and signature
  scripts/policies/ios-16.5-20F66/trusted-key.bin   public anchor
  .cache/verified-root/NAME.vroot                   hash table, for --object
  .cache/verified-root/owner.pem                    private key, never committed

Run before `unixnu build userdata --object`, which adds the tables without moving
the windows, so the signatures stay valid for the rebuilt image.

    scripts/make-root-policies.py --image out/.../userdata/userdata-root.img
"""
import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

DEVICE = Path(__file__).resolve().parents[1]
HEADER_AND_SIGNATURE = 160 + 384
GENERATION = 1


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Window:
    """A file-like view of one window, so the signer reads exactly its bytes."""

    def __init__(self, stream, offset, size):
        self.stream, self.left = stream, size
        stream.seek(offset)

    def read(self, count):
        data = self.stream.read(min(count, self.left))
        self.left -= len(data)
        return data


def owner_key(directory):
    private, public = directory / 'owner.pem', directory / 'owner-public.pem'
    if not private.exists():
        directory.mkdir(parents=True, exist_ok=True)
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072',
                        '-pkeyopt', 'rsa_keygen_pubexp:65537', '-out', str(private)], check=True, capture_output=True)
        private.chmod(0o600)
    subprocess.run(['openssl', 'rsa', '-in', str(private), '-pubout', '-out', str(public)],
                   check=True, capture_output=True)
    return private, public


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--kit', type=Path, required=True, help='kits/VerifiedRoot/tools/manifest.py')
    parser.add_argument('--uuid', action='append', required=True, metavar='NAME=UUID',
                        help='the volume UUID each window authenticates')
    args = parser.parse_args()

    report = json.loads(args.image.with_suffix('.json').read_text())
    uuids = dict(item.split('=', 1) for item in args.uuid)
    manifest = load('verified_manifest', args.kit)
    cache = DEVICE / '.cache/verified-root'
    policies = DEVICE / 'scripts/policies/ios-16.5-20F66'
    policies.mkdir(parents=True, exist_ok=True)
    private, public = owner_key(cache)

    with args.image.open('rb') as stream:
        for window in report['windows']:
            name, offset, size = window['name'], window['offset'], window['size']
            if name not in uuids:
                raise SystemExit(f'No volume UUID given for window {name}')
            work = cache / name
            record = manifest.make(Window(stream, offset, size), size, private, public,
                                   uuids[name], GENERATION, work)
            if record['image_sha256'] != window['sha256']:
                raise SystemExit(f'{name}: signed digest differs from the recorded window digest')
            envelope = (work / 'manifest.bin').read_bytes()
            (policies / f'{name}.policy.bin').write_bytes(envelope[:HEADER_AND_SIGNATURE])
            (cache / f'{name}.vroot').write_bytes(envelope[HEADER_AND_SIGNATURE:])
            (policies / 'trusted-key.bin').write_bytes((work / 'trusted-key.bin').read_bytes())
            (work / 'hashes.bin').unlink()
            (work / 'manifest.bin').unlink()
            print(f'{name}: {record["block_count"]} blocks, volume {record["volume_uuid"]}, '
                  f'table {record["block_count"] * 32} bytes')


if __name__ == '__main__':
    main()
