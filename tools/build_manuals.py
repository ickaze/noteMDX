"""Build offline UTF-8 manuals from the bundled CP932 note.doc and English text."""
from pathlib import Path
import re,html,json
root=Path(__file__).resolve().parent.parent
e=html.escape
css='''
:root{color-scheme:light;--ink:#172b3d;--muted:#526775;--line:#d6e0e6;--accent:#086b85;--paper:#fff}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:1rem}body{margin:0;background:#edf2f5;color:var(--ink);font:16px/1.8 system-ui,"Yu Gothic",Meiryo,sans-serif}main{max-width:1140px;margin:auto;background:var(--paper);padding:44px 56px 80px}header{border-top:5px solid var(--accent);padding:18px 0 24px;border-bottom:1px solid var(--line)}h1{font-size:2.2rem;line-height:1.3;margin:.3em 0}h2{margin:3em 0 1em;border-bottom:2px solid var(--line);padding-bottom:.3em;font-size:1.55rem}h3{margin:2em 0 .6em;font-size:1.15rem}a{color:var(--accent);text-underline-offset:3px}p{margin:1em 0}nav{background:#f0f7f9;border:1px solid var(--line);padding:22px;margin-top:26px}nav ol{columns:2;column-gap:32px;margin:0;padding-left:24px}nav li{break-inside:avoid;margin:.3em 0}nav h2{margin:0 0 12px;border:0;font-size:1.1rem}.badge{font-size:.8rem;letter-spacing:.12em;color:var(--accent);font-weight:700}.languages{font-size:.9rem;margin:12px 0}.table-scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.94rem;margin:1.2em 0}th,td{border:1px solid var(--line);padding:10px 13px;text-align:left;vertical-align:top}th{background:#eaf2f5}tbody tr:nth-child(even){background:#f8fafb}code{font-family:Consolas,"Cascadia Mono",monospace;background:#edf3f5;padding:.1em .25em;border-radius:3px}pre{background:#f5f8fa;border-left:3px solid #adc9d3;padding:18px;overflow-x:auto;font:14px/1.8 Consolas,"Yu Gothic",Meiryo,monospace;tab-size:4}pre code{background:none;padding:0}.original{white-space:pre-wrap;overflow-wrap:anywhere;font:15px/1.85 system-ui,"Yu Gothic",Meiryo,sans-serif}.notice{background:#eef8f3;border-left:4px solid #368966;padding:16px 20px}.back{display:block;text-align:right;font-size:.85rem;margin-top:18px}details{border:1px solid var(--line);padding:12px 18px;margin:1em 0}summary{cursor:pointer;font-weight:600}.command-index{display:flex;gap:8px;flex-wrap:wrap}.command-index a{font:13px Consolas,monospace;padding:3px 7px;background:#edf3f5}.top{position:fixed;right:18px;bottom:18px;background:var(--accent);color:white;padding:8px 14px;border-radius:4px;text-decoration:none}@media(max-width:700px){main{padding:24px 18px 70px}h1{font-size:1.7rem}nav ol{columns:1}th,td{padding:8px}pre{padding:12px;font-size:12px}.original{font-size:14px}}@media print{body{background:white}main{max-width:none;padding:0}nav ol{columns:2}.top,.back{display:none}pre{white-space:pre-wrap}h2,h3{break-after:avoid}table{font-size:10pt}a{color:inherit}}
'''
def inline(s):
    parts=re.split(r'(`[^`]+`)',s)
    return ''.join('<code>'+e(x[1:-1])+'</code>' if x.startswith('`') else e(x) for x in parts)
def cells(s):
    result=[]; current=''; code=False
    for c in s.strip().strip('|'):
        if c=='`': code=not code
        if c=='|' and not code: result.append(current.strip());current=''
        else: current+=c
    result.append(current.strip());return result

