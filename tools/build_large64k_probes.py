"""Generate short-line ASCII/CRLF NOTE probes, without copying reference MDX."""
from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parent.parent
out=root/'probes/large64k';out.mkdir(parents=True,exist_ok=True)
params=['31,0,3,6,0,28,0,1,0,0,0,','30,0,3,6,0,24,0,1,0,0,0,','28,18,3,9,11,9,0,0,0,0,0,','26,0,15,10,0,0,0,1,0,0,0,','3,0,15']
def voice(n):return [f'@{n}={{',*params,'}']
def notes(ch,n):
    # Explicit, alternating notes: no native repeats or macros shrink the data.
    return [ch+' '+''.join('c%1' if i%2==0 else 'd%1' for i in range(start,min(start+16,n))) for start in range(0,n,16)]
configs=[('K00SAFE',28000,'linear',1,1),('K01TAIL',30000,'linear',1,256),('K02PART',35000,'linear',1,1),('K03SUM',12000,'linear',4,1),('K04LOOP',18000,'loop',1,1),('K05LOOP',35000,'loop',1,1),('K06REPT',18000,'repeat',1,1),('K07ESC',18000,'escape',1,1)]
manifest=[]
for name,n,kind,tracks,voices in configs:
    title='NOTE 64K probe '+name
    lines=['; NOTE v0.8.5 large MDX format probe.', '; Compile with: note.x -m256 -v1 -1 '+name+'.MML', '; No -c, -cn or -z. Conversion only; playback is not needed.', '#title "'+title+'"']
    for number in range(voices):lines+=voice(number)
    if voices==256:
        for start in range(0,256,8):lines+=['A '+''.join('@'+str(i) for i in range(start,start+8))]
    else:
        for ch in 'ABCD'[:tracks]:lines+=[ch+' @0']
    if kind=='loop':lines+=['A L']
    if kind in ('repeat','escape'):lines+=['A [']
    if kind=='escape':lines+=notes('A',100)+['A /']
    for ch in 'ABCD'[:tracks]:lines+=notes(ch,n)
    if kind in ('repeat','escape'):lines+=['A ]2']
    raw=('\r\n'.join(lines)+'\r\n').encode('ascii')
    assert max(map(len,raw.splitlines()))<=80
    (out/(name+'.MML')).write_bytes(raw)
    # Conventional layout only: these are predictions, not measured NOTE output.
    track_sizes=[2]*9
    for i in range(tracks):track_sizes[i]+=2*n+(512 if voices==256 else 2)
    if kind=='loop':track_sizes[0]+=1 # F1 rel16 instead of F1 00
    if kind in ('repeat','escape'):track_sizes[0]+=6
    if kind=='escape':track_sizes[0]+=200+3
    voice_offset=20+sum(track_sizes)
    total=len(title)+4+voice_offset+voices*27
    offsets=[];at=20
    for size in track_sizes:offsets.append(at);at+=size
    manifest.append(dict(name=name,input_sha256=hashlib.sha256(raw).hexdigest(),input_bytes=len(raw),maximum_line_bytes=max(map(len,raw.splitlines())),note_arguments=['-m256','-v1','-1',name+'.MML'],conventional_layout_prediction=dict(file_bytes=total,voice_offset=voice_offset,track_offsets=offsets,voice_bytes=voices*27),actual_note_output='not yet received'))
for row in manifest:
    reference=root/'tests/large64k_reference'
    mdx=reference/(row['name']+'.mdx');log=reference/(row['name']+'.LOG')
    if log.exists():
        row['actual_note_output']='success' if mdx.exists() else 'rejected (see supplied log)'
        row['reference_log_sha256']=hashlib.sha256(log.read_bytes()).hexdigest()
        if mdx.exists():
            row['reference_bytes']=mdx.stat().st_size
            row['reference_sha256']=hashlib.sha256(mdx.read_bytes()).hexdigest()
(out/'PROBES.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
batch=['echo NOTE 64K format probes - conversion only']
for row in manifest:batch+=['echo '+row['name'],'note.x '+' '.join(row['note_arguments'])+' > '+row['name']+'.LOG']
(out/'RUNNOTE.BAT').write_bytes(('\r\n'.join(batch)+'\r\n').encode('ascii'))
print('Generated',len(manifest),'NOTE probes; conventional sizes:',[(m['name'],m['conventional_layout_prediction']['file_bytes']) for m in manifest])
