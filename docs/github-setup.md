# GitHub への push と CI(Actions)の確認手順(初心者向け)

このプロジェクトのテストは、お使いの PC では Smart App Control に止められて実行できません。
そのため **GitHub Actions(GitHub が貸してくれる Windows マシン)でビルドとテストを実行**します。
ここでは「リポジトリを作る → 最初のアップロード(push)→ 結果を見る」までを順番に説明します。

用語: **リポジトリ** = プロジェクトの保管場所 / **commit** = 変更の保存 / **push** = GitHub へアップロード。

---

## 0. 準備(最初の 1 回だけ)

1. **GitHub アカウント**を作る: <https://github.com/signup> (無料)。メール認証まで済ませる。
2. **Git** は、この PC には導入済みです(確認: PowerShell で `git --version`)。
   - 「認識されません」と出たら、PowerShell を一度閉じて開き直す。
3. 名前とメールを Git に登録する(commit に記録される情報で、公開リポジトリでは誰でも見られます)。

   メールは **GitHub の「noreply アドレス」**を使うと、本物のメールアドレスを公開せずに済みます。
   GitHub の Settings → Emails で「Keep my email addresses private」をオンにし、表示される
   `12345678+ユーザー名@users.noreply.github.com` をコピーしておく。

   ```powershell
   git config --global user.name "あなたの名前(ハンドル名でも可)"
   git config --global user.email "12345678+ユーザー名@users.noreply.github.com"
   git config --global init.defaultBranch main
   ```

## 1. GitHub 上に空のリポジトリを作る

1. <https://github.com/new> を開く。
2. 入力する項目:
   - **Repository name**: `LatencyForge`(好きな名前でよい)
   - **Public / Private**: 最初は **Private(非公開)** を推奨。公開は結果に納得してからでよい。
     - 非公開リポジトリでは Actions の無料枠に上限があり、Windows は消費が 2 倍です(月 2,000 分の無料枠なら実質 1,000 分)。
       1 回の実行は数分〜十数分なので、普段は問題になりません。
   - **Add a README file / .gitignore / license は、すべて「オフ」のまま。**
     (このプロジェクトに既にあるので、チェックすると最初の push で衝突します)
3. 「Create repository」を押す。表示されたページにある URL(`https://github.com/ユーザー名/LatencyForge.git`)を控える。

## 1.5 push する前の安全確認(重要)

次のものを GitHub に上げてはいけません。`.gitignore` で除外済みですが、**必ず自分の目でも確認**します。

- `build/`(巨大なビルド成果物)
- `config/`(実行時に作られる設定・状態・履歴。自分の PC の情報が入る)
- vcpkg の作業フォルダ(`vcpkg/`, `vcpkg_installed/`)
- パスワード・API キー・証明書(このプロジェクトには置いていないはずです)

## 2. 最初の push

PowerShell で、プロジェクトのフォルダに移動して実行します。
(`<プロジェクトのフォルダ>` の部分は、このプロジェクトを置いた場所に、ご自身の環境に合わせて読み替えてください。)

```powershell
cd "<プロジェクトのフォルダ>"
git init
git add .
git status
```

**ここで止まって `git status` の出力を確認します。** 「Changes to be committed」の一覧に
`build/`、`config/`、`vcpkg_installed/`、`shots/` で始まるファイルが **出ていなければ OK** です。
出ていたら `git add` を取り消してから相談してください(`git reset` で取り消せます)。

問題なければ続けます。

```powershell
git commit -m "Initial commit: M1 UI foundation and M2 core (registry layer, policy, tests)"
git branch -M main
git remote add origin https://github.com/ユーザー名/LatencyForge.git
git push -u origin main
```

- 最初の push でブラウザが開き、GitHub へのサインインを求められます。サインインして許可します
  (Windows 版 Git に入っている Git Credential Manager が、パスワードの代わりに安全にログイン状態を保存します)。
- パスワード入力を求められたら、GitHub のパスワードは使えません。ブラウザでのサインインに進んでください。
- `error: failed to push` と出たら、エラー文をそのまま貼って相談してください。

## 3. CI(Actions)の結果を見る

push した瞬間から、自動で実行が始まります。

1. GitHub のリポジトリのページを開き、上のタブの **「Actions」** をクリック。
2. 一覧の一番上に **「build」** という実行(run)が出ます。クリックして開く。
3. 状態の見方:
   - 🟡 黄色の丸 = 実行中(初回は vcpkg のビルドで 10〜20 分かかることがあります。2 回目以降はキャッシュで速くなります)
   - ✅ 緑のチェック = 成功
   - ❌ 赤のバツ = 失敗
4. 開いた画面の左の **「windows-x64」** をクリックすると、手順ごとの結果が見えます。
   主な手順:
   - `Configure` / `Build` … ビルド
   - `Unit tests (no real registry)` … 単体テスト
   - `Integration tests (HKCU test key only)` … 実機レジストリ(HKCU のテストキーのみ)の統合テスト
   - どこで赤くなったかが分かります。手順名をクリックすると、その出力が読めます。
5. 実行画面の一番下の **「Artifacts」** に、ダウンロードできる成果物が出ます。
   - `test-results` … テストのログ(`*.log`)と詳細(`*.xml`)。**失敗したときも必ず付きます。**
   - `latencyforge-win-x64` … ビルドした exe とハッシュ(ビルドとテストが成功した場合のみ)。
6. 実行画面の上部の **Summary** に、`unit : tests=… failures=…` のような集計が出ます。

### 失敗したとき
- 赤くなった手順を開き、**エラーの部分(赤字)をコピー**して、そのまま私に貼ってください。
- `test-results` の `unit.log` / `integration.log` を開くと、どのテストがなぜ失敗したかが書いてあります。
- 手元で直したら、同じ手順(`git add .` → `git commit -m "..."` → `git push`)でもう一度 push すると、CI が再実行されます。
- Actions タブの左の「build」→ 右上の **「Run workflow」** で、push しなくても手動で再実行できます。

## 4. 普段の更新の流れ(2 回目以降)

```powershell
cd "<プロジェクトのフォルダ>"   # 自分の環境の場所に読み替える
git status                 # 何が変わったか確認
git add .
git commit -m "変更内容を短く書く"
git push
```

## 5. 注意

- **M2 のテストは、CI が緑になるまで「テスト未確認」**として扱います。
- CI は `windows-latest` を使います。GitHub 側がイメージを更新して Visual Studio の版が変わると、
  ビルドが急に失敗することがあります(`Visual Studio 17 2022` が見つからない、など)。
  その場合は workflow の `runs-on` を `windows-2022` に固定してください。
- 非公開(Private)のまま使っても、このプロジェクトの機能に影響はありません。
- 初めて公開(Public)にするときは、`LICENSE`(MIT)、README の免責事項、アンチチートに関する注意が
  そろっていることを確認してください(M8 で整備します)。