def markdown(text):
    lines=text.splitlines();out=[];toc=[];i=0;heading=0
    while i<len(lines):
        line=lines[i]
        if not line.strip(): i+=1;continue
        if line.startswith('```'):
            block=[];i+=1
            while i<len(lines) and not lines[i].startswith('```'):block.append(lines[i]);i+=1
            out.append('<pre><code>'+e('\n'.join(block))+'</code></pre>');i+=1;continue
        m=re.match(r'^(#{1,3}) (.*)',line)
        if m:
            level=len(m[1]);heading+=1;anchor=f's{heading}'
            if level==1: i+=1;continue
            out.append(f'<h{level} id="{anchor}">{inline(m[2])}</h{level}>')
            if level==2:toc.append((anchor,m[2]))
            i+=1;continue
        if line.startswith('|') and i+1<len(lines) and re.match(r'^\|[-: |]+\|$',lines[i+1]):
            rows=[cells(line)];i+=2
            while i<len(lines) and lines[i].startswith('|'):rows.append(cells(lines[i]));i+=1
            n=len(rows[0]);assert all(len(row)==n for row in rows),rows
            out.append('<div class="table-scroll"><table><thead><tr>'+''.join('<th>'+inline(c)+'</th>' for c in rows[0])+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+inline(c)+'</td>' for c in row)+'</tr>' for row in rows[1:])+'</tbody></table></div>');continue
        block=[line];i+=1
        while i<len(lines) and lines[i].strip() and not lines[i].startswith(('#','```','|')):block.append(lines[i]);i+=1
        out.append('<p>'+inline(' '.join(block))+'</p>')
    return '\n'.join(out),toc

def page(lang,title,body,toc):
    label='目次 / Index' if lang=='ja' else 'Index'
    nav='<nav aria-label="Index"><h2>'+label+'</h2><ol>'+''.join(f'<li><a href="#{a}">{e(t)}</a></li>' for a,t in toc)+'</ol></nav>'
    return f'''<!doctype html>
<html lang="{lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>{e(title)}</title><style>{css}</style></head><body><main id="top"><header><div class="badge">NOTEMDX · VERSION 0.8 · REVISION 3 · DOCS 1 · 2026-09-26</div><h1>{e(title)}</h1><div class="languages"><a href="manual_ja.html" lang="ja">日本語</a> · <a href="manual_en.html" lang="en">English</a> · <a href="../note.doc">note.doc</a> · <a href="COMPATIBILITY.md">COMPATIBILITY.md</a> · <a href="API.md">API</a></div></header>{nav}{body}</main><a class="top" href="#top">↑ Index</a></body></html>'''

text=(root/'note.doc').read_bytes().decode('cp932').split('●え～と')[0]
parts=re.split(r'^\s*○\s*(.*?)\n',text,flags=re.M)
sections=list(zip(parts[1::2],parts[2::2]))
assert len(sections)==19
ja=[];toc=[]
def section(anchor,title,content):
    toc.append((anchor,title));ja.append(f'<section><h2 id="{anchor}">{e(title)}</h2>{content}<a class="back" href="#top">↑ 目次へ</a></section>')
