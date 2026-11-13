#include "thesis/metrics/SystemInfo.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>

#if defined(_WIN32)
// Order matters: powrprof.h needs the types from windows.h (separate blocks keep the formatter
// from sorting them).
#include <windows.h>

#include <powrprof.h>
#endif

namespace thesis::metrics {
namespace {

std::string utcNow()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

#if defined(_WIN32)

std::string toUtf8(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string guidToString(const GUID& guid)
{
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", guid.Data1, guid.Data2,
                  guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return buffer;
}

// Windows 10/11 "power mode" slider (an overlay on top of the power plan).
std::string powerModeName(const std::string& guid)
{
    if (guid == "961cc777-2547-4f9d-8174-7d86181b8a7a")
        return "best power efficiency";
    if (guid == "00000000-0000-0000-0000-000000000000")
        return "balanced";
    if (guid == "3af9b8d9-7c97-431d-ad78-34a8bfea439f")
        return "better performance";
    if (guid == "ded574b5-45a0-4f42-8737-46345c09c238")
        return "best performance";
    return "unknown";
}

nlohmann::json capturePower()
{
    nlohmann::json power;

    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status)) {
        power["on_charger"] = status.ACLineStatus == 1;
        if (status.BatteryLifePercent <= 100) {
            power["battery_percent"] = status.BatteryLifePercent;
        }
    }

    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) == ERROR_SUCCESS && scheme != nullptr) {
        power["plan_guid"] = guidToString(*scheme);
        wchar_t name[256]{};
        DWORD size = sizeof(name);
        if (PowerReadFriendlyName(nullptr, scheme, nullptr, nullptr, reinterpret_cast<UCHAR*>(name), &size) ==
            ERROR_SUCCESS) {
            power["plan"] = toUtf8(name);
        }
        LocalFree(scheme);
    }

    // Not in the SDK headers, but exported by powrprof.dll since Windows 10 1709.
    using GetOverlay = DWORD(WINAPI*)(GUID*);
    if (HMODULE module = GetModuleHandleW(L"powrprof.dll")) {
        if (auto getOverlay = reinterpret_cast<GetOverlay>(GetProcAddress(module, "PowerGetEffectiveOverlayScheme"))) {
            GUID overlay{};
            if (getOverlay(&overlay) == ERROR_SUCCESS) {
                const std::string guid = guidToString(overlay);
                power["mode_guid"] = guid;
                power["mode"] = powerModeName(guid);
            }
        }
    }
    return power;
}

// NVML ships with the NVIDIA driver. It is loaded at run time (so the program also runs
// without it), once, and stays loaded: the benchmark polls the clock during warm-up.
class Nvml {
public:
    static Nvml& instance()
    {
        static Nvml nvml;
        return nvml;
    }

