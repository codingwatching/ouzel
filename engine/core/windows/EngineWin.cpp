// Ouzel by Elviss Strazdins

#include <cstdlib>
#include <system_error>

#pragma push_macro("WIN32_LEAN_AND_MEAN")
#pragma push_macro("NOMINMAX")
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#pragma pop_macro("WIN32_LEAN_AND_MEAN")
#pragma pop_macro("NOMINMAX")

#include "EngineWin.hpp"
#include "NativeWindowWin.hpp"
#include "../../platform/winapi/ShellExecuteErrorCategory.hpp"
#include "../../input/windows/InputSystemWin.hpp"
#include "../../utils/Bit.hpp"
#include "../../utils/Log.hpp"

namespace ouzel::core::windows
{
    namespace
    {
        const platform::winapi::ShellExecuteErrorCategory shellExecuteErrorCategory{};

        void translateMessage(HWND window, const std::set<HACCEL>& accelerators, MSG& message)
        {
            bool translate = true;

            for (auto accelerator : accelerators)
                if (TranslateAccelerator(window, accelerator, &message))
                    translate = false;

            if (translate)
            {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
        }
    }

    Engine::Engine(const std::vector<std::string>& args):
        core::Engine{args}
    {
    }

    int Engine::run()
    {
        start();

        auto& inputSystem = inputManager.getInputSystem();
        const auto& nativeWindow = window.getNativeWindow();

        while (isActive())
        {
            if (!isPaused())
            {
                MSG message;
                if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    translateMessage(nativeWindow.getNativeWindow(),
                                     nativeWindow.accelerators, message);

                    if (message.message == WM_QUIT)
                    {
                        exit();
                        return static_cast<int>((message.wParam));
                    }
                }
            }
            else
            {
                MSG message;
                if (const auto ret = GetMessage(&message, nullptr, 0, 0); ret == -1)
                    throw std::system_error{static_cast<int>(GetLastError()), std::system_category(), "Failed to get message"};
                else if (ret == 0)
                {
                    exit();
                    return static_cast<int>((message.wParam));
                }
                else
                    translateMessage(nativeWindow.getNativeWindow(),
                                     nativeWindow.accelerators,
                                     message);
            }

            inputSystem.update();
        }

        exit();

        return EXIT_SUCCESS;
    }

    void Engine::runOnMainThread(const std::function<void()>& func)
    {
        const auto& nativeWindow = window.getNativeWindow();

        std::unique_lock lock{executeMutex};
        executeQueue.push(func);
        lock.unlock();

        if (!PostMessage(nativeWindow.getNativeWindow(), WM_USER, 0, 0))
            throw std::system_error{static_cast<int>(GetLastError()), std::system_category(), "Failed to post message"};
    }

    void Engine::executeAll()
    {
        std::function<void()> func;

        for (;;)
        {
            std::unique_lock lock{executeMutex};

            if (executeQueue.empty())
                break;

            func = std::move(executeQueue.front());
            executeQueue.pop();
            lock.unlock();

            if (func) func();
        }
    }

    void Engine::openUrl(const std::string& url)
    {
        const auto charCount = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
        if (charCount == 0)
            throw std::system_error{static_cast<int>(GetLastError()), std::system_category(), "Failed to convert UTF-8 to wide char"};

        auto buffer = std::make_unique<WCHAR[]>(charCount);
        if (MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, buffer.get(), charCount) == 0)
            throw std::system_error{static_cast<int>(GetLastError()), std::system_category(), "Failed to convert UTF-8 to wide char"};

        // Result of the ShellExecuteW can be cast only to an int (https://docs.microsoft.com/en-us/windows/desktop/api/shellapi/nf-shellapi-shellexecutew)
        const auto result = ShellExecuteW(nullptr, L"open", buffer.get(), nullptr, nullptr, SW_SHOWNORMAL);
        if (const auto status = bitCast<std::intptr_t>(result); status <= 32)
            throw std::system_error{static_cast<int>(status), shellExecuteErrorCategory, "Failed to execute open"};
    }
}
