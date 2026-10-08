#pragma once
#include <chrono>
#include <cstdint>
#include <string_view>

#include "lf/result.hpp"

namespace lf {

struct OsVersion {
    uint32_t major = 0, minor = 0, build = 0;
};

// RtlGetVersion による実バージョン (互換性シムの影響を受けない)。
OsVersion detectOsVersion();

// 最後に Windows が起動した時刻 (稼働時間から逆算)。高速スタートアップのシャットダウンではリセットされない
// = 「再起動」しない限り再起動待ちが解消されない、という実態と一致する。
std::chrono::system_clock::time_point systemBootTime();

// システムの復元ポイントを作成する (SRSetRestorePointW)。管理者権限が必要。
// 注意: システム保護が無効だと失敗する。また Windows は既定で 24 時間以内に作成済みなら新規作成をスキップすることがある。
Result<void> createRestorePoint(std::string_view description);

}  // namespace lf
