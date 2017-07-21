// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#include "core/CompileConfig.h"

#if OUZEL_SUPPORTS_DIRECT3D11

#include "MeshBufferResourceD3D11.h"
#include "BufferResourceD3D11.h"
#include "utils/Log.h"

namespace ouzel
{
    namespace graphics
    {
        MeshBufferResourceD3D11::MeshBufferResourceD3D11()
        {
        }

        MeshBufferResourceD3D11::~MeshBufferResourceD3D11()
        {
        }

        bool MeshBufferResourceD3D11::init(uint32_t newIndexSize, BufferResource* newIndexBuffer,
                                           const std::vector<VertexAttribute>& newVertexAttributes, BufferResource* newVertexBuffer)
        {
            return true;
        }

        bool MeshBufferResourceD3D11::setIndexSize(uint32_t newIndexSize)
        {
            return true;
        }

        bool MeshBufferResourceD3D11::setIndexBuffer(BufferResource* newIndexBuffer)
        {
            return true;
        }

        bool MeshBufferResourceD3D11::setVertexAttributes(const std::vector<VertexAttribute>& newVertexAttributes)
        {
            return true;
        }

        bool MeshBufferResourceD3D11::setVertexBuffer(BufferResource* newVertexBuffer)
        {
            return true;
        }

        bool MeshBufferResourceD3D11::upload()
        {
            std::lock_guard<std::mutex> lock(uploadMutex);

            if (dirty)
            {
                switch (indexSize)
                {
                case 2:
                    indexFormat = DXGI_FORMAT_R16_UINT;
                    break;
                case 4:
                    indexFormat = DXGI_FORMAT_R32_UINT;
                    break;
                default:
                    indexFormat = DXGI_FORMAT_UNKNOWN;
                    Log(Log::Level::ERR) << "Invalid index size";
                    return false;
                }

                indexBufferD3D11 = static_cast<BufferResourceD3D11*>(indexBuffer);
                vertexBufferD3D11 = static_cast<BufferResourceD3D11*>(vertexBuffer);

                dirty = 0;
            }

            return true;
        }
    } // namespace graphics
} // namespace ouzel

#endif
