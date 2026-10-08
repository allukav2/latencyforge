#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lf/result.hpp"
#include "lf/sysinfo.hpp"

namespace lf {

// Windows の優先度クラス (SetPriorityClass の値)。
enum class PriorityClass : uint32_t {
    Idle = 0x40,
    BelowNormal = 0x4000,
    Normal = 0x20,
    AboveNormal = 0x8000,
    High = 0x80,
    Realtime = 0x100,
};

struct ProcessEntry {
    uint32_t pid = 0;
    uint32_t parentPid = 0;
    std::string exeName;  // "game.exe" (UTF-8、大文字小文字は元のまま)
};

// 1 プロセスについて、安全判定と復元に必要な情報。
struct ProcessState {
    uint32_t pid = 0;
    uint64_t startTime = 0;  // 作成時刻 (FILETIME)。PID の再利用を見分けるため、変更・復元のたびに照合する
    std::string exeName;
    std::string imagePath;
    GroupMask affinity;      // 現在のアフィニティ (プロセスのプロセッサグループ内)
    uint32_t priorityClass = static_cast<uint32_t>(PriorityClass::Normal);
    bool multiGroup = false;     // 複数のプロセッサグループにまたがる (対象外)
    bool isProtected = false;    // 保護プロセス (PPL など)
    bool isCritical = false;     // 重要なシステムプロセス
    bool sameUser = false;       // 自分と同じユーザー (トークンを読めなければ false)
    bool inWindowsDir = false;   // Windows フォルダ配下の実行ファイル
    uint32_t sessionId = 0;
};

// OS API の境界。実機は WinProcessApi、テストとデモは FakeProcessApi。
// 実装は最小権限 (PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION) だけでプロセスを開く。
// SeDebugPrivilege / PROCESS_ALL_ACCESS / インジェクション / フックは使わない。
class IProcessApi {
public:
    virtual ~IProcessApi() = default;

    virtual std::vector<ProcessEntry> enumerate() = 0;
    // 開けない (保護/アンチチート/権限不足) ・存在しない場合は Error。その場合、呼び出し側はスキップしてログに残す。
    virtual Result<ProcessState> query(uint32_t pid) = 0;
    // startTime が現在のプロセスと一致しなければ ErrorCode::ProcessChanged (PID 再利用の防止)。
    virtual Result<void> setAffinity(uint32_t pid, uint64_t startTime, const GroupMask& mask) = 0;
    virtual Result<void> setPriority(uint32_t pid, uint64_t startTime, uint32_t priorityClass) = 0;

    virtual uint32_t selfPid() const = 0;
    virtual uint32_t currentSessionId() const = 0;
};

}  // namespace lf
