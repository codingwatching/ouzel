// Ouzel by Elviss Strazdins

#ifndef OUZEL_CORE_NATIVEWINDOWEM_HPP
#define OUZEL_CORE_NATIVEWINDOWEM_HPP

#include "../NativeWindow.hpp"

namespace ouzel::core::emscripten
{
    class NativeWindow final: public core::NativeWindow
    {
    public:
        NativeWindow(const std::function<void(const Event&)>& initCallback,
                     const Size2U& newSize,
                     bool newFullscreen,
                     const std::string& newTitle,
                     bool newHighDpi);

        void setSize(const Size2U& newSize);
        void setFullscreen(bool newFullscreen);

        void handleResize();
        void handleFullscreenChange(bool newFullscreen);
    
    private:
        void executeCommand(const Command& command) final;
    };
}

#endif // OUZEL_CORE_NATIVEWINDOWEM_HPP
