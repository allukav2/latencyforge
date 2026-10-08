#pragma once
#include <string>
#include <vector>

#include "lf/registry.hpp"

namespace lf {

// レジストリ書き込みの可否を決める唯一の関門。tweak 定義のロード時、適用時、復元時のすべてで検査する
// (state.json が改ざんされても、復元経由でセキュリティ関連キーへ書けないようにするため)。
//
// 1. 拒否リスト (組み込み・設定で外せない) を先に評価:
//    Defender / UAC / Windows Update / ファイアウォール / SmartScreen / DeviceGuard・VBS・HVCI / LSA 保護 /
//    Spectre・Meltdown 緩和 / SEHOP / CFG / Image File Execution Options。
// 2. 許可リスト (キー接頭辞、セグメント境界、大文字小文字無視) に一致しなければ拒否。
class Policy {
public:
    // 製品用: 許可するのは Session Manager\kernel のみ。機能追加時 (USB/GPU ページ) にここへ追加する。
    static Policy standard();
    // 拒否リストはそのまま、許可リストだけ差し替える (単体/統合テスト用)。
    static Policy withAllowList(const std::vector<std::string>& keyPrefixes);

    Result<void> check(const RegPath& path) const;

    // 拒否リストだけを評価 (許可リストは見ない)。
    Result<void> checkNotDenied(const RegPath& path) const;

private:
    std::vector<std::vector<std::string>> m_allow;  // 小文字のセグメント列 (先頭はハイブ名)
};

}  // namespace lf