section('about','この説明書について','<p>notemdx.exe 0.8 は、X68000のMXDRV2用MDXを生成するMMLコンバータです。note.docの技術説明を、索引付きで整理しました。現在の仕様との差は各項の補足を優先してください。原版全文は同梱のnote.doc（CP932テキスト）で確認できます。</p>')
usage='''
## 現在の使い方

```
notemdx.exe [options] input.mml
notemdx.exe -c -z STAGE5.mus
notemdx.exe --encoding utf8 --output song.mdx song.mus
```

拡張子省略時は.mml、.musの順に探します。出力省略時は入力名の拡張子を.mdxに変更します。原版同様、引数なしのスイッチは-xprのように併記できます。-i、-z、-t、-wの文字列引数は、そのスイッチ以降の文字列を引数として読み取ります。

|オプション|内容|
|---|---|
|`--output FILE`|MDXの出力先。旧版の-o FILEも判別可能な場合は受け付けます。引数なし-oは原版のPCMマップ指定です。|
|`--encoding auto / utf8 / cp932`|既定autoはBOM付きUTF-8、それ以外はCP932。includeにも適用。|
|`-x` / `--reverse-octave`|<と>の初期方向を反転。|
|`-p` / `--ex-pcm`|PCMをP〜Wの8トラックに拡張。|
|`-iCHANNELS` / `--mute CHANNELS`|変換しないトラック。例-iABPQ。小文字可。|
|`-c` / `--compress rests`|休符の圧縮。|
|`-cn` / `--compress notes`|休符と、タイで連続した同音程の圧縮。|
|`--compress none`|圧縮を無効化。|
|`-z` / `--opt "*"`|全項目の最適化。|
|`-zSELECTORS` / `--opt SELECTORS`|dvqpt012から選択。--opt noneで無効化。|
|`--help` / `--version`|ヘルプ／版表示。|

圧縮・最適化は既定では無効です。CLIまたはAPIで明示した設定は、MMLの#compress/#optに優先します。終了コードは成功0、変換エラー1、引数・入出力エラー2です。通常の変換エラーでは既存MDXを維持します。-r指定時は旧MDXを削除し、-e指定時は登録済みバンクを保存します。タイトルとPDX名はCP932に保存し、表現できない文字はエラーです。

## 原版スイッチとの対応

|原版のスイッチ|現在の対応|
|---|---|
|`-mSIZE`|ファイル全体の上限をKiB単位で指定。1〜65536、既定64。-m256で67,488バイトの元MDXと一致。トラック・音色開始位置の16bit制限は別途検査。|
|`-x` / `-p`|オクターブ方向反転／EX-PCM。対応済み。|
|`-iCHANNELS`|原版形式を復元。--mute CHANNELSは別名として維持。|
|`-r`|変換エラー時に既存MDXを削除。入力・include・読込バンクと同じファイルは削除しません。|
|`-b`|変換エラー時にビープ。WindowsはBeep、他環境は端末BEL。可聴かどうかは環境によります。|
|`-l` / 引数なし`-o`|認識して未対応警告。pcmuse.mapの生成／読込／論理和は行いません。-o単独では-lが必要なことも表示。|
|`-c[n]` / `-z[dvqpt012]`|圧縮／最適化。対応済み。|
|`-t[name]` / `-w[name]`|音色／波形バンクを保存。省略時tone.bin／wave.bin。CLI指定は#save-tone/#save-waveに優先。相対名は実行カレント基準。|
|`-v` / `-v0`|変換中トラックを表示。エラー行は対応端末で位置以降を赤色表示し、非対応端末・リダイレクトではASCII矢印。|
|`-v1`|変換中トラックとエラー行を表示し、位置の直前にASCIIの -> を挿入。|
|`-1`|最初の致命的エラーで中止。現実装は指定なしでも最初の致命的エラーで中止。|
|`-e`|エラー時にも要求された音色／波形バンクを保存。不完全なMDXは保存しません。最初のエラーまでの登録内容であり、エラー後の解析継続は未対応。|

-t/-wのファイル名はスイッチに続けて書きます（例-wcustom.bin）。別引数に分ける形式ではありません。-oの曖昧さを避け、MDX出力先には--outputを使用してください。互換用-o FILEは、入力指定後、次の名前が.mdx/.MDXの場合、または出力名・入力名が連続する場合に受け付けます。原版のPCM指定は-loの併記が明確です。

#remove/#beep/#verは引き続き警告して無視します。上記のCLIスイッチを指定してください。#playは実行しません。各スイッチの検査結果と対応限界はCLI_COMPATIBILITY.mdに記録しています。

'''
ub,_=markdown(usage)
original_usage=parts[0].split('●使用方法')[1].split('●文法について')[0]
original_usage=re.sub(r'note(?=\s*\[switch\])','notemdx.exe 0.8',original_usage)
original_usage='\n'.join(x for x in original_usage.splitlines() if '───' not in x).strip()
section('usage','使用方法と原版スイッチの対応',ub+'<details><summary>原版使用方法の説明（対応表を優先）</summary><p>以下は原版仕様の記録です。未対応スイッチは上表を参照してください。</p><pre class="original">'+e(original_usage)+'</pre></details>')
report='''<p>変換成功時、曲名、FM使用音色番号、使用中のPCMバンクごとの使用番号、OPM A〜H・PCM P〜Wの総ステップ数とL/Cからのステップ数を、桁を揃えた英語ラベルで表示します。曲名・ファイル名自体は原文を保持します。</p><p>FMはMDXに収録する選択音色を、範囲に省略せず個別に列挙します。PCMは発音するバンク内番号0〜95です。FM・PCMとも未使用番号は表示しません。音符のないバンクは表示しません。PDX内のサンプルの存在を調べる機能ではありません。</p><p>1ステップは四分音符の1/48。有限ループ・入れ子・最終回の/を反映します。Lは初回の終端までを総数として数え、L/Cから終端までをループ数とします。Cは最初に実行される位置を採用し、到達しないCはループ点なしです。ループ点なしは「-」、未使用・無効・ミュートトラックは0です。Wの同期待ちは含めません。ゲートで発音を短縮しても経過ステップ数は変わりません。64bit超過はoverflowと表示します。</p><p>PCMのL反復でバンクが持ち越される場合も使用番号に含めます。項目名の幅はTotal stepsの11文字に揃え、半角空白1つの後に:を置きます。PCM番号の連続範囲のみ0-3のように省略表示します。PCMバンク名はPCM bank0〜PCM bank255です。同じ情報はResult::statisticsから取得できます。</p>'''
# Pull an actual report, not a hand-maintained numeric example.
example=root/'docs/report_example.txt'
if example.exists():report+='<pre><code>'+e(example.read_text(encoding='utf-8'))+'</code></pre>'
section('report','変換結果の表示',report)
extra={
'識別子':'通常行の先頭がスペース・タブ等の無効な文字なら、その行全体がコメントです。複数行にまたがる定義の継続行は例外です。演奏行中は、有効な命令・マクロではない大文字から行末までコメントになります。引数や$・#で始まる構文はそれぞれの文法で解析します。',
'mml':'現在の制限：有限ループ内のL、連符の入れ子・波形効果・ループ関連命令、音色マクロ内のL/Cは使用できません。?内のLもエラーです。一部の定義・疑似命令をまたぐタイは未対応です。Cは実際のループを作らず、最初の通過位置からの集計に使用します。Cをまたぐ音長圧縮は行いません。',
'疑似命令':'#pcmlistとpcmuse.mapの読込・生成・論理和は未対応です。#pcmlistは警告して無視し、MDXは生成します。#play/#remove/#beep/#verも警告して無視します。以下の原版説明にある外部実行・旧MDX削除・ビープ・詳細色表示は実行しません。#save-tone/#save-waveを使って保存できますが、-t/-wも使え、CLI指定が疑似命令に優先します。CLIの相対名は実行カレント、疑似命令の相対名は命令を書いたファイルのフォルダから解決し、種類ごとに最後の保存名を採用します。',
'音符':'【原版からの機能拡張：連続臨時記号】notemdx.exe 0.8では音程に続く臨時記号を順にすべて処理します。最初の明示記号は調号に優先し、+ごとに半音上げ、-ごとに半音下げ、=または"で自然音へ戻します。d++はe（ダブルシャープ）、d--はc（ダブルフラット）、d+++はf、d+"はdです。通常音符・コード構成音・ポルタメント目標音で共通です。引用符によるマクロ本文では=を使用してください。',
'マクロ定義':'現在は入力累計8MiB、展開16MiBの既定上限があります。引用符で囲んだマクロは、対応する閉じ引用符までが本文です。閉じ引用符後の説明文は含みません。',
'波形エフェクト':'参照MDXに合わせ、音符数モードの正のディレイは無視します。波形による分割は通常のMDX命令で表現し、独自の拡張命令は追加しません。',
'波形メモリ定義':'現在の位相が範囲外になるよう動作中の波形を短く再定義する場合は、先に効果をOFFにしてください。',
'グライド':'GLON再開時の整数半音グライドでF3設定・復帰も出す挙動は、参照MDXに合わせています。詳細はCOMPATIBILITY.mdとIL_VALIDATION.mdを参照してください。',
}
for idx,(title,body) in enumerate(sections):
    title=title.strip();anchor=f'ref{idx+1}'
    body='\n'.join(x for x in body.splitlines() if '───' not in x)
    body=re.sub(r'\bnote(?:\.x)?\b','notemdx.exe 0.8',body)
    chunks=[]
    if title in extra:chunks.append('<div class="notice">'+e(extra[title])+'</div>')
    if title in ('mml','疑似命令'):
        # Original command heading lines have exactly two leading tabs.
        command_blocks=re.split(r'(?m)^(\t\t[^\t\s][^\n]*)(?=\n)',body)
        chunks.append('<pre class="original">'+e(command_blocks[0].strip())+'</pre>')
        links=[];rendered=[]
        for j in range(1,len(command_blocks),2):
            heading=command_blocks[j].strip();desc=command_blocks[j+1] if j+1<len(command_blocks) else ''
            a=f'{anchor}-c{j}';links.append((a,heading.split('\t')[0]));rendered.append(f'<h3 id="{a}">{e(heading)}</h3><pre class="original">'+e('\n'.join(x.lstrip('\t') for x in desc.splitlines()).strip())+'</pre>')
        chunks.append('<details><summary>コマンド索引</summary><div class="command-index">'+''.join(f'<a href="#{a}">{e(t)}</a>' for a,t in links)+'</div></details>')
        chunks.extend(rendered)
    else:
        chunks.append('<pre class="original">'+e('\n'.join(x[2:] if x.startswith('\t\t') else x.lstrip('\t') for x in body.splitlines()).strip())+'</pre>')
    section(anchor,'○ '+title,''.join(chunks))
