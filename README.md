# NoteMDX 0.8 改訂3 — 文書改訂1（2026-09-26）

NOTE v0.8.5形式のMMLをX68000用MDXに変換するC++17モジュールとCLIです。変換部分は他のプログラムから独立して利用できます。

**全52入力で、44本が元MDXと全バイト一致、2本はEX-PCM補正後一致、6本は原版同様の拒否結果を確認しています。** 音色・波形バンク2本も一致しました。小規模33入力に限ると31本が全バイト一致です。 残る2本は、Aパートが音符を発音しないEX-PCMデータです。元データの欠落バイトE8を補った比較コピーと一致します。元ファイルとの一致扱いにはしていません。

0.4では音色・波形バイナリの保存／読込、#compress、#optによる重複コマンドの削除を追加しました。追加のMDX7本とバイナリ2本はすべて全バイト一致しています。音符数モードのディレイ、負のディレイとタイ、VM/MVと一時音量、複数の波形効果の組合せも照合しました。

`pcmuse.map` の生成は、ご指定により未対応です。`#pcmlist` は警告して無視し、MDXは生成します。対応範囲は `docs/COMPATIBILITY.md` を参照してください。実機・既存プレイヤーでの再生は未確認です。

## 0.8の追加機能

日英の索引付き説明書は [日本語](docs/manual_ja.html) / [English](docs/manual_en.html) です。原文の `note.doc`（CP932テキスト）も同梱しています。

変換成功時に、曲名、FM使用音色、使用中のPCMバンクごとの音色番号、OPM A〜H・PCM P〜Wの総ステップ数とL/Cからのステップ数を英語の整列表示で出力します。曲名は原文を保持します。1ステップは四分音符の1/48です。

FMはMDXに収録する選択音色を範囲省略せず個別に表示します。PCMは発音する番号を0〜95のバンク内番号で表示します。未使用番号はFM・PCMとも表示しません。項目幅はTotal stepsの11文字、半角空白1つの後に:です。PDX内の音声の存在やPCM8による実際の割り当てを検査するものではありません。有限ループと最終回の `/` を反映し、Lは最初の終端まで、Cは最初に実行される位置から終端までを集計します。ループ点なしは `-`、未使用／ミュートトラックは0です。同期信号待ちWの待ち時間は加算しません。64bitを超える場合は `overflow` と表示します。

同じ情報は `Result::statistics` から取得できます。`pcmuse.map` の入出力は引き続き未対応です。

## 実曲互換性

ILの11曲すべてが元MDXと全バイト一致しました。**STAGE5・STAGE6は `-c -z` を指定し、他9曲は既定設定です。** 休符のタイ、波形の発音長の丸め、クオンタイズ設定・復帰、波形中の音量、GLON再開、圧縮・最適化の状態管理を修正しました。

```bat
notemdx.exe -c -z STAGE5.mus
notemdx.exe -c -z STAGE6.mus
```

当時の起動オプションは未確認です。この指定で元データを再現できることを確認しました。曲名による自動切替は行いません。

既存33本の照合結果も維持しています。小規模入力と実曲の小計44入力では、42本完全一致、2本が既知のEX-PCM宣言補正後一致です。STAGE5/6には上記オプションが必要です。

0.6で対応した行末コメントと連続臨時記号、0.5の行頭コメント規則も維持しています。

## 原版スイッチの復元と64KiB検証

-iCHANNELSを復元し、-m/-r/-b/-t/-w/-v/-1/-eと引数なしスイッチの併記に対応しました。-v1はASCII矢印、-v0は対応端末で色を使用します。-eはエラー時の登録済みバンク保存に対応しますが、最初の致命的エラー後の解析継続は未対応です。-lと引数なし-oは認識した上でpcmuse.map未対応の警告を出します。全項目の確認表はdocs/CLI_COMPATIBILITY.mdを参照してください。

-mSIZEでファイル全体の上限をKiB単位で指定できます（既定64KiB）。-m256で返送された67,488バイトのK01TAIL.mdxと全バイト一致しました。ファイル全体は64KiBを超えられますが、トラック・音色領域の開始位置はデータ先頭から65535以内である必要があります。ループの相対距離制限も維持します。原版もトラック位置超過と長距離ループの6入力をエラーにしていました。

