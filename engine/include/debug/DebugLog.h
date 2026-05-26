#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

struct DebugLogField {
    std::string_view name;
    std::string_view value;
};

class DebugLog {
  public:
    static DebugLog &Get();

    bool Open(const std::wstring &path);
    bool OpenDefault();
    void Close();

    bool IsEnabled() const { return enabled_; }
    const std::wstring &GetPath() const { return path_; }

    void SetFrame(uint64_t frame, float deltaTime);
    void Write(std::string_view system, std::string_view entity,
               std::string_view state, std::string_view value,
               std::initializer_list<DebugLogField> fields = {});

  private:
    DebugLog() = default;
    ~DebugLog();

    DebugLog(const DebugLog &) = delete;
    DebugLog &operator=(const DebugLog &) = delete;

    std::wstring MakeDefaultPath() const;

    uint64_t frame_ = 0;
    float deltaTime_ = 0.0f;
    bool enabled_ = false;
    std::wstring path_;
    void *stream_ = nullptr;
};
