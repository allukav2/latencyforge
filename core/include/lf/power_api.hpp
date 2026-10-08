#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lf/registry.hpp"

namespace lf {

// GUID 文字列の正規化: "{ABC...}" → 小文字・波括弧なし。形式 (8-4-4-4-12 の 16 進) が不正なら空文字。
std::string normalizeGuid(std::string_view guid);

// 電源プランの設定の OS API の境界 (powrprof の文書化された関数だけ)。実機は WinPowerApi、テストとデモは FakePowerApi。
// 引数の GUID は normalizeGuid 済みの形。
class IPowerApi {
public:
    virtual ~IPowerApi() = default;

    virtual Result<std::string> activeSchemeGuid() = 0;                      // PowerGetActiveScheme
    virtual Result<std::string> schemeName(const std::string& scheme) = 0;   // PowerReadFriendlyName
    // 設定が (その電源プランに) 無ければ nullopt。PowerReadACValueIndex / PowerReadDCValueIndex
    virtual Result<std::optional<uint32_t>> readIndex(const std::string& scheme, const std::string& subgroup,
                                                      const std::string& setting, bool ac) = 0;
    // PowerWriteACValueIndex / PowerWriteDCValueIndex
    virtual Result<void> writeIndex(const std::string& scheme, const std::string& subgroup, const std::string& setting, bool ac,
                                    uint32_t value) = 0;
    // 変更を反映する (PowerSetActiveScheme)。アクティブな電源プランを書き換えたときに呼ぶ。
    virtual Result<void> applyScheme(const std::string& scheme) = 0;
};

// 電源設定を IRegistry の形に見せるアダプタ。パスの形式は registry.hpp の RegHive::Power を参照。
//  - 電源プランは具体的な GUID が必須 ("ACTIVE" は定義のテンプレート用で、ここでは拒否する = 呼び出し側が解決する)。
//  - 値は DWORD のみ。存在しない設定は UnsupportedValueType (= 適用しない)。削除はできない (復元は元の値を書き戻す)。
class PowerRegistry final : public IRegistry {
public:
    explicit PowerRegistry(IPowerApi& api) : m_api(api) {}
    Result<std::optional<RegValue>> read(const RegPath& path) override;
    Result<void> write(const RegPath& path, const RegValue& value) override;
    Result<void> deleteValue(const RegPath& path) override;

private:
    struct Target {
        std::string scheme, subgroup, setting;
        bool ac = true;
    };
    Result<Target> parse(const RegPath& path) const;
    IPowerApi& m_api;
};

// HKLM/HKCU は base へ、POWER は power へ振り分ける。
class RoutingRegistry final : public IRegistry {
public:
    RoutingRegistry(std::unique_ptr<IRegistry> base, std::unique_ptr<IRegistry> power)
        : m_base(std::move(base)), m_power(std::move(power)) {}
    Result<std::optional<RegValue>> read(const RegPath& p) override { return pick(p).read(p); }
    Result<void> write(const RegPath& p, const RegValue& v) override { return pick(p).write(p, v); }
    Result<void> deleteValue(const RegPath& p) override { return pick(p).deleteValue(p); }

private:
    IRegistry& pick(const RegPath& p) { return p.hive == RegHive::Power ? *m_power : *m_base; }
    std::unique_ptr<IRegistry> m_base, m_power;
};

// メモリ上の電源プラン (テストと --demo)。実機の電源プランには一切触れない。
class FakePowerApi final : public IPowerApi {
public:
    struct Scheme {
        std::string name;
        // キー: subgroup + "/" + setting + (ac ? "/ac" : "/dc")
        std::map<std::string, uint32_t> values;
    };
    std::map<std::string, Scheme> schemes;
    std::string active;
    std::vector<std::string> calls;  // "write <scheme> <sub>/<set>/ac=<v>" など
    std::function<std::optional<Error>(const char* op)> hook;  // Error を返すとその操作が失敗する

    static std::string key(const std::string& sub, const std::string& set, bool ac) { return sub + "/" + set + (ac ? "/ac" : "/dc"); }

    void addScheme(const std::string& guid, const std::string& name) { schemes[guid].name = name; }
    void setValue(const std::string& scheme, const std::string& sub, const std::string& set, bool ac, uint32_t v) {
        schemes[scheme].values[key(sub, set, ac)] = v;
    }
    std::optional<uint32_t> value(const std::string& scheme, const std::string& sub, const std::string& set, bool ac) const {
        auto s = schemes.find(scheme);
        if (s == schemes.end()) return std::nullopt;
        auto v = s->second.values.find(key(sub, set, ac));
        if (v == s->second.values.end()) return std::nullopt;
        return v->second;
    }

    Result<std::string> activeSchemeGuid() override {
        if (auto e = fail("activeSchemeGuid")) return *e;
        if (active.empty()) return Error{ErrorCode::NotFound, "no active power scheme"};
        return active;
    }
    Result<std::string> schemeName(const std::string& scheme) override {
        auto s = schemes.find(scheme);
        if (s == schemes.end()) return Error{ErrorCode::NotFound, "no such power scheme"};
        return s->second.name;
    }
    Result<std::optional<uint32_t>> readIndex(const std::string& scheme, const std::string& sub, const std::string& set, bool ac) override {
        if (auto e = fail("read")) return *e;
        if (!schemes.count(scheme)) return Error{ErrorCode::NotFound, "no such power scheme"};
        return value(scheme, sub, set, ac);
    }
    Result<void> writeIndex(const std::string& scheme, const std::string& sub, const std::string& set, bool ac, uint32_t v) override {
        calls.push_back("write " + scheme + " " + key(sub, set, ac) + "=" + std::to_string(v));
        if (auto e = fail("write")) return *e;
        if (!schemes.count(scheme)) return Error{ErrorCode::NotFound, "no such power scheme"};
        setValue(scheme, sub, set, ac, v);
        return {};
    }
    Result<void> applyScheme(const std::string& scheme) override {
        calls.push_back("apply " + scheme);
        if (auto e = fail("apply")) return *e;
        return {};
    }

private:
    std::optional<Error> fail(const char* op) { return hook ? hook(op) : std::nullopt; }
};

}  // namespace lf
