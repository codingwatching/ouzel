// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#pragma once

#include "core/CompileConfig.h"

#if OUZEL_SUPPORTS_OPENGL

#if OUZEL_SUPPORTS_OPENGLES
    #define GL_GLEXT_PROTOTYPES 1
    #include "GLES/gl.h"
    #include "GLES2/gl2.h"
    #include "GLES2/gl2ext.h"
    #include "GLES3/gl3.h"
#else
    #define GL_GLEXT_PROTOTYPES 1
    #include "GL/glcorearb.h"
    #include "GL/glext.h"
#endif

#include "graphics/TextureResource.h"

namespace ouzel
{
    namespace graphics
    {
        class TextureResourceOGL: public TextureResource
        {
        public:
            TextureResourceOGL();
            virtual ~TextureResourceOGL();

            virtual bool init(const Size2& newSize,
                              bool newDynamic,
                              bool newMipmaps = true,
                              bool newRenderTarget = false,
                              uint32_t newSampleCount = 1,
                              bool newDepth = false,
                              PixelFormat newPixelFormat = PixelFormat::RGBA8_UNORM) override;
            virtual bool init(const std::vector<uint8_t>& newData,
                              const Size2& newSize,
                              bool newDynamic,
                              bool newMipmaps = true,
                              PixelFormat newPixelFormat = PixelFormat::RGBA8_UNORM) override;

            virtual bool setSize(const Size2& newSize) override;
            virtual bool setData(const std::vector<uint8_t>& newData, const Size2& newSize) override;
            virtual bool setFilter(Texture::Filter newFilter) override;
            virtual bool setAddressX(Texture::Address newAddressX) override;
            virtual bool setAddressY(Texture::Address newAddressY) override;
            virtual bool setMaxAnisotropy(uint32_t newMaxAnisotropy) override;
            virtual void setClearColorBuffer(bool clear) override;
            virtual void setClearDepthBuffer(bool clear) override;
            virtual void setClearColor(Color color) override;
            virtual void setFrameBufferClearedFrame(uint32_t clearedFrame) override;

            GLuint getTextureId() const { return textureId; }

            GLuint getFrameBufferId() const { return frameBufferId; }

            GLbitfield getClearMask() const { return clearMask; }
            const GLfloat* getFrameBufferClearColor() const { return frameBufferClearColor; }

        protected:
            virtual bool upload() override;

            GLuint textureId = 0;

            GLsizei width = 0;
            GLsizei height = 0;

            GLuint frameBufferId = 0;
            GLuint depthBufferId = 0;

            GLbitfield clearMask = 0;
            GLfloat frameBufferClearColor[4];
        };
    } // namespace graphics
} // namespace ouzel

#endif
