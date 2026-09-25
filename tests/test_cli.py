"""Optional integration checks: python test_cli.py /path/to/notemdx(.exe)."""
import pathlib
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())

def run(*args):
    return subprocess.run([exe, *map(str, args)], capture_output=True)

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    source = root / '日本語.mml'
    output = root / '日本語.mdx'
    source.write_bytes('#title "日本語の曲"\nA c4\n'.encode('cp932'))
    assert run(source).returncode == 0
    assert output.read_bytes().startswith('日本語の曲'.encode('cp932') + b'\r\n\x1a')
    original = output.read_bytes()
    source.write_bytes(b'A q9\n')
    assert run(source).returncode == 1
    assert output.read_bytes() == original
    source.write_text('#title "UTF8日本語"\nA c4\n', encoding='utf-8')
    assert run('--encoding', 'utf8', source).returncode == 0
    assert output.read_bytes().startswith('UTF8日本語'.encode('cp932'))
    source.write_text('#title "BOM日本語"\nA c4\n', encoding='utf-8-sig')
    assert run(source).returncode == 0
    assert output.read_bytes().startswith('BOM日本語'.encode('cp932'))
    child = root / '音色.mml'
    child.write_bytes(b'i="c4"\n')
    source.write_bytes('#include "音色.mml"\nA i\n'.encode('cp932'))
    assert run(source).returncode == 0
    assert run('-o', source, source).returncode == 2
    before = child.read_bytes()
    assert run('-o', child, source).returncode == 1
    assert child.read_bytes() == before
    source.write_bytes('#include "日本語.mml"\n'.encode('cp932'))
    assert run(source).returncode == 1
    source.write_text('#title "😀"\nA c4\n', encoding='utf-8')
    assert run('--encoding', 'utf8', source).returncode == 2
    source.write_bytes(b'#play "echo SHOULD_NOT_RUN"\nA c4\n')
    proc = run(source)
    assert proc.returncode == 0 and b'SHOULD_NOT_RUN' not in proc.stdout
    source.write_bytes(b'A c4\n')
    assert run(source.with_suffix('')).returncode == 0
    source.write_bytes('#wavemem\n@w0={1,0,-32768,32767}\n#save-wave "波形.bin"\nA c\n'.encode('cp932'))
    assert run(source).returncode==0
    bank=root/'波形.bin'; assert bank.stat().st_size==131968
    before=bank.read_bytes(); mdx=output.read_bytes()
    source.write_bytes('#wavemem\n#load-wave "波形.bin"\n#save-wave "copy.bin"\nA c\n'.encode('cp932'))
    assert run(source).returncode==0 and (root/'copy.bin').read_bytes()==before
    source.write_bytes('#wavemem\n#load-wave "波形.bin"\n#save-wave "波形.bin"\nA c\n'.encode('cp932'))
    assert run(source).returncode==2 and bank.read_bytes()==before
    assert output.read_bytes()==mdx
    source.write_bytes('#save-tone "日本語.mml"\nA c\n'.encode('cp932'))
    text=source.read_bytes()
    assert run(source).returncode==2 and source.read_bytes()==text
    source.write_bytes(b'#save-tone "same.bin"\n#save-wave "same.bin"\nA c\n')
    assert run(source).returncode==2 and not (root/'same.bin').exists()
    source.write_bytes(b'#save-tone "ok.bin"\n#save-wave "absent/wave.bin"\nA c\n')
    assert run(source).returncode==2 and not (root/'ok.bin').exists()
    assert output.read_bytes()==mdx and not list(root.glob('*.notemdx.tmp'))
    damaged=bytearray(before);damaged[128:130]=b'\x02\x01';bank.write_bytes(damaged)
    source.write_bytes('#wavemem\n#load-wave "波形.bin"\n#save-tone "ok.bin"\nA c\n'.encode('cp932'))
    assert run(source).returncode==1 and not (root/'ok.bin').exists()
    assert output.read_bytes()==mdx
    source.write_bytes(b'A r%1 r%2 v3 v4 c4\n')
    assert run('-c','-z',source).returncode==0
    compact=output.read_bytes()
    assert run('--compress','rests','--opt','*',source).returncode==0
    assert output.read_bytes()==compact
    assert run('--compress','none','--opt','none',source).returncode==0
    assert len(output.read_bytes())==len(compact)+3
    source.write_bytes(b'#compress 1\nA c4&c4\n')
    assert run(source).returncode==0
    compact=output.read_bytes()
    assert run('--compress','none',source).returncode==0
    assert len(output.read_bytes())==len(compact)+3
    assert run('--compress','invalid',source).returncode==2
    assert run('--opt','bad',source).returncode==1
print('23 CLI integration scenarios passed')
