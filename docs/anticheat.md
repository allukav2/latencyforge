# アンチチートに関する注意 / Notes on anti-cheat software

## 日本語

LatencyForge の **Affinity 機能**は、ゲームの CPU アフィニティ(使用するコア)と、必要に応じて優先度を変更します。
一部のアンチチート(**Vanguard、EasyAntiCheat(EAC)、BattlEye** など)は、プロセスのアフィニティ変更を
**警告したり、ブロックしたり、不正行為の疑いとして扱ったりする可能性があります**。

### このツールが行っていること(技術的な説明)

- 他のプロセスを開くときの権限は **`PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION` だけ**です。
  プロセスを開く場所はコード上で 1 か所(`WinProcessApi`)に限り、テストで検査しています。
- 使わないもの: `SeDebugPrivilege`、`PROCESS_ALL_ACCESS`、プロセスメモリの読み書き、リモートスレッド、DLL インジェクション、フック。
  これらの API がコードに現れないことも、テストで検査しています。
- 保護プロセス(PPL など)、重要なシステムプロセス、他のユーザーのプロセス、Windows フォルダ配下の実行ファイル、
  音声エンジン(audiodg)、代表的なアンチチートのサービス(vgc、EasyAntiCheat、BEService など)には触れません。
- アンチチートが保護しているプロセスは、`OpenProcess` が失敗します。その場合は何も変更せず、スキップしてログに残します。
- 変更前のアフィニティ/優先度を記録し、ゲームの終了時とアプリの終了時に必ず元へ戻します。

### それでも保証できないこと

- アンチチートの検知方法・方針はゲームごとに異なり、変更されることがあります。「検出されない」とは主張しません。
- アフィニティの変更を禁止するゲームの規約があるかもしれません。**オンライン対戦ゲームで使う前に、そのゲームの規約を確認してください。**
- 心配な場合は、Affinity 機能をオフのままにしてください(既定はオフです)。

## English

The **Affinity feature** of LatencyForge changes a game's CPU affinity (which cores it may use) and, optionally, its priority.
Some anti-cheat systems (**Vanguard, EasyAntiCheat (EAC), BattlEye**, and others) **may warn about, block, or treat as suspicious**
changes to a process's affinity.

### What the tool does (technical)

- It opens other processes with **only `PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION`**.
  There is exactly one place in the code that opens a process (`WinProcessApi`); a test enforces this.
- It does not use `SeDebugPrivilege`, `PROCESS_ALL_ACCESS`, process memory access, remote threads, DLL injection or hooks.
  A test also checks that these APIs never appear in the source.
- It never touches protected processes (PPL and similar), critical system processes, other users' processes, executables under the
  Windows folder, the audio engine (audiodg), or well-known anti-cheat services (vgc, EasyAntiCheat, BEService, and so on).
- A process guarded by an anti-cheat typically makes `OpenProcess` fail. In that case nothing is changed; the process is skipped and logged.
- The previous affinity/priority is recorded and always restored when the game exits and when the app exits.

### What cannot be guaranteed

- Detection methods and policies differ per game and can change. This tool does not claim to be "undetectable".
- A game's rules may forbid changing affinity. **Check the rules of the game before using it in online matches.**
- If in doubt, leave the Affinity feature off (it is off by default).
