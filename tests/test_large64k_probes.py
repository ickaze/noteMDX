"""Compare two original large-file MDX outputs and six rejection logs."""
import hashlib,json,pathlib,subprocess,sys,tempfile
root=pathlib.Path(__file__).resolve().parent.parent
folder=root/'probes/large64k';references=root/'tests/large64k_reference'
exe=str(pathlib.Path(sys.argv[1]).resolve())
rows=json.loads((folder/'PROBES.json').read_text(encoding='utf-8'))
expected={'K00SAFE':None,'K01TAIL':None,'K02PART':'MDX offset exceeds 16 bits','K03SUM':'MDX offset exceeds 16 bits','K04LOOP':'Infinite-loop displacement exceeds signed 16-bit','K05LOOP':'Infinite-loop displacement exceeds signed 16-bit','K06REPT':'Repeat body exceeds signed 16-bit','K07ESC':'Repeat body exceeds signed 16-bit'}
namespace={'__file__':str(root/'tests/test_reference.py')}
exec(compile((root/'tests/test_reference.py').read_text(encoding='utf-8').split('raw_matches = 0')[0],'test_reference.py','exec'),namespace)
results=[]
with tempfile.TemporaryDirectory() as directory:
 for row in rows:
    source=folder/(row['name']+'.MML');raw=source.read_bytes();raw.decode('ascii')
    assert hashlib.sha256(raw).hexdigest()==row['input_sha256']
    assert b'\n' not in raw.replace(b'\r\n',b'') and max(map(len,raw.splitlines()))<=80
    assert not any(x in raw.lower() for x in (b'#compress',b'#opt',b'#include',b'#pcmfile'))
    log=(references/(row['name']+'.LOG')).read_bytes().decode('cp932')
    out=pathlib.Path(directory)/(row['name']+'.mdx')
    r=subprocess.run([exe,'-m256',str(source),'--output',str(out)],capture_output=True,timeout=30)
    assert b'format guard remains' not in r.stderr
    if expected[row['name']] is None:
        assert r.returncode==0,r.stderr
        original=(references/(row['name']+'.mdx')).read_bytes()
        assert out.read_bytes()==original,row['name']
        namespace['validate'](original)
        assert len(original)==row['conventional_layout_prediction']['file_bytes']
        print(row['name'],len(original),'RAW MATCH',hashlib.sha256(original).hexdigest())
    else:
        assert r.returncode==1 and expected[row['name']].encode() in r.stderr,(row['name'],r.stderr)
        assert not out.exists()
        phrase='mml のサイズが大きすぎます' if row['name'] in ('K02PART','K03SUM') else 'ループ範囲が広すぎます'
        assert phrase in log,row['name']
        assert not (references/(row['name']+'.mdx')).exists()
        print(row['name'],'REJECTED as in supplied NOTE log (diagnostic text/position may differ)')
 # The budget and format constraints are deliberately independent.
 source=folder/'K01TAIL.MML';out=pathlib.Path(directory)/'limit.mdx'
 for options in ([],['-m64']):
    r=subprocess.run([exe,*options,str(source),'--output',str(out)],capture_output=True)
    assert r.returncode==1 and not out.exists()
 for options in (['-m66'],['-m128']):
    r=subprocess.run([exe,*options,str(source),'--output',str(out)],capture_output=True)
    assert r.returncode==0 and out.read_bytes()==(references/'K01TAIL.mdx').read_bytes()
    out.unlink()
assert rows[1]['conventional_layout_prediction']['file_bytes']>65536
assert max(rows[1]['conventional_layout_prediction']['track_offsets'])<65536
assert rows[1]['conventional_layout_prediction']['voice_offset']<65536
print('2 byte-exact original MDX matches; 6 original rejection outcomes confirmed; file-budget checks passed')
