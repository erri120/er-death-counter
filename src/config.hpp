#pragma once

#include <cstdint>
#include <optional>

#include <windows.h>

namespace config {

enum class Corner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct Display {
    Corner corner = Corner::TopLeft;
    float  scale  = 1.0f;
};

Display load(HINSTANCE dll);

}
