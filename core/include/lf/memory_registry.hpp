#pragma once
#include <algorithm>
#include <cctype>
#include <map>
#include <string>

#include "lf/registry.hpp"

namespace lf {

// メモリ上だけのレジストリ。デモモード (--demo) で UI を実機に触れずに動かすために使う。
// キーは大文字小文字を区別しない (実レジストリと同じ)。
class MemoryRegistry final : public IRegistry {
public:
    void set(const RegPath& p, const RegValue& v) { m_values[key(p)] = v; }
    std::optional<RegValue> get(const RegPath& p) const {
        auto it = m_values.find(key(p));
        if (it == m_values.end()) return std::nullopt;
        return it->second;
    }
    size_t size() const { return m_values.size(); }

    Result<std::optional<RegValue>> read(const RegPath& p) override { return get(p); }
    Result<void> write(const RegPath& p, const RegValue& v) override {
        set(p, v);
        return {};
    }
    Result<void> deleteValue(const RegPath& p) override {
        m_values.erase(key(p));
        return {};
    }

private:
    static std::string key(const RegPath& p) {
        std::string s = p.display();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
    std::map<std::string, RegValue> m_values;
};

}  // namespace lf
