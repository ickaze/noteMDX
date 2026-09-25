"""Check offline manual structure, links, original-text coverage and version."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit,unquote
import re

root=Path(__file__).resolve().parent.parent
class Document(HTMLParser):
    def __init__(self,text):
        super().__init__(convert_charrefs=True)
        self.ids=[];self.links=[];self.text=[];self.stack=[];self.feed(text)
        assert not self.stack,self.stack
    def handle_starttag(self,tag,attrs):
        attrs=dict(attrs)
        if 'id' in attrs:self.ids.append(attrs['id'])
        if tag=='a':self.links.append(attrs.get('href',''))
        if tag not in ('meta','br','hr','img','link','input'):self.stack.append(tag)
    def handle_endtag(self,tag):
        assert self.stack and self.stack.pop()==tag,tag
    def handle_data(self,data):self.text.append(data)

pages={}
for path in (root/'docs').glob('manual_*.html'):
    text=path.read_text(encoding='utf-8')
    doc=Document(text);pages[path.resolve()]=doc
    assert len(doc.ids)==len(set(doc.ids))
    assert '<meta charset="utf-8">' in text
    assert 'notemdx.exe 0.8' in text
    assert '<script' not in text and '<link' not in text
    assert all(word in text for word in ['pcmuse.map','#pcmlist','d++','d--','1994,95 by DIS','YURAYSAN','https://w.atwiki.jp/mxdrv/pages/23.html'])
    origin=text.split('id="origin"',1)[1] if path.name=='manual_ja.html' else text.split('>Origin and supplementary material</h2>',1)[1]
    assert '<table>' in origin
    assert all(name in origin for name in ['note.doc','priority.zip','effects.zip','followup.zip','remaining.zip','IL.zip','large64k.zip','err.log','K00SAFE','K01TAIL','56,093','67,488','LARGE64K_VALIDATION.md'])
for path,doc in pages.items():
    for link in doc.links:
        parts=urlsplit(link)
        if parts.scheme:continue
        target=(path.parent/unquote(parts.path)).resolve() if parts.path else path
        assert target.exists(),(path,link)
        if parts.fragment:assert target in pages and parts.fragment in pages[target].ids,(path,link)
assert 'notemdx.exe 0.8 は、X68000のMXDRV2用MDXを生成するMMLコンバータです。note.docの技術説明を、索引付きで整理しました。現在の仕様との差は各項の補足を優先してください。原版全文は同梱のnote.doc（CP932テキスト）で確認できます。' in (root/'docs/manual_ja.html').read_text(encoding='utf-8')
# Every nondecorative technical source line must survive the Japanese reformat.
original=(root/'note.doc').read_bytes().decode('cp932').split('●え～と')[0]
original=original.split('●文法について')[1]
original=re.sub(r'\bnote(?:\.x)?\b','notemdx.exe 0.8',original)
normalize=lambda s:re.sub(r'\s+','',s)
visible=normalize(''.join(pages[(root/'docs/manual_ja.html').resolve()].text))
for line in original.splitlines():
    if not line.strip() or '───' in line:continue
    assert normalize(line) in visible,repr(line)
print('Two HTML manuals: tag structure, local links, index anchors, version and Japanese source coverage passed')
