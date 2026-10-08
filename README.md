# LatencyForge

[日本語](#日本語) | [English](#english)

---

## 日本語

競技ゲーム向けの低遅延最適化ツールです(Windows 10 1809 以降 / Windows 11、x64)。無料・オープンソース(MIT)。
**最優先は「安全に元へ戻せること」** です。すべての変更は適用前に差分を確認でき、変更前の値は自動でバックアップされます。

### 既知の制限(必ずお読みください)

- **アンチチートについて**: 一部のアンチチート(**Vanguard、EasyAntiCheat(EAC)、BattlEye** など)は、プロセスのアフィニティ変更を
  **警告したり、ブロックしたりする可能性があります**。Affinity 機能は、保護されたプロセス・システムのプロセス・開けないプロセスには触れず
  (スキップしてログに残します)、権限は `PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION` だけを使い、
  `SeDebugPrivilege`・インジェクション・フックは一切使いません。それでも、アンチチートとの相性は保証できません。
  オンライン対戦ゲームで使う前に、そのゲームの規約を確認し、**自己責任で**ご利用ください。詳細: [docs/anticheat.md](docs/anticheat.md)
- **効果はビルド・環境に依存します**。レジストリの調整は公式に文書化されていないものが多く、Windows のビルドによっては効果がない場合があります。
  「FPS が◯%上がる」といった保証はしません。
- **NVIDIA GPU 機能は、NVIDIA の実機では未検証**です。現在は GPU とドライバーの検出結果を表示するだけで、GPU やドライバーの設定は変更しません
  (公式に文書化された設定だけを、承認を得て追加する方針です。候補: [docs/gpu-candidates.md](docs/gpu-candidates.md))。
- **USB の設定は、公式の電源設定だけ**を使います(USB のセレクティブ サスペンド)。USB のポーリングレートの変更や、ドライバー/INF の改変は行いません。
  効果はシステム依存で、測れない場合があります。
- **単純な CPU 構成では、Affinity の効果は小さい**です(ハイブリッドコアがなく、L3 キャッシュが 1 グループの CPU など)。その場合は何も変更しません。
- **対応 OS**: Windows 10(1809 以降)と Windows 11 の x64。Windows 7/8、Server、LTSC、ARM64 は保証外で、起動時に警告し、一部の機能を無効にします。
- 実行には**管理者権限**が必要です(レジストリの変更とプロセス情報の操作のため)。

### できること(現在)

| 機能 | 内容 |
|---|---|
| カーネル・タイマー | 6 つのレジストリ値(`data/tweaks/kernel.json`)。差分プレビュー → 確認 → 適用、Dry-run、個別/全体の復元、再起動が必要の表示 |
| プリセット | 安全 / バランス / 最大(`data/presets.json`)。「最大」は項目数が最も多いだけで、最速という意味ではありません |
| USB | USB セレクティブ サスペンドの無効化(AC / バッテリー別)。現在の電源プランに適用し、プランごとにバックアップ。カーネルページと同じ差分プレビュー・復元 |
| GPU (NVIDIA) | NVIDIA GPU とドライバーの検出表示(NVIDIA 実機では未検証)。設定の変更はまだありません |
| Affinity 自動最適化 | 登録したゲームの起動を検知して、ゲームを最適なコア群へ、その他のアプリを別のコア群へ。終了時に必ず元へ戻す |
| システム検出 | OS、CPU トポロジー(P/E コア、L3 共有グループ、SMT)、GPU ベンダー。非対応の機能はグレーアウトして理由を表示 |
| 安全機構 | 変更前の値のバックアップ、冪等な復元、途中失敗時のロールバック、異常終了後の復元提案、変更履歴 |

### 安全性

- **触らないもの**: Windows Defender、SEHOP、CFG、VBS/HVCI、Spectre/Meltdown 緩和、UAC、Windows Update、ファイアウォール、SmartScreen。
  これらに関わるレジストリキーへの書き込みは、コードで拒否されます(許可リストと拒否リスト)。
- **外部通信なし**。テレメトリ・自動更新・ダウンロード実行はありません。ドライバーのインストール、サービス常駐、スタートアップ登録もしません。
- 免責事項と復元方法: [DISCLAIMER.md](DISCLAIMER.md)

### Affinity の使い方

1. 「Affinity」ページを開き、対象のゲームを登録します(ゲームを起動してから「実行中のプロセスから選択」が簡単です)。
2. 「ゲーム起動時に自動で適用」をオンにします(既定はオフ。オフの間は何も動きません)。
3. 登録したゲームが起動すると自動で適用され、ゲームの終了時と、このアプリの終了時に元へ戻ります。
   アプリが異常終了した場合も、次回起動時に前回の変更を元に戻します。

### ビルド

要件: Visual Studio 2022(C++ ワークロード)、CMake 3.25 以上、vcpkg(環境変数 `VCPKG_ROOT`)。

```
cmake --preset x64
cmake --build --preset x64-release
```

テストは GitHub Actions で実行します(`.github/workflows/build.yml`)。

### ライセンス

[MIT](LICENSE)

---

## English

A low-latency tuning tool for competitive gaming (Windows 10 1809+ / Windows 11, x64). Free and open source (MIT).
**Safe, reversible changes come first**: every change shows a diff before it is applied, and the previous values are backed up automatically.

### Known limitations (please read)

- **Anti-cheat software**: some anti-cheat systems (**Vanguard, EasyAntiCheat (EAC), BattlEye**, and others) **may warn about or block
  changes to a process's affinity**. The Affinity feature never touches protected processes, system processes or processes it cannot open
  (it skips and logs them), uses only the access rights `PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION`, and never uses
  `SeDebugPrivilege`, injection or hooks. Even so, compatibility with anti-cheat software cannot be guaranteed. Check the rules of your game
  before using it in online matches and use it **at your own risk**. Details: [docs/anticheat.md](docs/anticheat.md)
- **Effects depend on the Windows build and your system.** Many of the registry values are not officially documented and may do nothing
  on some builds. No "X% more FPS" claims are made.
- **The NVIDIA GPU feature has not been verified on real NVIDIA hardware.** For now it only shows the detected GPU and driver and does not
  change any GPU or driver setting (only officially documented settings will be added, after approval; candidates:
  [docs/gpu-candidates.md](docs/gpu-candidates.md)).
- **USB settings use the official Windows power settings only** (USB selective suspend). USB polling rates are never changed and no
  drivers/INF files are modified. The effect is system-dependent and may not be measurable.
- **On simple CPU layouts the benefit of Affinity is small** (no hybrid cores and a single L3 group, for example). In that case nothing is changed.
- **Supported OS**: Windows 10 (1809 or later) and Windows 11, x64. Windows 7/8, Server, LTSC and ARM64 are outside the supported range:
  a warning is shown at startup and some features are disabled.
- **Administrator rights** are required (to change registry values and to manage process settings).

### What it does (currently)

| Feature | Description |
|---|---|
| Kernel / Timer | Six registry values (`data/tweaks/kernel.json`). Diff preview → confirm → apply, Dry-run, per-item and full restore, restart-required marker |
| Presets | Safe / Balanced / Maximum (`data/presets.json`). "Maximum" means the most items, not the highest speed |
| USB | Disables USB selective suspend (AC / battery separately) in the active power plan, backed up per plan; same diff preview and restore as the kernel page |
| GPU (NVIDIA) | Shows the detected NVIDIA GPU and driver (not verified on real NVIDIA hardware). No settings are changed yet |
| Affinity automation | Detects a registered game starting, places it on the best cores and other apps on a different set; always restores on exit |
| System detection | OS, CPU topology (P/E cores, L3 sharing groups, SMT), GPU vendor. Unsupported features are greyed out with the reason |
| Safety | Backup of previous values, idempotent restore, rollback on partial failure, recovery prompt after a crash, change history |

### Safety

- **Never touched**: Windows Defender, SEHOP, CFG, VBS/HVCI, Spectre/Meltdown mitigations, UAC, Windows Update, firewall, SmartScreen.
  Writes to registry keys related to these are rejected in code (allow list and deny list).
- **No network access.** No telemetry, no auto-update, no download-and-run. No drivers, no resident service, no startup registration.
- Disclaimer and how to revert: [DISCLAIMER.md](DISCLAIMER.md)

### Using Affinity

1. Open the "Affinity" page and register your game (starting the game first and using "Pick from running processes" is easiest).
2. Turn on "Apply automatically when a game starts" (off by default; while off, nothing runs).
3. When the registered game starts the settings are applied automatically; they are restored when the game exits and when this app exits.
   After a crash, the next launch restores whatever the previous session had changed.

### Build

Requirements: Visual Studio 2022 (C++ workload), CMake 3.25+, vcpkg (`VCPKG_ROOT`).

```
cmake --preset x64
cmake --build --preset x64-release
```

Tests run on GitHub Actions (`.github/workflows/build.yml`).

### License

[MIT](LICENSE)
