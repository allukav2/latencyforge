# GPU (NVIDIA) 設定の候補 / Candidate NVIDIA settings

**状態: どれも未実装です。** 方針は「公式に文書化されていると確実に言える設定だけを、承認を得てから実装する」です。
ドライバーファイルの書き換えや、非公式なレジストリの変更は行いません。NVIDIA の実機での検証もできていません。

**Status: none of these is implemented.** The policy is to implement only settings that can be said, with confidence, to be officially
documented, and only after approval. Driver files are never modified and unofficial registry hacks are never used. None of this has been
verified on real NVIDIA hardware.

> 「文書化の確度」は、実装者(AI)が調べて・記憶している範囲での評価です。**承認の前に、あなた(または NVIDIA/Microsoft の一次資料)で確認してください。**

| # | 候補 | 仕組み | 文書化の確度 | 影響範囲 | 備考 |
|---|---|---|---|---|---|
| 1 | 電源管理モード(「パフォーマンス最大化を優先」など) | NVAPI の DRS (Driver Settings) API。設定 ID `PREFERRED_PSTATE_ID` | **中〜高**: NVAPI SDK(NVIDIA が公開)のヘッダー `NvApiDriverSettings.h` に設定 ID と値が定義されている | アプリごとのプロファイル(ゲームの exe 単位)にできる = 影響が小さい | NVAPI SDK の取り込みと**ライセンス確認**が必要。NVIDIA 実機でしか動作確認できない |
| 2 | 最大プリレンダリング フレーム数(低遅延モードの一部) | NVAPI DRS。設定 ID `PRERENDERLIMIT_ID` | **中**: 数値の設定はヘッダーにある。NVIDIA コントロールパネルの「低遅延モード: ウルトラ」に相当する内部の挙動は、ヘッダーからは確認できない | アプリごと | 「ウルトラ」は実装しない(文書化が確認できないため) |
| 3 | 垂直同期 / フレームレート制限 | NVAPI DRS | 中 | アプリごと | 画質・見た目に影響する。「低遅延」の調整としては優先度が低い |
| 4 | ハードウェア アクセラレーションによる GPU スケジューリング(HAGS) | Windows の設定 (レジストリ `HKLM\...\GraphicsDrivers\HwSchMode`) | **確認できていない**: 機能自体は Microsoft が公開しているが、レジストリ値の仕様を公式文書で確認できていない | システム全体、再起動が必要 | NVIDIA 固有ではない。効果はシステム依存で、悪化する場合もある |
| 5 | アプリごとの GPU 優先設定(ハイブリッド グラフィックス向け) | Windows の「グラフィックの設定」(レジストリ `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`) | **確認できていない**: 設定画面は文書化されているが、レジストリの書式は公式文書で確認できていない | アプリごと(ユーザー単位) | ノート PC で、ゲームが内蔵 GPU で動いてしまう場合に有用。NVIDIA 固有ではない |
| 6 | ウィンドウ ゲームの最適化 | Windows の設定(`DirectXUserGlobalSettings`) | **確認できていない** | ユーザー単位 | レジストリの書式を公式文書で確認できていない |
| 7 | GPU の電力制限・クロック | NVML (`nvmlDeviceSetPowerManagementLimit` など) | 高(NVML は公式) | GPU 全体 | **実装しない**: オーバークロックに近く、リスクが大きい。「低遅延」の目的から外れる |

## 推奨(ご判断いただきたい点)

- **候補 1 と 2** だけを、「アプリごとのプロファイルとして設定し、適用前の値を保存して元に戻せる」形で実装する案です。
  前提: NVAPI SDK のライセンス確認、NVIDIA 実機での検証(私は NVIDIA 実機を持っていないため、**あなたの実機での確認が必要**)。
- 候補 4〜6 は、レジストリ値の仕様が公式に確認できるまで実装しません。
- 候補 7 は実装しません。

## English summary

- Candidates 1 and 2 (NVAPI DRS: power management mode, max pre-rendered frames) are the only ones that appear to be documented in an
  official SDK header; they would be applied per application profile, backed up and restorable. They need an NVAPI license check and
  verification on real NVIDIA hardware.
- Candidates 4–6 (Windows graphics settings stored in the registry) are not confirmed to be officially documented and are not planned.
- Candidate 7 (power limit / clocks via NVML) is out of scope.
