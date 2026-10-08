#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "lf/process_api.hpp"

namespace lf {

std::string lowerAscii(std::string_view s);

// 名前だけで「絶対に触らない」プロセス: Windows のシステム/シェル/セキュリティ、音声 (audiodg)、
// 代表的なアンチチートのサービス。ベストエフォートの一覧で、実際の保護は次の checkTarget が OS の情報で行う
// (保護プロセス/重要プロセス/他ユーザー/Windows フォルダ配下は名前に関係なく除外)。
bool isNeverTouchName(std::string_view exeName);

enum class SkipReason {
    None,           // 対象にしてよい
    Protected,      // 保護プロセス (PPL など)
    Critical,       // 重要なシステムプロセス
    OtherUser,      // 自分と別のユーザー、またはトークンを読めない (サービス/SYSTEM など)
    OtherSession,   // 別のセッション (サービスなど)
    WindowsDir,     // Windows フォルダ配下の実行ファイル
    MultiGroup,     // 複数のプロセッサグループにまたがる
    NeverTouchName, // 名前による除外
    Self,           // このアプリ自身
};

const char* skipReasonName(SkipReason r);

// 変更してよいプロセスか。ゲーム/バックグラウンドの両方に同じ基準を使う。
SkipReason checkTarget(const ProcessState& p, uint32_t selfPid, uint32_t currentSessionId);

// プロファイルに登録できる実行ファイル名か: ".exe" で終わり、パス区切りや不正文字を含まず、64 文字以内。
bool isValidExeName(std::string_view name);

}  // namespace lf
