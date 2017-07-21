// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#pragma once

#include "core/CompileConfig.h"

#if OUZEL_SUPPORTS_METAL

#if defined(__OBJC__)
#import <Metal/Metal.h>
typedef id<MTLBuffer> MTLBufferPtr;
#else
#include <objc/objc.h>
typedef id MTLBufferPtr;
#endif

#include "graphics/BufferResource.h"

namespace ouzel
{
    namespace graphics
    {
        class MeshBufferResourceMetal;

        class BufferResourceMetal: public BufferResource
        {
            friend MeshBufferResourceMetal;
        public:
            BufferResourceMetal();
            virtual ~BufferResourceMetal();

            virtual bool init(Buffer::Usage newUsage, bool newDynamic = true) override;
            virtual bool init(Buffer::Usage newUsage, const void* newData, uint32_t newSize, bool newDynamic) override;

            virtual bool setData(const void* newData, uint32_t newSize) override;

            MTLBufferPtr getBuffer() const { return buffer; }

        protected:
            virtual bool upload() override;

            MTLBufferPtr buffer = Nil;
            uint32_t bufferSize = 0;
        };
    } // namespace graphics
} // namespace ouzel

#endif
