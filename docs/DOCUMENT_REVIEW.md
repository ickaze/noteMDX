# 文書全体の再点検 — 0.8 改訂3 文書改訂1

確認日：2026-09-26（日本時間）。実行プログラムの版は0.8 revision 3を維持します。

## 修正した内容

|対象|点検結果・対応|
|---|---|
|日英HTML・COMPATIBILITY.mdの作成根拠|直前の改訂3にはlarge64k.zipの記述がありましたが、長い段落内でした。今回、資料別の表に整理し、元MDX2本・LOG8本、入力8本、サイズ、照合結果、詳細資料名を明示しました。|
|作成根拠の範囲|note.doc、YURAYSAN氏の出力形式資料、4群の試験ZIP、IL.zip、large64k.zipを区別。err.logはWindowsのビルド・テストの根拠として別扱いにしました。日英で同じ資料と結果を掲載。|
|README・COMPATIBILITY.md・VALIDATION.md|現在の全52入力と小規模33入力・実曲11入力の小計44入力を区別。全バイト一致44本、補正後一致2本、拒否6本、別集計のバンク2本に統一。|
|CLI・実曲・大容量の検証資料|残っていた「MSVC未実施」の一律表記を修正。ユーザーの改訂2ログではビルドと7件のCTestが成功、manualsのみ失敗。改訂3のWindows再実行は未確認と明記。|
|検証の時点|Sanitizerは改訂2、8件相当の個別実行は改訂3、今回の再実行は説明書の生成・検査であることを区別。過去のCHANGELOGの件数は当時の記録として維持。|
|64KiBの記述|ファイル全体の予算と16bit開始位置・ループ相対距離を区別。K01TAILの67,488バイトとK00SAFEの56,093バイトを同梱データで確認。診断位置・件数の完全一致、原版の厳密な分岐境界値は未検証。|
|API・CLI・統計|公開ヘッダとCLI処理、既存検証記録に照合。APIは未使用番号を保持、CLIは表示しない。-i、-v、-t/-w、-m、-e/-r、未対応PCMマップの説明を確認。|
|probesの説明・ファイル一覧|33入力用とlarge64kの8入力用テストを区別し、IL11曲を含む全体への案内を追記。|
|THIRD_PARTY_NOTICES.md|large64k.zipを含む提供資料を明示。原版LOGも新規ソース用0BSDの再許諾対象外と明記。LICENSE本文は維持。|
|HTML生成と検査|日本語の根拠表をCOMPATIBILITY.mdから生成。既存HTML検査に、日英それぞれの作成根拠節で全資料名と大容量2本の数値を確認する検査を追加。|

## 検証結果と変更範囲

- 日英HTMLのタグ構造・索引・ローカルリンク・指定導入文・日本語原文行網羅が成功。
- CP932既定の模擬環境でも生成ツールと説明書検査が成功。
- 原版note.doc、変換本体、公開ヘッダ、ビルド設定、変換テスト、サンプル、参照MDX・バンク・LOG、試験MMLなど保護対象133ファイルが改訂3とバイト一致。
- ILの11組の記録済み入力・参照SHA-256を実ファイルと照合。
- 変換本体に変更がないため、変換テストの全件再実行は今回行わず、docs/rev3_*_results.txtとCTEST_FIX.mdの改訂3結果を引き継ぎます。Windowsで全8件が通過したとの新しい主張はしていません。
- 外部資料の再調査や原版NOTEの新規実行は行っていません。記録済みの根拠・実装・提供データの整合性を確認した文書改訂です。

## 更新ファイル

- CHANGELOG.md
- README_JA.md
- THIRD_PARTY_NOTICES.md
- docs/CLI_COMPATIBILITY.md
- docs/COMPATIBILITY.md
- docs/IL_VALIDATION.md
- docs/LARGE64K_VALIDATION.md
- docs/VALIDATION.md
- docs/manual_en.html
- docs/manual_en.md
- docs/manual_ja.html
- probes/README_JA.md
- tests/test_manuals.py
- tools/build_manuals.py
- docs/DOCUMENT_REVIEW.md（本記録）
- SHA256SUMS.txt（再生成）
