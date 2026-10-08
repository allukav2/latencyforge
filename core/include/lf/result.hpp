#pragma once
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace lf {

// ユーザー向けメッセージは data/lang の "error.<キー>" で解決する。detail は技術詳細 (UI では展開表示)。
enum class ErrorCode {
    InvalidPath,
    PolicyDenied,
    UnsupportedBuild,
    UnsupportedValueType,
    AccessDenied,
    RegistryOpen,
    RegistryRead,
    RegistryWrite,
    RegistryDelete,
    StateCorrupt,
    StateWrite,
    RollbackIncomplete,
    PendingTransaction,
    DefinitionInvalid,
    Io,
    NotFound,
    NotLoaded,
    RestorePointFailed,
};

const char* errorKey(ErrorCode code);  // 例: "error.policyDenied"

struct Error {
    ErrorCode code = ErrorCode::Io;
    std::string detail;
    unsigned long win32 = 0;  // 0 = なし
};

template <class T>
class [[nodiscard]] Result {
public:
    Result(Error e) : m_v(std::in_place_index<1>, std::move(e)) {}
    template <class U, class = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Error> &&
                                                !std::is_same_v<std::decay_t<U>, Result> &&
                                                std::is_constructible_v<T, U&&>>>
    Result(U&& v) : m_v(std::in_place_index<0>, std::forward<U>(v)) {}

    bool ok() const { return m_v.index() == 0; }
    explicit operator bool() const { return ok(); }
    T& value() { return std::get<0>(m_v); }
    const T& value() const { return std::get<0>(m_v); }
    const Error& error() const { return std::get<1>(m_v); }

private:
    std::variant<T, Error> m_v;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error e) : m_err(std::move(e)) {}
    bool ok() const { return !m_err.has_value(); }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return *m_err; }

private:
    std::optional<Error> m_err;
};

}  // namespace lf
