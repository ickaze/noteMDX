"""IL real-song checks: conversion/structure and eleven established byte matches.

STAGE5/STAGE6 use explicit -c -z.
Usage: python tests/test_il.py /path/to/notemdx
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
exe = str(pathlib.Path(sys.argv[1]).resolve())
# Reuse the format validator without running the separate 33-fixture suite.
namespace = {'__file__': str(root/'tests/test_reference.py')}
code = (root/'tests/test_reference.py').read_text(encoding='utf-8').split('raw_matches = 0')[0]
exec(compile(code, 'test_reference.py', 'exec'), namespace)
layout, validate = namespace['layout'], namespace['validate']
exact = 0
with tempfile.TemporaryDirectory() as directory:
    for source in sorted((root/'tests/il_reference').glob('*.mus')):
        output = pathlib.Path(directory)/(source.stem+'.mdx')
        options = ['-c','-z'] if source.stem in ('STAGE5','STAGE6') else []
        run = subprocess.run([exe, *options, str(source), '-o', str(output)], capture_output=True)
        assert run.returncode == 0, (source.name, run.stderr.decode(errors='replace'))
        actual = output.read_bytes()
        validate(actual)
        assert actual == source.with_suffix('.MDX').read_bytes(), source.stem+' differs'
        exact += 1
        print(source.stem, len(actual), 'RAW MATCH')
assert exact == 11
print('11 conversions valid; 11/11 byte-exact')
