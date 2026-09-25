# 変換モジュールAPI

## 入出力

`notemdx::compile(const Source&, const Options&) -> Result`

SourceのtextはCP932のMMLバイト列です。nameは診断・include解決に使用する論理ファイル名です。ファイルの存在はモジュール側では確認しません。

ResultはMDXのバイト列、診断一覧、補助出力ファイル一覧を所有します。`ok()` がfalseなら `mdx` と `auxiliary_files` は空です。失敗時の不完全なMDXは返しません。

通常の文法エラーは例外で外へ投げず、Diagnosticとして返します。メモリ不足などの標準ライブラリ例外、またはホストのコールバックが投げる例外は呼び出し元で扱ってください。

## include

```cpp
options.include_loader = [](
    const std::string& requested,
    const std::string& parent,
    notemdx::Source& loaded,
    std::string& error) -> bool {
    // requested: CP932のinclude名
    // parent: Source::name。文字コード・名前空間はホストの規約による。
    // loaded.name: 解決後の識別名。循環判定のため同一ファイルは同一名にする。
    // loaded.text: CP932で読み込んだMML
    // 成功ならtrue。失敗ならerrorを設定してfalse。
    return false;
};
```

メモリ辞書からの取得、ゲーム内アセット、ファイルシステムなど任意の実装を使えます。コールバック未設定で#includeを使うと診断エラーになります。

## バイナリバンク

```cpp
options.binary_loader = [](
    const std::string& requested, const std::string& parent,
    std::vector<std::uint8_t>& bytes, std::string& error) -> bool {
    // requested: CP932、parent: 命令のあるSource::name。
    // NOTEの生バイナリをbytesへ。文字コード変換はしない。
    // 失敗はerrorを設定してfalseを返す。
    return false;
};
```

#load-toneは7168バイト、#load-waveは131968バイトを要求します。サイズ、音色番号、波形長、ループ位置を検査します。登録済みデータは読込内容で初期化されます。波形の読込は各トラックの命令位置で反映します。

成功時、#save-tone/#save-waveまたはOptionsの保存名が指定された場合、`Result::auxiliary_files` に `AuxiliaryFile {name, parent, bytes}` が入ります。nameはCP932の保存名、parentは命令のあるSource::nameです。音色、波形の順で最大2ファイルを返し、各種類で最後に指定した保存名が有効です。保存は読込・定義をすべて反映した最終状態です。ホストは `ok()` を確認してから保存してください。

CLIは相対名をparentのフォルダから解決します。ライブラリ単体ではパスの意味とアクセス許可をホスト側で決められます。

## 独立性とスレッド

呼び出しごとに解析状態を構築し、可変のグローバル状態は持ちません。別々のcompile呼び出しは同時実行可能です。include_loaderやbinary_loaderで共有するホスト側の状態は、ホスト側で必要な同期を行ってください。

CP932/UTF-8変換や保存処理はCLIの `src/main.cpp` にあります。DLL化する場合は、同じC++ランタイム/ABIでの利用か、別途C APIの境界を設けてください。この版が提供するAPIはC++ APIと静的ライブラリです。

## オプション

|フィールド|既定値|用途|
|---|---|---|
|ex_pcm|false|全16チャンネル|
|reverse_octave|false|オクターブ方向の初期反転|
|muted_channels|空|変換から除外するチャンネル|
|max_output_bytes|65536|ファイル全体のバイト数上限。64〜67108864。形式内の16bit開始位置は別途検査|
|max_source_bytes|8MiB|includeを含む入力累計上限|
|max_expanded_bytes|16MiB|マクロ・トラック生成時の展開量上限|
|include_loader|未設定|インクルードのホスト実装|
|binary_loader|未設定|NOTEバンク読込のホスト実装|
|compression|未設定（optional）|設定時は全入力で#compressに優先。-1無効、0休符、1休符とタイ音符|
|optimization|未設定（optional）|設定時は全入力で#optに優先。空文字は無効、dvqpt012または*|

