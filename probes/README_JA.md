# NOTEとの照合に使用したMML

この表は小規模検証4群の33本と音色・波形バンク2本を対象にしています。別途large64k.zipのMDX2本・LOG8本を受領済みで、large64k/README_JA.mdとdocs/LARGE64K_VALIDATION.mdに結果を記録しています。ILの11曲を含む全体は52入力です。追加の変換・返送は不要です。pcmuse.mapの生成は対象外です。

|区分|内容|MDX本数|
|---|---|---:|
|priority|基本文法・ループ・LFO・音色・連符など|13|
|effects|波形エフェクト・休符・音程マップ|10|
|followup|EX-PCMと一時音量・制御|3|
|remaining|バンク保存／読込と残る組合せ・圧縮・最適化|7|

remainingのg00_saveは音色・波形を保存し、g01_loadはそのバンクを読み込みます。手動実行時は同じフォルダでg00、g01の順に変換してください。#pcmlistは警告して無視します。

通常は `python tests/test_reference.py <notemdxのパス>` で上記33入力とバンク2本を検査できます。大容量8入力はtests/test_large64k_probes.py、実曲11曲はtests/test_il.pyを使用してください。この検査では一時フォルダを使用し、元データを変更しません。結果と既知の2本のEX-PCM補正はdocs/VALIDATION.mdに記載しています。
