#include "lf/win_registry.hpp"

#include <windows.h>

#include <cstring>
#include <vector>

#include "lf/util.hpp"

namespace lf {
namespace {

// RAII: HKEY を必ず閉じる。
class RegKey {
public:
    RegKey() = default;
    ~RegKey() { reset(); }
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
    RegKey(RegKey&& o) noexcept : m_key(o.m_key) { o.m_key = nullptr; }
    RegKey& operator=(RegKey&& o) noexcept {
        if (this != &o) {
            reset();
            m_key = o.m_key;
            o.m_key = nullptr;
        }
        return *this;
    }
    HKEY* put() {
        reset();
        return &m_key;
    }
    HKEY get() const { return m_key; }
    void reset() {
        if (m_key) RegCloseKey(m_key);
        m_key = nullptr;
    }

private:
    HKEY m_key = nullptr;
};

HKEY rootOf(RegHive h) { return h == RegHive::HKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

Error winError(ErrorCode generic, LONG rc, const RegPath& p, const char* op) {
    const ErrorCode code = (rc == ERROR_ACCESS_DENIED) ? ErrorCode::AccessDenied : generic;
    return Error{code, std::string(op) + " failed for " + p.display(), static_cast<unsigned long>(rc)};
}

bool isMissing(LONG rc) { return rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND; }

}  // namespace

Result<std::optional<RegValue>> WinRegistry::read(const RegPath& p) {
    RegKey key;
    LONG rc = RegOpenKeyExW(rootOf(p.hive), widen(p.subkey).c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, key.put());
    if (isMissing(rc)) return std::optional<RegValue>{};
    if (rc != ERROR_SUCCESS) return winError(ErrorCode::RegistryOpen, rc, p, "RegOpenKeyEx");

    const std::wstring name = widen(p.valueName);
    std::vector<BYTE> buf(256);
    DWORD type = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        DWORD size = static_cast<DWORD>(buf.size());
        rc = RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, buf.data(), &size);
        if (rc == ERROR_MORE_DATA) {
            buf.resize(size + 16);  // 値が読み取りの間に大きくなる場合に備え再試行
            continue;
        }
        if (isMissing(rc)) return std::optional<RegValue>{};
        if (rc != ERROR_SUCCESS) return winError(ErrorCode::RegistryRead, rc, p, "RegQueryValueEx");
        buf.resize(size);
        break;
    }
    if (rc != ERROR_SUCCESS) return winError(ErrorCode::RegistryRead, rc, p, "RegQueryValueEx");

    auto unsupported = [&]() {
        return Error{ErrorCode::UnsupportedValueType,
                     "existing value has a type/size that cannot be backed up faithfully (type " + std::to_string(type) +
                         "): " + p.display()};
    };
    switch (type) {
        case REG_DWORD: {
            if (buf.size() != 4) return unsupported();
            DWORD v = 0;
            std::memcpy(&v, buf.data(), 4);
            return std::optional<RegValue>{RegValue::dword(v)};
        }
        case REG_QWORD: {
            if (buf.size() != 8) return unsupported();
            uint64_t v = 0;
            std::memcpy(&v, buf.data(), 8);
            return std::optional<RegValue>{RegValue::qword(v)};
        }
        case REG_SZ:
        case REG_EXPAND_SZ: {
            if (buf.size() % 2 != 0) return unsupported();
            std::wstring w(reinterpret_cast<const wchar_t*>(buf.data()), buf.size() / 2);
            while (!w.empty() && w.back() == L'\0') w.pop_back();
            // 途中に NUL を含む値は忠実に復元できない。
            if (w.find(L'\0') != std::wstring::npos) return unsupported();
            return std::optional<RegValue>{type == REG_SZ ? RegValue::string(narrow(w)) : RegValue::expand(narrow(w))};
        }
        default:
            return unsupported();
    }
}

Result<void> WinRegistry::write(const RegPath& p, const RegValue& v) {
    RegKey key;
    LONG rc = RegCreateKeyExW(rootOf(p.hive), widen(p.subkey).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, key.put(), nullptr);
    if (rc != ERROR_SUCCESS) return winError(ErrorCode::RegistryOpen, rc, p, "RegCreateKeyEx");

    const std::wstring name = widen(p.valueName);
    switch (v.type) {
        case RegType::Dword: {
            const DWORD d = static_cast<DWORD>(v.number);
            rc = RegSetValueExW(key.get(), name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof d);
            break;
        }
        case RegType::Qword: {
            const uint64_t q = v.number;
            rc = RegSetValueExW(key.get(), name.c_str(), 0, REG_QWORD, reinterpret_cast<const BYTE*>(&q), sizeof q);
            break;
        }
        case RegType::String:
        case RegType::ExpandString: {
            const std::wstring w = widen(v.text);
            rc = RegSetValueExW(key.get(), name.c_str(), 0, v.type == RegType::String ? REG_SZ : REG_EXPAND_SZ,
                                reinterpret_cast<const BYTE*>(w.c_str()), static_cast<DWORD>((w.size() + 1) * sizeof(wchar_t)));
            break;
        }
    }
    if (rc != ERROR_SUCCESS) return winError(ErrorCode::RegistryWrite, rc, p, "RegSetValueEx");
    return {};
}

Result<void> WinRegistry::deleteValue(const RegPath& p) {
    RegKey key;
    LONG rc = RegOpenKeyExW(rootOf(p.hive), widen(p.subkey).c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, key.put());
    if (isMissing(rc)) return {};
    if (rc != ERROR_SUCCESS) return winError(ErrorCode::RegistryOpen, rc, p, "RegOpenKeyEx");
    rc = RegDeleteValueW(key.get(), widen(p.valueName).c_str());
    if (rc != ERROR_SUCCESS && !isMissing(rc)) return winError(ErrorCode::RegistryDelete, rc, p, "RegDeleteValue");
    return {};
}

}  // namespace lf
