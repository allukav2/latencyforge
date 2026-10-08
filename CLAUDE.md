# CLAUDE.md — LatencyForge (仮称)

Windows 10 (1809+)/11 x64 向け、競技ゲーム用の低遅延最適化ツール。C++20、MIT、完全無料・OSS、完全オフライン。
最重要: **UI の品質** と **安全に元へ戻せること**。判断に迷ったら本ファイルに従う。計画: M1〜M8 の計画書(ローカル作業用。リポジトリには含まれない)

## 進め方
- マイルストーンごとに「実装 → ビルド確認 → テスト → 変更要約」。一度に全部書かない。M1〜M8 は計画書参照。
- 仕様が不確かなもの(レジストリ値の効果、ビルド依存挙動)は断定しない。コードコメントと UI に「効果はビルド依存」と明記。
- ユーザーへの質問は最大 3 つ。他は仮定を明示して進める。
- 誇大表現(「FPS◯%向上」「絶対に検出されない」等)は書かない。検知回避目的の実装はしない。

## 技術スタック
- C++20 / CMake / MSVC (VS2022) / vcpkg。x64 のみ(ARM64 非対応)。CRT は静的 (/MT, triplet `x64-windows-static`)。
- UI: Dear ImGui + DirectX11(失敗時 WARP → それも駄目なら分かりやすいエラーで終了)。
- 構成: `core`(静的ライブラリ、UI 非依存)/ `app`(UI)/ `tests`(GoogleTest)。
- マニフェスト: requireAdministrator、DPI 対応、Windows 10/11 supportedOS。exe にバージョン情報・アイコン埋め込み。
- 描画はイベント駆動 or FPS 制限。バックグラウンド負荷ほぼゼロ。

## 禁止事項(絶対)
- ドライバー導入、カーネルモード、サービス常駐、スタートアップ登録、外部通信、テレメトリ、自動更新、ダウンロード実行、コード生成。
- パッカー、難読化、自己書き換え。
- Defender / SEHOP / CFG / VBS・HVCI / Spectre・Meltdown 緩和 / UAC / Windows Update / ファイアウォール / SmartScreen に触れる・弱めること。
  - `DisableExceptionChainValidation`、`DisableControlFlowGuardExportSuppression` は定義ファイルにも入れない。
- プロセス操作: SeDebugPrivilege、PROCESS_ALL_ACCESS、インジェクション、フック禁止。`OpenProcess` は `PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION` のみ。保護/システム/アンチチート保護下のプロセスは触らない。失敗は黙ってスキップしログに残す。
- 非公式ドライバー改変、USB ポーリングレート変更ハック、NVIDIA ドライバーファイル書き換え。

## 安全設計の不変条件
1. 適用前に旧値(存在しない場合は「存在しない」)を JSON にバックアップ。
2. 復元は冪等。個別復元・全復元・「すべて元に戻して終了」を提供。
3. 複数変更の途中失敗はロールバック。journal(pending/committed)で異常終了を検出し、再起動時に復元を提案。
4. Dry-run モード(差分のみ表示)。適用前に差分プレビューダイアログ必須。
5. レジストリは RAII ラッパー + `IRegistry` インタフェース経由。全エラーをログ化。
6. 書込パスはホワイトリスト + セキュリティ関連キーのブロックリストを **コードで** 強制。
7. 変更履歴(日時/対象/旧値/新値/結果)を永続化。
8. tweak はデータ駆動(`data/tweaks/*.json`)。id/path/value/type/data/説明(ja,en)/risk/requiresReboot/Windows ビルド範囲/備考。スキーマ不正はロード拒否。

## UI 規約
- ダークテーマ、アクセント色変更、角丸、控えめグロー、一貫した余白/タイポ。デフォルト ImGui 見えは不可。
- サイドバー: ホーム / Affinity / USB / GPU(NVIDIA) / カーネル・タイマー / ベンチマーク / バックアップ・復元 / ログ / 設定。
- tweak カード: トグル、1 行説明、詳細展開、リスク(低/中)、現在値→変更後、適用バッジ、個別復元。
- 非対応機能はグレーアウト + 理由ツールチップ。アニメは設定で全 OFF(省リソースモード)。
- 文字列は `data/lang/{ja,en}.json`。**ハードコード禁止**。日英切替。
- 各マイルストーンでスクリーンショット可能な状態にする。

## コーディング規約
- C++20、例外は境界で捕捉、`std::expected` 相当の Result 型でエラー伝播。RAII 徹底、生ハンドル禁止(`UniqueHandle`/`RegKey`)。
- 命名: 型 `PascalCase`、関数/変数 `camelCase`、メンバ `m_`、定数 `kPascal`、名前空間 `lf::`。
- core は Win32 UI 非依存。OS 依存部はインタフェース越しにモック可能にする。
- 警告 `/W4 /permissive-` を有効、コメントは「なぜ」のみ簡潔に。

## テスト
- core は単体テスト(レジストリはモック)。バックアップ/復元/ロールバック/スキーマ検証を重点。
- 実レジストリ統合テストは **HKCU 配下の専用テストキーのみ**。HKLM は Sandbox/VM 手動検証(`docs/manual-test-checklist.md`)。

## ビルド手順
```
cmake --preset x64          # 配布用 (requireAdministrator マニフェスト)
cmake --build --preset x64-release
ctest --preset x64-release

cmake --preset x64-dev      # 開発/スクリーンショット用 (asInvoker、昇格不要)
cmake --build --preset x64-dev-release
powershell -File tools\screenshot.ps1 -Page 4 -Out shots\kernel.png   # Page: 0=Home..8=Settings
```
前提: VS2022 Build Tools (C++ workload)、CMake ≥ 3.25、vcpkg(`VCPKG_ROOT` 設定、`~\vcpkg`)。新しい PowerShell では PATH 更新のため再起動が必要な場合あり。
ImGui は 1.92 以降(動的フォント API `PushFont(font, size)` を使用)。

## テスト実行環境の制約(重要)
- この開発 PC は Smart App Control が有効で、未署名のテスト exe (`lf_tests.exe`) の実行がブロックされる。
  **SAC の設定は変更しない。回避のためのコード分割・リネーム等もしない。**
- ローカルで確認するのは「ビルドが通ること」まで。テストの正本は GitHub Actions (`.github/workflows/build.yml`)。
  CI が緑になるまで該当マイルストーンのテストは「未確認」と報告する。
- `gtest_discover_tests` は `DISCOVERY_MODE PRE_TEST`(ビルド中にテスト exe を起動しない)。
- 手順書: `docs/github-setup.md`(push と Actions の見方)。Windows Sandbox/VM での手動検証はユーザーが後で実施。

## 配布
- ポータブル(単一 exe + 設定フォルダ)。設定は exe 隣 `data/`、書込不可なら `%APPDATA%\LatencyForge`。
- GitHub Actions: ビルド → テスト → SHA-256 → Release 添付。署名ステップは証明書未設定でもスキップしてビルドが通ること。
- docs/: 誤検知報告手順、SmartScreen 説明、アンチチート注意、tweak 定義ガイド、手動検証チェックリスト。
