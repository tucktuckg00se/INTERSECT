#!/usr/bin/env python3
"""Extract an INTERSECT state blob from a preset or a REAPER project, for use as a test fixture.

    python3 extract_state.py <source> <out.bin> [--index N]

<source> is an .intersectpreset file or a REAPER .rpp / .rpp-bak project. --index picks the Nth
INTERSECT instance in a project (default 0). Writes <out.bin> plus <out.json> with what the
processor tests check: base state version, extension (RPM1) version, slice count, sample count.

Every saved sample path becomes /fixtures/sample-N.<ext> and every file name sample-N.<ext>, so
no local folder or sample names end up in the repo. JUCE writes strings null-terminated with no
length prefix, so a replacement of any length keeps the blob readable.

Standard library only.
"""

import argparse
import base64
import json
import re
import struct
import sys

PRESET_MAGIC = struct.pack('<i', 0x52505849)       # "IXPR"
EXTENSION_MAGIC = struct.pack('<i', 0x52504D31)    # "RPM1"
JUCE_PRIVATE_ID = b'JUCEPrivateData'
AUDIO_EXTENSIONS = rb'(?:wav|wave|aif|aiff|flac|mp3|ogg|rx2|rex)'
# A session-sample path follows its sampleId (a small int, so the byte before is 0x00).
PATH_RE = re.compile(rb'(?<=\x00)((?:/|[A-Za-z]:[\\/])[\x20-\xff]*?\.' + AUDIO_EXTENSIONS + rb')(?=\x00)',
                     re.IGNORECASE)


def from_preset(data):
    # IXPR container: magic, format version, plugin version string, int64 size, state blob.
    end_of_version = data.index(b'\0', 8) + 1
    size, = struct.unpack_from('<q', data, end_of_version)
    start = end_of_version + 8
    return data[start:start + size]


def from_reaper(data, index):
    text = data.decode('utf-8', 'replace')
    chunks = re.findall(r'<VST "VST3i?: INTERSECT[^\n]*\n(.*?)\n\s*>', text, re.S)
    if index >= len(chunks):
        sys.exit(f'only {len(chunks)} INTERSECT instance(s) in this project')
    # REAPER pads each base64 line on its own, so decode line by line.
    raw = b''.join(base64.b64decode(line + '=' * (-len(line) % 4))
                   for line in (l.strip() for l in chunks[index].splitlines())
                   if re.fullmatch(r'[A-Za-z0-9+/=]+', line))
    # The state starts with the version int, followed by the APVTS XML string.
    xml = raw.find(b'<?xml')
    if xml < 4:
        sys.exit('no INTERSECT state found in this chunk')
    start = xml - 4
    # JUCE's VST3 wrapper appends: int64 0, private ValueTree, int64 tree size, "JUCEPrivateData".
    # The tree's own type name is also "JUCEPrivateData", so only accept an occurrence whose
    # surrounding bytes match that layout (a size that points back to an int64 0 before the tree).
    ident = raw.find(JUCE_PRIVATE_ID, start)
    while ident >= 0:
        tree_size, = struct.unpack_from('<q', raw, ident - 8)
        tree_start = ident - 8 - tree_size
        if tree_size > 0 and tree_start - 8 >= start \
                and struct.unpack_from('<q', raw, tree_start - 8)[0] == 0 \
                and raw[tree_start:tree_start + len(JUCE_PRIVATE_ID)] == JUCE_PRIVATE_ID:
            return raw[start:tree_start - 8]
        ident = raw.find(JUCE_PRIVATE_ID, ident + 1)
    sys.exit('no JUCE private-data trailer found')


def neutralise_paths(blob):
    paths = list(dict.fromkeys(m.group(1) for m in PATH_RE.finditer(blob)))
    for n, path in enumerate(paths, start=1):
        base = re.split(rb'[\\/]', path)[-1]
        ext = base.rsplit(b'.', 1)[-1].lower()
        new_base = b'sample-%d.%s' % (n, ext)
        blob = blob.replace(path + b'\0', b'/fixtures/' + new_base + b'\0')
        blob = blob.replace(b'\0' + base + b'\0', b'\0' + new_base + b'\0')
        if path in blob or (b'\0' + base + b'\0') in blob:
            sys.exit(f'could not replace every copy of sample {n}')
    return blob, len(paths)


def describe(blob, num_samples):
    version, = struct.unpack_from('<i', blob, 0)
    xml_end = blob.index(b'\0', 4) + 1
    # zoom float, scroll float, selected slice int, midiSelectsSlice bool, root note int
    num_slices, = struct.unpack_from('<i', blob, xml_end + 4 + 4 + 4 + 1 + 4)
    ext = blob.rfind(EXTENSION_MAGIC)
    extension = struct.unpack_from('<i', blob, ext + 4)[0] if ext >= 0 else 0
    return {'stateVersion': version, 'extensionVersion': extension,
            'numSlices': num_slices, 'numSamples': num_samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('source')
    parser.add_argument('out')
    parser.add_argument('--index', type=int, default=0)
    args = parser.parse_args()

    data = open(args.source, 'rb').read()
    blob = from_preset(data) if data[:4] == PRESET_MAGIC else from_reaper(data, args.index)
    blob, num_samples = neutralise_paths(blob)
    info = describe(blob, num_samples)

    with open(args.out, 'wb') as f:
        f.write(blob)
    with open(re.sub(r'\.bin$', '', args.out) + '.json', 'w') as f:
        json.dump(info, f, indent=2)
        f.write('\n')
    print(args.out, info)


if __name__ == '__main__':
    main()