大容量検証の結果はdocs/LARGE64K_VALIDATION.md、原版のMDX2本とLOG8本はtests/large64k_referenceに同梱しています。追加の返送は不要です。入力と再現用バッチはprobes/large64kにあります。

```text
notemdx.exe -m256 probes/large64k/K01TAIL.MML
```

従来の44入力に今回の8入力を加えた52入力について、生成成功46本（44本が元MDXと全バイト一致、2本は既知のEX-PCM補正後一致）、原版同様の拒否6本を確認しています。

MDX出力先の明示指定は--output FILEを推奨します。互換用-o FILEは、入力名指定後、次の名前が.mdx/.MDXの場合、または出力名・入力名が連続する場合に認識します。PCMマップ指定は-loの併記が明確です。

## Visual Studio 2022

1. 「C++によるデスクトップ開発」とWindows SDKをインストールしたVisual Studio 2022で、`NoteMDX.sln` を開きます。
2. `Release / x64` でソリューションをビルドします。
3. `bin\x64\Release\notemdx.exe` がコンバータです。`notemdx_core.lib` が変換ライブラリです。
4. `bin\x64\Release\notemdx_tests.exe` で自動検査を実行できます。

外部ライブラリ、vcpkg、サウンドDLLは不要です。Windows用バイナリは同梱していません。改訂2の提供ログでMSVCビルド成功とCTest 7/8成功を確認しました。改訂3はLinux/GCCとCP932既定の模擬環境で検証済みです。改訂3のWindows再実行は未確認です。

CMakeを使用する場合は `build_vs2022.bat` を実行できます。この場合の実行ファイルは `build\Release` に生成されます。

## 使い方

```bat
bin\x64\Release\notemdx.exe examples\basic.mml
bin\x64\Release\notemdx.exe --encoding utf8 -o song.mdx song.mml
bin\x64\Release\notemdx.exe --ex-pcm --mute BH song.mml
```

出力を省略すると、入力の拡張子を `.mdx` に変更した名前になります。入力名の拡張子を省略した場合は `.mml`、`.mus` の順に探します。

|オプション|内容|
|---|---|
|`--output FILE`|出力ファイル。-o FILEも判別可能な場合は互換用に受理|
|`--encoding auto`|既定。BOM付きUTF-8、その他はCP932として読み込む|
|`--encoding utf8`|BOMなしも含めてUTF-8として読み込む|
|`--encoding cp932`|CP932 / WindowsのShift_JISとして読み込む|
|`--ex-pcm` / `-p`|PCM 8ch、全16chのMDXを生成|
|`--reverse-octave` / `-x`|`<` / `>` の初期方向を反転|
|`-iCHANNELS` / `--mute CHANNELS`|指定チャンネルを変換しない。例 `ABPQ`|
|`-c` / `--compress rests`|休符圧縮。MMLの#compressより優先|
|`-cn` / `--compress notes`|休符とタイで接続された同音程を圧縮|
|`--compress none`|圧縮を明示的に無効化|
|`-z` / `--opt "*"`|全項目の最適化。MMLの#optより優先|
|`-zdvqpt012` / `--opt dvqpt012`|最適化項目を指定。--opt noneで無効化|
|`--help` / `--version`|ヘルプ / バージョン表示|

MDXのタイトルとPDX参照名はCP932になります。変換不能な文字はエラーです。日本語ファイル名を使用できます。include内の文字コードにも同じオプションを使用します。

成功時は終了コード0、MML変換エラー時は1、入出力・引数エラー時は2です。通常はMMLエラーで既存MDXを維持します。-r指定時は削除します。成功時の書き込みも一時ファイルを経由します。

`#play` は警告して無視します。MMLから外部コマンドは実行しません。`#remove`は無視しますが、CLIで-rを明示すると変換エラー時に旧MDXを削除します。-bはビープ、-v/-v0/-v1は進行状況と位置付きソース表示です。

## 他のプログラムから使用

`include/notemdx/compiler.hpp` と `src/compiler.cpp` だけで変換部分を組み込めます。あるいは `notemdx_core.lib` をリンクしてください。C++の標準ライブラリのみを使用します。

