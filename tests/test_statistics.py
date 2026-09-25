"""Independently execute MDX finite repeats and check the English CLI report.
Usage: python tests/test_statistics.py /path/to/notemdx
"""
import pathlib
import re
import subprocess
import sys
import tempfile

root=pathlib.Path(__file__).resolve().parent.parent
exe=str(pathlib.Path(sys.argv[1]).resolve())

def integer(b,p,signed=False):
    return int.from_bytes(b[p:p+2],'big',signed=signed)

def execute(b):
    p=0; ticks=0; bank=0; uses={}; stack=[]; first={}; steps=0
    loop=None; loop_target=None; passes=0; first_total=None
    while p<len(b):
        steps+=1
        assert steps<2000000, 'fixture execution limit'
        first.setdefault(p,ticks)
        op=b[p]
        if op<0x80: ticks+=op+1;p+=1
        elif op<0xe0:
            ticks+=b[p+1]+1
            uses.setdefault(bank,set()).add(op-0x80)
            p+=2
        elif op==0xf6: stack.append([b[p+1],p+3]);p+=3
        elif op==0xf4:
            if stack[-1][0]==1:
                stack.pop();p=p+3+integer(b,p+1)+2
            else:p+=3
        elif op==0xf5:
            stack[-1][0]-=1
            if stack[-1][0]:p=stack[-1][1]
            else:stack.pop();p+=3
        elif op==0xf1:
            assert not stack
            if b[p+1]==0:return ticks,None,uses
            if passes==0:
                loop_target=p+3+integer(b,p+1,True)
                first_total=ticks;loop=ticks-first[loop_target]
                passes=1;p=loop_target
            else:return first_total,loop,uses
        else:
            if op==0xfd:bank=b[p+1]
            if op in (0xec,0xeb,0xea):p+=2 if b[p+1]>=0x80 else 6
            elif op in (0xf3,0xf2,0xfe,0xe7):p+=3
            elif op in (0xff,0xfd,0xfc,0xfb,0xf8,0xf0,0xef,0xed,0xe9):p+=2
            else:p+=1
    raise AssertionError('no terminator')

def numbers(s):
    if s=='none':return set()
    out=set()
    for part in s.split(', '):
        endpoints=part.split('-')
        out.update(range(int(endpoints[0]),int(endpoints[-1])+1))
    return out

def verify(path,extra,output,allow_c=False):
    run=subprocess.run([exe,*extra,str(path),'-o',str(output)],capture_output=True,check=True)
    report=run.stdout.decode('utf-8')
    data=output.read_bytes()
    base=data.index(b'\r\n\x1a')+3;base=data.index(b'\0',base)+1
    voice=integer(data,base);count=integer(data,base+2)//2-1
    offsets=[integer(data,base+2+i*2) for i in range(count)]+[voice]
    total=[];loops=[];pcm={}
    for i in range(count):
        ticks,loop,uses=execute(data[base+offsets[i]:base+offsets[i+1]])
        total.append(ticks);loops.append('-' if loop is None else str(loop))
        if i>=8:
            for bank,notes in uses.items():pcm.setdefault(bank,set()).update(notes)
    total += [0]*(16-count);loops += ['-']*(16-count)
    reported_total=[int(x) for line in report.splitlines() if line.startswith('Total steps') for x in line.split(':')[1].split()]
    reported_loop=[x for line in report.splitlines() if line.startswith('Loop steps') for x in line.split(':')[1].split()]
    assert total==reported_total,(path,'total',total,reported_total)
    # C is deliberately absent from MDX. Its semantics have separate expected-value tests.
    if not allow_c: assert loops==reported_loop,(path,'loop',loops,reported_loop)
    reported_pcm={}
    for line in report.splitlines():
        if line.startswith('PCM bank'):
            m=re.fullmatch(r'PCM bank(\d+)\s+: (.*)',line);assert m,line
            used=numbers(m[2]);assert used<=set(range(96))
            reported_pcm[int(m[1])]=used
    assert pcm==reported_pcm,(path,'PCM',pcm,reported_pcm)
    fmline=next(line for line in report.splitlines() if line.startswith('FM voices'))
    fm_used=numbers(fmline.split(': ',1)[1].split(' / (')[0])
    assert '-' not in fmline.split(': ',1)[1] and '(' not in fmline
    assert fm_used==set(data[base+voice::27]),(path,'FM bank')
    for label in ('OPM track','PCM track','Total steps','Loop steps'):
        assert all(line.index(':')==12 for line in report.splitlines() if line.startswith(label))
    return report

with tempfile.TemporaryDirectory() as temp:
    tmp=pathlib.Path(temp)
    for source in sorted((root/'tests/il_reference').glob('*.mus')):
        extra=['-c','-z'] if source.stem in ('STAGE5','STAGE6') else []
        verify(source,extra,tmp/(source.stem+'.mdx'))
    fixture=tmp/'statistics.mml'
    fixture.write_bytes(b'#title Report\n#ex-pcm\nA [r%1/r%2]3 L {cde}4\nP [n0 @2 n1]3 L n2 @3 n3\nQ @255 [n95/n0]1\nR @9 r4\nW n1\n')
    verify(fixture,[],tmp/'statistics.mdx')
    verify(fixture,['--mute','AQW'],tmp/'muted.mdx')
    fixture.write_bytes(b'#title C\nA r%2 [r%3 C r%5]3\nB [r%2/C r%3]1\n')
    report=verify(fixture,[],tmp/'c.mdx',True)
    loopline=next(line for line in report.splitlines() if line.startswith('Loop steps'))
    assert loopline.split(':')[1].split()[:2]==['21','-']
    fixture.write_bytes(b'#title Huge\nA '+b'['*10+b'r%1'+b']255'*10+b'\n')
    result=subprocess.run([exe,str(fixture)],capture_output=True,check=True,timeout=5)
    assert b'overflow' in result.stdout
print('11 song reports and 4 focused statistics scenarios passed')
