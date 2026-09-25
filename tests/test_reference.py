"""Compare the compiler with the user's 33 original NOTE outputs.

The EX-PCM fixture is preserved verbatim. Its missing E8 is inserted only
into an in-memory comparison copy, and is reported separately from raw matches.
"""
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
exe = str(pathlib.Path(sys.argv[1]).resolve())

def word(data, offset, signed=False):
    return int.from_bytes(data[offset:offset+2], 'big', signed=signed)

def layout(data):
    p = data.index(b'\r\n\x1a') + 3
    base = data.index(b'\0', p) + 1
    first = word(data, base+2)
    assert first in (20, 34), 'unexpected channel table size'
    count = first // 2 - 1
    offsets = [word(data, base+2+2*i) for i in range(count)]
    voice = word(data, base)
    return base, offsets, voice

def validate(data):
    base, offsets, voice = layout(data)
    assert base+voice <= len(data)
    assert (len(data)-base-voice) % 27 == 0
    assert offsets == sorted(offsets)
    if len(offsets) == 16:
        assert data[base+offsets[0]] == 0xe8, 'missing EX-PCM declaration'
    for channel, start in enumerate(offsets):
        end = offsets[channel+1] if channel+1 < len(offsets) else voice
        assert start < end <= voice
        p = base+start
        limit = base+end
        boundaries = set()
        branches = []
        stack = []
        ended = False
        while p < limit:
            boundaries.add(p)
            op = data[p]
            if op < 0x80:
                size = 1
            elif op < 0xe0:
                size = 2
            elif op == 0xf1:
                size = 2 if data[p+1] == 0 else 3
                if size == 3:
                    branches.append(p+3+word(data, p+1, True))
                assert p+size == limit
                ended = True
            elif op == 0xf6:
                assert data[p+1] > 0 and data[p+2] == 0
                stack.append((p, []))
                size = 3
            elif op == 0xf4:
                assert stack
                stack[-1][1].append((p, p+3+word(data,p+1)))
                size = 3
            elif op == 0xf5:
                assert stack
                start_loop, escapes = stack.pop()
                assert p+1+word(data,p+1,True) == start_loop+1
                for _, target in escapes:
                    assert target == p+1
                size = 3
            elif op in (0xec, 0xeb):
                size = 2 if data[p+1] >= 0x80 else 6
            elif op == 0xea:
                size = 2 if data[p+1] >= 0x80 else 6
            elif op in (0xf3, 0xf2, 0xfe):
                size = 3
            elif op in (0xff, 0xfd, 0xfc, 0xfb, 0xf8, 0xf0, 0xef, 0xed, 0xe9):
                size = 2
            elif op in (0xfa, 0xf9, 0xf7, 0xee, 0xe8):
                size = 1
            elif op == 0xe7 and data[p+1] == 1:
                size = 3
            else:
                raise AssertionError(f'unknown opcode {op:02x} at {p}')
            p += size
            assert p <= limit
        assert ended and not stack
        for destination in branches:
            assert destination in boundaries and destination < limit-3

raw_matches = 0
repaired_matches = 0
with tempfile.TemporaryDirectory() as directory:
    for source in sorted(p for category in ('priority','effects','followup','remaining') for p in (root/'probes'/category).glob('*.mml')):
        if source.parent.name=='remaining':
            copy=pathlib.Path(directory)/source.name
            shutil.copyfile(source,copy); source=copy
            if source.stem=='g01_load':
                # Load the original oracle banks, independently of our save output.
                for name in ('g00_ton.bin','g00_wav.bin'):
                    shutil.copyfile(root/'tests'/'reference'/name,pathlib.Path(directory)/name)
        output = pathlib.Path(directory)/(source.stem+'.mdx')
        run = subprocess.run([exe, str(source), '-o', str(output)], capture_output=True)
        assert run.returncode == 0, (source.name, run.stderr.decode(errors='replace'))
        if source.stem=='g00_save':
            assert b'pcmuse.map is not generated' in run.stderr
            assert not (pathlib.Path(directory)/'pcmuse.map').exists()
            for name in ('g00_ton.bin','g00_wav.bin'):
                assert (pathlib.Path(directory)/name).read_bytes()==(root/'tests'/'reference'/name).read_bytes(),name
                print(name, 'RAW MATCH')
        actual = output.read_bytes()
        original = (root/'tests'/'reference'/output.name).read_bytes()
        validate(actual)
        if source.stem in ('p11_ex','f00_ex0'):
            base, offsets, voice = layout(original)
            assert len(original) == base+voice-1
            insert = base+offsets[0]
            assert original[insert] == (0xef if source.stem=='p11_ex' else 0x2f)
            comparison = original[:insert]+b'\xe8'+original[insert:]
            assert actual != original and actual == comparison, 'EX-PCM correction differs'
            repaired_matches += 1
            label = 'MATCH after explicitly inserting missing E8 (raw reference differs)'
        else:
            assert actual == original, f'{source.name}: differs from original NOTE output'
            raw_matches += 1
            label = 'RAW MATCH'
        print(source.stem, len(actual), label, hashlib.sha256(actual).hexdigest())
assert raw_matches == 31 and repaired_matches == 2
print('31 byte-exact MDX matches; 2 byte-exact binary banks; 2 EX-PCM structural correction matches; all outputs structurally valid')
