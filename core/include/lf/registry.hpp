#pragma once
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "lf/result.hpp"

namespace lf {

enum class RegHive { HKLM, HKCU };
enum class RegType { Dword, Qword, String, ExpandString };

struct RegValue {
    RegType type = RegType::Dword;
    uint64_t number = 0;  // Dword / Qword
    std::string text;     // String / ExpandString (UTF-8)

    static RegValue dword(uint32_t v) { return {RegType::Dword, v, {}}; }
    static RegValue qword(uint64_t v) { return {RegType::Qword, v, {}}; }
    static RegValue string(std::string s) { return {RegType::String, 0, std::move(s)}; }
    static RegValue expand(std::string s) { return {RegType::ExpandString, 0, std::move(s)}; }

    bool isNumeric() const { return type == RegType::Dword || type == RegType::Qword; }
    bool operator==(const RegValue& o) const {
        return type == o.type && (isNumeric() ? number == o.number : text == o.text);
    }
    std::string display() const;  // ログ/UI 用 ("REG_DWORD 0x00000001 (1)" 等)
};

// 値 1 つを指す。subkey は "SYSTEM\\...\\kernel" 形式 (ハイブ名を含まない)。
struct RegPath {
    RegHive hive = RegHive::HKLM;
    std::string subkey;
    std::string valueName;

    std::string keyString() const;   // "HKLM\\SYSTEM\\..."
    std::string display() const;     // keyString + " :: " + valueName
    bool operator==(const RegPath&) const = default;
};

const char* typeName(RegType t);                         // "REG_DWORD" ...
std::optional<RegType> parseTypeName(std::string_view);  // 厳密一致 (大文字)
const char* hiveName(RegHive h);

// "HKLM\\a\\b" + "Value" を検証して RegPath にする。
// 拒否: HKLM/HKCU 以外、空セグメント、"." ".."、'/'、制御文字、過長、空/バックスラッシュ入りの値名。
Result<RegPath> parseRegPath(std::string_view key, std::string_view valueName);

nlohmann::json valueToJson(const std::optional<RegValue>& v);       // null = 存在しない
Result<std::optional<RegValue>> valueFromJson(const nlohmann::json& j);

// レジストリ抽象。実機実装は WinRegistry、テストではモック。
class IRegistry {
public:
    virtual ~IRegistry() = default;
    // 値が (キーごと) 存在しなければ nullopt。バックアップできない型は UnsupportedValueType。
    virtual Result<std::optional<RegValue>> read(const RegPath& path) = 0;
    // キーが無ければ作成して書く。
    virtual Result<void> write(const RegPath& path, const RegValue& value) = 0;
    // 存在しなくても成功 (冪等)。
    virtual Result<void> deleteValue(const RegPath& path) = 0;
};

}  // namespace lf
