// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#pragma once

#include <memory>
#include <atomic>

namespace ouzel
{
    namespace graphics
    {
        class Renderer;

        class Resource
        {
            friend Renderer;
        public:
            Resource(): dirty(false) {}

            virtual void free() = 0;

        protected:
            virtual void update() = 0;
            virtual bool upload() = 0;
            
            std::atomic<bool> dirty;
        };
    } // graphics
} // ouzel
