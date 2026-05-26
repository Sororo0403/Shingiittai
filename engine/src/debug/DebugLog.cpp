#include "debug/DebugLog.h"

#include <Windows.h>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

#pragma warning(push, 0)
#include "nlohmann/json.hpp"
#pragma warning(pop)

namespace {

std::wstring GetExecutableDirectory() {
    std::array<wchar_t, MAX_PATH> pathBuffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
    if (length == 0 || length >= pathBuffer.size()) {
        return L".";
    }

    const std::filesystem::path executablePath(
        std::wstring(pathBuffer.data(), length));
    return executablePath.parent_path().wstring();
}

std::wstring MakeTimestampName(const wchar_t *prefix,
                               const wchar_t *extension) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::wostringstream name;
    name << prefix << std::put_time(&localTime, L"%Y%m%d_%H%M%S")
         << extension;
    return name.str();
}

std::ofstream *AsStream(void *stream) {
    return static_cast<std::ofstream *>(stream);
}

std::string NarrowPath(const std::wstring &path) {
    return std::filesystem::path(path).generic_string();
}

} // namespace

DebugLog &DebugLog::Get() {
    static DebugLog log;
    return log;
}

DebugLog::~DebugLog() { Close(); }

bool DebugLog::Open(const std::wstring &path) {
    Close();

    try {
        const std::filesystem::path logPath(path);
        if (logPath.has_parent_path()) {
            std::filesystem::create_directories(logPath.parent_path());
        }

        auto stream = std::make_unique<std::ofstream>(logPath, std::ios::binary);
        if (!*stream) {
            return false;
        }

        path_ = logPath.wstring();
        stream_ = stream.release();
        enabled_ = true;
        const std::string narrowPath = NarrowPath(path_);
        Write("DebugLog", "File", "opened", "ok", {{"path", narrowPath}});
        return true;
    } catch (...) {
        Close();
        return false;
    }
}

bool DebugLog::OpenDefault() { return Open(MakeDefaultPath()); }

void DebugLog::Close() {
    if (stream_) {
        if (enabled_) {
            Write("DebugLog", "File", "closed", "ok");
        }
        delete AsStream(stream_);
        stream_ = nullptr;
    }

    enabled_ = false;
    path_.clear();
}

void DebugLog::SetFrame(uint64_t frame, float deltaTime) {
    frame_ = frame;
    deltaTime_ = deltaTime;
}

void DebugLog::Write(std::string_view system, std::string_view entity,
                     std::string_view state, std::string_view value,
                     std::initializer_list<DebugLogField> fields) {
    if (!enabled_ || !stream_) {
        return;
    }

    nlohmann::json line;
    line["frame"] = frame_;
    line["dt"] = deltaTime_;
    line["system"] = std::string(system);
    line["entity"] = std::string(entity);
    line["state"] = std::string(state);
    line["value"] = std::string(value);

    if (fields.size() > 0) {
        nlohmann::json data = nlohmann::json::object();
        for (const DebugLogField &field : fields) {
            data[std::string(field.name)] = std::string(field.value);
        }
        line["data"] = std::move(data);
    }

    std::ofstream *stream = AsStream(stream_);
    *stream << line.dump() << '\n';
    stream->flush();
}

std::wstring DebugLog::MakeDefaultPath() const {
    const std::filesystem::path directory =
        std::filesystem::path(GetExecutableDirectory()) / L"logs";
    std::filesystem::path path =
        directory / MakeTimestampName(L"run_", L".jsonl");
    for (int index = 1; std::filesystem::exists(path); ++index) {
        std::wostringstream name;
        name << L"run_" << index << L"_" << MakeTimestampName(L"", L".jsonl");
        path = directory / name.str();
    }
    return path.wstring();
}