limits='''<p>ファイル全体の上限は既定64KiBで、-mSIZEで増やせます。改訂2では音色データを末尾に置いた67,488バイトの元MDXを-m256で全バイト再現しました。トラック・音色領域の開始位置はデータ先頭から65535以内である必要があります。今回の原版も、トラック位置超過と長距離ループの6入力を拒否しました。詳細はLARGE64K_VALIDATION.mdを参照してください。include16段、通常マクロ8段、ループ32段。相対ジャンプは符号付き16bit範囲として検査します。連符の1要素は256ステップ以下です。診断の列はCP932のバイト位置で、タブは1文字として数えます。</p><p>同梱の実曲11曲は全バイト一致（STAGE5/6は-c -z、他は既定設定）。別の33入力は31本が全バイト一致、2本は欠落E8を補った比較コピーと一致します。2本は原本との一致件数には含めません。追加の大容量MDX2本と音色・波形バンク2本も一致しています。任意の全入力での完全互換や実機再生は未確認です。</p><p>Windows用にはVisual Studio 2022のソリューションを同梱しています。Release/x64でビルドするとnotemdx.exeとnotemdx_core.libを生成します。改訂2の提供ログではMSVCビルド成功とCTestの7件成功を確認しました。残る説明書テストの文字コードエラーを改訂3で修正し、Linux/GCCとCP932既定の模擬環境で検証しています。改訂3のWindows再実行は未確認です。詳しくはREADME_JA.mdとAPI.mdを参照してください。</p>'''
section('limits','現在の制限と検証範囲',limits)
source=(root/'docs/COMPATIBILITY.md').read_text(encoding='utf-8').split('## 根拠\n')[1].strip()
source_html,_=markdown(source)
source_html=source_html.replace('[MXDRVデータ資料](https://w.atwiki.jp/mxdrv/pages/23.html)','<a href="https://w.atwiki.jp/mxdrv/pages/23.html">MXDRVデータ資料</a>')
section('origin','作成根拠・補助資料',source_html+'<p>ここでいう文書に基づく作成とは、原プログラムや第三者実装のソースを用いない独立実装を意味します。上記の補助資料と実出力による照合を含みます。提供されたnote.doc、楽曲MML・MDX等は、新規ソース用の0BSDライセンスによる再許諾の対象には含めません。</p>')
(root/'docs/manual_ja.html').write_text(page('ja','notemdx.exe 0.8 — 日本語説明書','\n'.join(ja),toc),encoding='utf-8')
enbody,entoc=markdown((root/'docs/manual_en.md').read_text(encoding='utf-8'))
enbody=enbody.replace('https://w.atwiki.jp/mxdrv/pages/23.html','<a href="https://w.atwiki.jp/mxdrv/pages/23.html">https://w.atwiki.jp/mxdrv/pages/23.html</a>')
(root/'docs/manual_en.html').write_text(page('en','notemdx.exe 0.8 — English manual',enbody,entoc),encoding='utf-8')
print('Built Japanese and English offline manuals:',len(toc),len(entoc),'index entries')
