#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "ini.h"

#include "logger.hpp"

namespace config {

namespace {

std::string config_path(HINSTANCE dll) {
    std::wstring buf(MAX_PATH, L'\0');
    const auto len = ::GetModuleFileNameW(dll, buf.data(),
                                          static_cast<DWORD>(buf.size()));
    if (len == 0 || len == buf.size()) {
        return {};
    }
    buf.resize(len);

    const auto dir = std::filesystem::path(buf).parent_path();
    return (dir / L"death_counter.ini").string();
}

std::string_view trim(std::string_view s) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto begin     = std::find_if(s.begin(), s.end(), not_space);
    const auto end       = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return begin <= end
        ? std::string_view{begin, end}
        : std::string_view{};
}

std::string_view lower(std::string_view s) {
    static std::string buf;
    buf.assign(s);
    std::transform(buf.begin(), buf.end(), buf.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return buf;
}

std::optional<Corner> parse_corner(std::string_view value) {
    if (value == "top_left") {
        return Corner::TopLeft;
    }
    if (value == "top_right") {
        return Corner::TopRight;
    }
    if (value == "bottom_left") {
        return Corner::BottomLeft;
    }
    if (value == "bottom_right") {
        return Corner::BottomRight;
    }
    return std::nullopt;
}

int handler(void* user, const char* section, const char* name,
            const char* value) {
    if (std::string_view{section} != "display") {
        return 1;
    }

    auto* display = static_cast<Display*>(user);
    const auto key = trim(name);
    const auto val = trim(value);

    if (key == "corner") {
        if (const auto corner = parse_corner(lower(val))) {
            display->corner = *corner;
        } else {
            logger::log("[death-counter] invalid corner '{}' in config, using default",
                        std::string{val});
        }
        return 1;
    }

    if (key == "scale") {
        const auto* first  = val.data();
        const auto* last   = val.data() + val.size();
        float        scale = 0.0f;
        const auto [ptr, ec] = std::from_chars(first, last, scale);
        if (ec == std::errc{} && ptr == last && scale > 0.0f) {
            const auto clamped = std::clamp(scale, 0.25f, 5.0f);
            if (clamped != scale) {
                logger::log("[death-counter] scale {} out of range, clamped to {}",
                            scale, clamped);
            }
            display->scale = clamped;
        } else {
            logger::log("[death-counter] invalid scale '{}' in config, using default",
                        std::string{val});
        }
        return 1;
    }

    return 1;
}

}

Display load(HINSTANCE dll) {
    Display display{};

    const auto path = config_path(dll);
    if (path.empty()) {
        logger::log("[death-counter] could not resolve config path, using defaults");
        return display;
    }

    const auto result = ini_parse(path.c_str(), handler, &display);
    if (result > 0) {
        logger::log("[death-counter] invalid config at line {}, using defaults for "
                    "the remaining values", result);
    }

    return display;
}

}
