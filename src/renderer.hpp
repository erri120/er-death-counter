#pragma once

#include <cstdint>
#include <optional>

#include <windows.h>

namespace renderer {

bool init(HINSTANCE dll);
void shutdown();

void set_death_count(std::optional<std::uint32_t> total, std::uint32_t session,
                     std::optional<std::uint32_t> boss_tries);

}