    [[nodiscard]] bool available() const { return device_ != nullptr; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    std::optional<std::uint32_t> graphicsClockMhz() const
    {
        unsigned value = 0;
        if (available() && getClock_ && getClock_(device_, kClockGraphics, &value) == 0) {
            return value;
        }
        return std::nullopt;
    }

    nlohmann::json snapshot() const
    {
        if (!available()) {
            return {{"available", false}, {"reason", reason_}};
        }
        nlohmann::json gpu{{"available", true}};
        char name[96]{};
        unsigned value = 0;
        int pstate = 0;
        if (getName_ && getName_(device_, name, sizeof(name)) == 0)
            gpu["name"] = name;
        if (getClock_ && getClock_(device_, kClockGraphics, &value) == 0)
            gpu["graphics_clock_mhz"] = value;
        if (getClock_ && getClock_(device_, kClockMemory, &value) == 0)
            gpu["memory_clock_mhz"] = value;
        if (getTemperature_ && getTemperature_(device_, kTemperatureGpu, &value) == 0)
            gpu["temperature_c"] = value;
        if (getPower_ && getPower_(device_, &value) == 0)
            gpu["power_draw_w"] = value / 1000.0;
        if (getPowerLimit_ && getPowerLimit_(device_, &value) == 0)
            gpu["power_limit_w"] = value / 1000.0;
        if (getPState_ && getPState_(device_, &pstate) == 0)
            gpu["performance_state"] = "P" + std::to_string(pstate);
        return gpu;
    }

    Nvml(const Nvml&) = delete;
    Nvml& operator=(const Nvml&) = delete;

private:
    using Device = struct NvmlDevice*;
    using Init = int (*)();
    using Shutdown = int (*)();
    using GetHandle = int (*)(unsigned, Device*);
    using GetName = int (*)(Device, char*, unsigned);
    using GetUnsigned = int (*)(Device, unsigned*);
    using GetClock = int (*)(Device, int, unsigned*);
    using GetTemperature = int (*)(Device, int, unsigned*);
    using GetPState = int (*)(Device, int*);

    static constexpr int kClockGraphics = 0;
    static constexpr int kClockMemory = 2;
    static constexpr int kTemperatureGpu = 0;

    Nvml()
    {
        library_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library_ == nullptr) {
            library_ = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        }
        if (library_ == nullptr) {
            reason_ = "nvml.dll not found";
            return;
        }
        const auto load = [&](const char* name) { return GetProcAddress(library_, name); };
        const auto init = reinterpret_cast<Init>(load("nvmlInit_v2"));
        shutdown_ = reinterpret_cast<Shutdown>(load("nvmlShutdown"));
        const auto getHandle = reinterpret_cast<GetHandle>(load("nvmlDeviceGetHandleByIndex_v2"));
        getName_ = reinterpret_cast<GetName>(load("nvmlDeviceGetName"));
        getClock_ = reinterpret_cast<GetClock>(load("nvmlDeviceGetClockInfo"));
        getTemperature_ = reinterpret_cast<GetTemperature>(load("nvmlDeviceGetTemperature"));
        getPower_ = reinterpret_cast<GetUnsigned>(load("nvmlDeviceGetPowerUsage"));
        getPowerLimit_ = reinterpret_cast<GetUnsigned>(load("nvmlDeviceGetEnforcedPowerLimit"));
        getPState_ = reinterpret_cast<GetPState>(load("nvmlDeviceGetPerformanceState"));

        if (init == nullptr || shutdown_ == nullptr || getHandle == nullptr || init() != 0) {
            reason_ = "NVML could not be initialised";
            shutdown_ = nullptr;
            return;
        }
        if (getHandle(0, &device_) != 0) {
            device_ = nullptr;
            reason_ = "no NVIDIA GPU found by NVML";
        }
    }

    ~Nvml()
    {
        if (shutdown_ != nullptr) {
            shutdown_();
        }
        if (library_ != nullptr) {
            FreeLibrary(library_);
        }
    }

    HMODULE library_ = nullptr;
    Device device_ = nullptr;
    std::string reason_;
    Shutdown shutdown_ = nullptr;
    GetName getName_ = nullptr;
    GetClock getClock_ = nullptr;
    GetTemperature getTemperature_ = nullptr;
    GetUnsigned getPower_ = nullptr;
    GetUnsigned getPowerLimit_ = nullptr;
    GetPState getPState_ = nullptr;
};

#endif

} // namespace

nlohmann::json captureSystemState()
{
    nlohmann::json state;
    state["time_utc"] = utcNow();
#if defined(_WIN32)
    state["power"] = capturePower();
    state["gpu"] = Nvml::instance().snapshot();
#else
    state["power"] = {{"available", false}};
    state["gpu"] = {{"available", false}};
#endif
    return state;
}

std::optional<std::uint32_t> currentGraphicsClockMhz()
{
#if defined(_WIN32)
    return Nvml::instance().graphicsClockMhz();
#else
    return std::nullopt;
#endif
}

} // namespace thesis::metrics
