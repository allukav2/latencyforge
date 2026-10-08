#include "lf/registry.hpp"

#include <cstdio>
#include <nlohmann/json.hpp>

namespace lf {

const char* errorKey(ErrorCode c) {
    switch (c) {
        case ErrorCode::InvalidPath: return "error.invalidPath";
        case ErrorCode::PolicyDenied: return "error.policyDenied";
        case ErrorCode::UnsupportedBuild: return "error.unsupportedBuild";
        case ErrorCode::UnsupportedValueType: return "error.unsupportedValueType";
        case ErrorCode::AccessDenied: return "error.accessDenied";
        case ErrorCode::RegistryOpen: return "error.registryOpen";
        case ErrorCode::RegistryRead: return "error.registryRead";
        case ErrorCode::RegistryWrite: return "error.registryWrite";
        case ErrorCode::RegistryDelete: return "error.registryDelete";
        case ErrorCode::StateCorrupt: return "error.stateCorrupt";
        case ErrorCode::StateWrite: return "error.stateWrite";
        case ErrorCode::RollbackIncomplete: return "error.rollbackIncomplete";
        case ErrorCode::PendingTransaction: return "error.pendingTransaction";
        case ErrorCode::DefinitionInvalid: return "error.definitionInvalid";
        case ErrorCode::Io: return "error.io";
        case ErrorCode::NotFound: return "error.notFound";
        case ErrorCode::NotLoaded: return "error.notLoaded";
    }
    return "error.io";
}

const char* typeName(RegType t) {
    switch (t) {
        case RegType::Dword: return "REG_DWORD";
        case RegType::Qword: return "REG_QWORD";
        case RegType::String: return "REG_SZ";
        case RegType::ExpandString: return "REG_EXPAND_SZ";
    }
    return "?";
}

std::optional<RegType> parseTypeName(std::string_view s) {
    if (s == "REG_DWORD") return RegType::Dword;
    if (s == "REG_QWORD") return RegType::Qword;
    if (s == "REG_SZ") return RegType::String;
    if (s == "REG_EXPAND_SZ") return RegType::ExpandString;
    return std::nullopt;
}

const char* hiveName(RegHive h) { return h == RegHive::HKLM ? "HKLM" : "HKCU"; }

std::string RegValue::display() const {
    char buf[64];
    switch (type) {
        case RegType::Dword:
            std::snprintf(buf, sizeof buf, "REG_DWORD 0x%08llX (%llu)", static_cast<unsigned long long>(number),
                          static_cast<unsigned long long>(number));
            return buf;
        case RegType::Qword:
            std::snprintf(buf, sizeof buf, "REG_QWORD 0x%016llX (%llu)", static_cast<unsigned long long>(number),
                          static_cast<unsigned long long>(number));
            return buf;
        default:
            return std::string(typeName(type)) + " \"" + text + "\"";
    }
}

std::string RegPath::keyString() const { return std::string(hiveName(hive)) + "\\" + subkey; }
std::string RegPath::display() const { return keyString() + " :: " + valueName; }

namespace {

bool hasBadChars(std::string_view s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return true;
    return false;
}

}  // namespace

Result<RegPath> parseRegPath(std::string_view key, std::string_view valueName) {
    auto bad = [&](const char* why) {
        return Error{ErrorCode::InvalidPath, std::string(why) + ": " + std::string(key) + " :: " + std::string(valueName)};
    };
    RegPath p;
    std::string_view rest;
    if (key.rfind("HKLM\\", 0) == 0) {
        p.hive = RegHive::HKLM;
        rest = key.substr(5);
    } else if (key.rfind("HKCU\\", 0) == 0) {
        p.hive = RegHive::HKCU;
        rest = key.substr(5);
    } else {
        return bad("key must start with HKLM\\ or HKCU\\");
    }
    if (rest.empty() || rest.size() > 1024) return bad("empty or too long subkey");
    if (hasBadChars(rest) || rest.find('/') != std::string_view::npos) return bad("illegal character in key");

    size_t pos = 0;
    while (pos <= rest.size()) {
        size_t next = rest.find('\\', pos);
        if (next == std::string_view::npos) next = rest.size();
        const std::string_view seg = rest.substr(pos, next - pos);
        if (seg.empty()) return bad("empty key segment");
        if (seg == "." || seg == "..") return bad("relative key segment");
        if (seg.size() > 255) return bad("key segment too long");
        pos = next + 1;
    }
    if (valueName.empty() || valueName.size() > 255) return bad("value name empty or too long");
    if (hasBadChars(valueName) || valueName.find('\\') != std::string_view::npos ||
        valueName.find('/') != std::string_view::npos)
        return bad("illegal character in value name");

    p.subkey = std::string(rest);
    p.valueName = std::string(valueName);
    return p;
}

nlohmann::json valueToJson(const std::optional<RegValue>& v) {
    using nlohmann::json;
    if (!v) return nullptr;
    json j;
    j["type"] = typeName(v->type);
    if (v->isNumeric())
        j["data"] = v->number;
    else
        j["data"] = v->text;
    return j;
}

Result<std::optional<RegValue>> valueFromJson(const nlohmann::json& j) {
    using nlohmann::json;
    auto bad = [](const char* why) { return Error{ErrorCode::StateCorrupt, std::string("invalid value: ") + why}; };
    if (j.is_null()) return std::optional<RegValue>{};
    if (!j.is_object()) return bad("not an object");
    auto t = j.find("type");
    auto d = j.find("data");
    if (t == j.end() || !t->is_string() || d == j.end()) return bad("missing type/data");
    auto type = parseTypeName(t->get<std::string>());
    if (!type) return bad("unknown type");
    RegValue v;
    v.type = *type;
    if (v.isNumeric()) {
        if (!d->is_number_unsigned()) return bad("numeric data must be an unsigned integer");
        v.number = d->get<uint64_t>();
        if (v.type == RegType::Dword && v.number > 0xFFFFFFFFull) return bad("DWORD out of range");
    } else {
        if (!d->is_string()) return bad("string data must be a string");
        v.text = d->get<std::string>();
    }
    return std::optional<RegValue>{std::move(v)};
}

}  // namespace lf
