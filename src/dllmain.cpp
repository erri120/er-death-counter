#include <thread>

#include <windows.h>

#include "logger.hpp"
#include "renderer.hpp"
#include "worker.hpp"

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstance, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    ::DisableThreadLibraryCalls(hinstance);

    if (!logger::instance().init(hinstance)) {
        return TRUE;
    }

    logger::log("[death-counter] DllMain called, DLL loaded");

    std::thread([hinstance] {
        if (!renderer::init(hinstance)) {
            logger::log("[death-counter] renderer init failed, mod disabled");
            return;
        }

        static std::jthread worker_thread{worker::run};
        (void)worker_thread;
    }).detach();

    (void)reserved;
    return TRUE;
}
