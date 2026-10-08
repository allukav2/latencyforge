#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "lf/registry.hpp"

namespace lftest {

struct SimulatedCrash {};  // プロセス強制終了の代わり。Engine は握りつぶしてはならない。

// メモリ上のレジストリ。キーは大文字小文字を区別しない (実レジストリと同じ)。
class MockRegistry : public lf::IRegistry {
public:
    // 操作ごとのフック。戻り値に Error を返すとその操作が失敗する。op は "read" / "write" / "delete"。
    std::function<std::optional<lf::Error>(const char* op, const lf::RegPath&)> hook;
    std::set<std::string> unsupportedRead;  // これらのパスの read は UnsupportedValueType
    int crashAfterWrites = -1;              // n 回書いた後の次の write で SimulatedCrash を投げる
    int writes = 0, deletes = 0;

    void set(const lf::RegPath& p, const lf::RegValue& v) { m_values[key(p)] = v; }
    bool has(const lf::RegPath& p) const { return m_values.count(key(p)) != 0; }
    std::optional<lf::RegValue> get(const lf::RegPath& p) const {
        auto it = m_values.find(key(p));
        if (it == m_values.end()) return std::nullopt;
        return it->second;
    }
    size_t count() const { return m_values.size(); }

    lf::Result<std::optional<lf::RegValue>> read(const lf::RegPath& p) override {
        if (hook)
            if (auto e = hook("read", p)) return *e;
        if (unsupportedRead.count(key(p))) return lf::Error{lf::ErrorCode::UnsupportedValueType, "unsupported: " + p.display()};
        return get(p);
    }
    lf::Result<void> write(const lf::RegPath& p, const lf::RegValue& v) override {
        if (crashAfterWrites >= 0 && writes >= crashAfterWrites) throw SimulatedCrash{};
        if (hook)
            if (auto e = hook("write", p)) return *e;
        ++writes;
        set(p, v);
        return {};
    }
    lf::Result<void> deleteValue(const lf::RegPath& p) override {
        if (hook)
            if (auto e = hook("delete", p)) return *e;
        ++deletes;
        m_values.erase(key(p));
        return {};
    }

    static std::string key(const lf::RegPath& p) {
        std::string s = p.display();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

private:
    std::map<std::string, lf::RegValue> m_values;
};

}  // namespace lftest
