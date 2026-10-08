# 免責事項 / Disclaimer

## 日本語

本ソフトウェア (LatencyForge) は MIT ライセンスのもと無償で提供され、**現状有姿 (AS IS)** で、明示・黙示を問わずいかなる保証もありません。

- **レジストリを変更します。** 効果はお使いの Windows のビルドやハードウェアに依存し、まったく効果がない場合もあります。
- 変更前の値は自動でバックアップされ、このアプリから元に戻せます (個別復元・すべて復元)。ただし、**あらゆる状況での完全な復元を保証するものではありません。** 重要なデータは事前にバックアップし、システムの復元ポイントの作成をご検討ください。
- 一部のアンチチート (Vanguard、EAC など) は、プロセスやシステム設定の変更を警告またはブロックする可能性があります。各ゲームの利用規約に従い、**自己責任で**ご利用ください。
- 本ソフトウェアの使用により生じたいかなる損害についても、作者および貢献者は責任を負いません。
- セキュリティ機能 (Windows Defender、UAC、Windows Update、ファイアウォール、SmartScreen、VBS/HVCI、SEHOP、CFG、Spectre/Meltdown 緩和) には一切触れません。ネットワーク通信、テレメトリ、自動更新はありません。

### 元に戻す方法
1. アプリの「カーネル・タイマー」ページで、各項目の「元に戻す」、または「すべて元に戻す…」を使う。
2. アプリが起動できない場合: Windows の「システムの復元」で、初回に作成した復元ポイントへ戻す。
3. 変更されるのは `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel` 配下の値のみです。値の一覧は `data/tweaks/*.json` にあります。

## English

This software (LatencyForge) is provided free of charge under the MIT License, **AS IS**, without warranty of any kind, express or implied.

- **It changes registry values.** The effect depends on your Windows build and hardware, and may be zero.
- The previous values are backed up automatically and can be restored from the app (per tweak or all at once), but **a complete restore in every situation cannot be guaranteed.** Back up important data and consider creating a system restore point first.
- Some anti-cheat software (for example Vanguard or EAC) may warn about or block changes to process or system settings. Follow each game's terms and use the tool **at your own risk**.
- The author and contributors are not liable for any damage resulting from the use of this software.
- The tool never touches security features (Windows Defender, UAC, Windows Update, firewall, SmartScreen, VBS/HVCI, SEHOP, CFG, Spectre/Meltdown mitigations). It makes no network connections, and has no telemetry or auto-update.

### How to revert
1. On the Kernel / Timer page, use "Revert" on an item, or "Revert everything…".
2. If the app cannot start: use Windows System Restore to return to the restore point created at first run.
3. Only values under `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel` are changed. The list is in `data/tweaks/*.json`.