```cpp
#include "notemdx/compiler.hpp"

notemdx::Source input;
input.name = "memory.mml";
input.text = "#title \"Test\"\nP @0 F4 n0,4\n"; // CP932 bytes

notemdx::Options options;
auto result = notemdx::compile(input, options);
if (result.ok()) {
    // result.mdx は生成されたMDXの所有権付きバイト列です。
    // 保存・再生・転送は呼び出し元が行います。
} else {
    // result.diagnostics: ファイル名、行、列、チャンネル、メッセージ
}
```

`#include` を使う場合だけ `Options::include_loader` に読み込みコールバックを設定します。変換モジュールは自分でファイルを開きません。UI、ファイルI/O、文字コード変換、再生処理から独立しています。詳細と利用例は `docs/API.md` と `examples/use_module.cpp` にあります。

## 音色・波形バイナリ

```text
#load-tone "tone.bin"
#wavemem
#load-wave "wave.bin"
#save-tone "copy_tone.bin"
#save-wave "copy_wave.bin"
```

NOTE形式の音色7168バイト、波形131968バイトを扱います。保存名省略時は `tone.bin` / `wave.bin` です。疑似命令の相対パスは各命令を書いたMMLファイルのフォルダを基準に解決します。-t[name]/-w[name]も使用でき、CLI指定の保存名は実行カレント基準で疑似命令より優先します。読込内容はそれまでの登録状態を置き換え、保存するのは変換終了時の状態です。

入力MML、include先、読込バイナリと同じファイルへは保存できません。別名を指定してください。複数出力のパス重複もエラーです。全ファイルを一時保存してから順に置き換えますが、OSの置換処理で途中失敗した場合の一括ロールバックは行いません。

モジュール利用時は `Options::binary_loader` で読込を実装し、成功時の `Result::auxiliary_files` を呼び出し元が保存します。モジュール自体はファイルを作成しません。

## 照合用データ

`probes/priority`・`effects`・`followup`・`remaining`の33入力と`tests/reference`の受領データ、`tests/il_reference`の11曲、`probes/large64k`の8入力と`tests/large64k_reference`のMDX2本・LOG8本を同梱しています。資料一覧は日英説明書末尾およびdocs/COMPATIBILITY.mdの「根拠」を参照してください。`python tests/test_reference.py <notemdx.exeのパス>` で小規模33入力とバンク2本を再照合できます。実曲はtests/test_il.py、大容量8入力はtests/test_large64k_probes.pyで照合します。追加ファイルの返送は不要です。

## ファイル構成

|パス|内容|
|---|---|
|`include/notemdx/compiler.hpp`|再利用用C++ API|
|`src/compiler.cpp`|MML解析・MDX生成|
|`src/main.cpp`|CLI、文字コード変換、include読み込み、保存|
|`NoteMDX.sln` / `*.vcxproj`|Visual Studio 2022構成|
|`CMakeLists.txt`|CMake構成|
|`tests/test_compiler.cpp`|変換・エラー・境界値の自動検査|
|`tests/test_reference.py` / `tests/reference`|33入力の照合（31本全バイト一致・2本補正後一致）と元MDX・バンク|
|`tests/test_il.py` / `tests/il_reference`|実曲11組の照合|
|`tests/test_large64k_probes.py` / `tests/large64k_reference`|large64k.zipの元MDX2本・LOG8本との照合|
|`examples/basic.mml` / `basic.mdx`|変換サンプルと生成結果|
|`probes`|元のNOTEに渡す照合用MML|
|`docs`|日英HTML説明書、API、対応範囲、検証記録|
|`note.doc`|提供された原版説明書（CP932テキスト）|

新規ソースのライセンスは0BSDです。`tests/il_reference` の楽曲MML・MDXおよび原版note.docはユーザー提供資料であり、ソースコード用ライセンスによる再許諾の対象には含めません。NOTE本体、MXDRV本体、第三者のコンバータや再生エンジンのソースは含みません。

改訂3では日本語WindowsのCTest文字コードエラーを修正しました。提供ログで改訂2のMSVCビルド成功と8テスト中7件の成功を確認しています。改訂3のWindows再実行は未確認です。詳細は[CTest修正記録](docs/CTEST_FIX.md)。