## ビルドへの組み込み

CMakeでは `add_subdirectory` で本プロジェクトを取り込み、`notemdx_core` をリンクできます。小規模な組み込みでは `src/compiler.cpp` を既存プロジェクトに追加し、`include` をインクルードパスに加えるだけです。

公開ヘッダはC++17、STL型を使用します。`main.cpp` を組み込む必要はありません。

## 休符圧縮と最適化をホストから指定

```cpp
notemdx::Options options;
options.compression = 0;
options.optimization = "*";
auto result = notemdx::compile(source, options);
```

これはCLIの `-c -z` と同じ指定です。optionalを未設定にすると、MML内の疑似命令に従い、初期状態は圧縮・最適化とも無効です。MMLの途中で設定を変える場合はホスト指定を外してください。

## 変換統計（0.8）

成功時の `Result::statistics` は表示やUIから独立した構造体です。

|フィールド|内容|
|---|---|
|title|CP932の曲名|
|fm_used / fm_unused|MDX音色領域に収録する選択音色／その他の定義済み音色。番号順|
|pcm_banks|発音のあるPCMバンクのみ、番号順。bank、used、unused（0〜95）|
|tracks[0..15]|ABCDEFGHPQRSTUVW順のTrackStatistics|
|total_steps|有限反復を展開した初回の終端までのステップ数|
|loop_steps|最初に実行したLまたはCから終端まで。なければnullopt|

StepCountはuint64_tのvalueとboolのoverflowを持ちます。上限超過時はvalueをUINT64_MAXに固定しoverflow=trueとします。途中の有限ループ内のCも最初の通過位置を採用します。到達しないCはnulloptです。未使用／ミュートトラックは0、ループ点なしです。Wの待機時間は含みません。

PCMの番号は実際の音符番号です。PDXを開かないため、サンプルの存在は検査しません。Lの反復で終端のバンクを引き継ぐ場合も使用番号に含めます。FMは発音前の音色選択も使用扱いです。失敗時のstatisticsは空の初期状態に戻ります。

## 原版CLI対応用の追加API（0.8 改訂1）

|Optionsの追加フィールド|既定値・意味|
|---|---|
|save_tone_filename / save_wave_filename|nullopt。指定時はMMLの保存名より優先するCP932名。空文字はtone.bin/wave.bin|
|save_parent|空。空の場合Source::name、指定時はその論理ソース名を相対保存先の基準にする|
|save_banks_on_error|false。trueなら最初の解析エラー時点の登録済みバンクをrecovery_filesへ返す|
|progress|未設定。設定時は各トラックの変換開始直前にチャンネル文字を通知|

Result::recovery_filesは失敗時専用です。save_banks_on_errorを明示した場合のみ使用し、最初のエラーまでの登録内容であることを呼び出し側に表示してください。失敗時のmdx/auxiliary_filesは引き続き空で、不完全なMDXを返しません。元のNOTEと同じエラー後の解析継続は行いません。コールバックの例外はホストで処理します。

CLIの-t/-wはカレントディレクトリをsave_parentの親フォルダとして指定します。モジュールは従来どおりファイルI/O、ビープ、削除、画面表示を行いません。-r/-b/-vはCLI側の処理です。APIのfm_unused/pcm_banks.unusedは互換性のため維持していますが、CLIは表示しません。

改訂2ではmax_output_bytesをファイル全体の上限として64〜67108864バイトに拡張し、既定値をnote.docの64KiBに合わせて65536としました。例：options.max_output_bytes = 256 * 1024 はCLIの-m256に対応します。音色データが末尾で64KiBを越えるK01TAILは、元MDXと全バイト一致します。

トラック開始位置と音色領域開始位置は、タイトル・PDX名の後のデータ先頭からの16bit値として別途検査します。最大65535で、越えた値を切り捨てて保存しません。最後のPCMトラックの末尾で音色開始位置が65536以上になる場合も拒否します。ループの符号付き16bit距離の検査は維持します。
