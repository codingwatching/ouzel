// Copyright 2015-2020 Elviss Strazdins. All rights reserved.

#include "../../core/Setup.h"

#if OUZEL_COMPILE_DIRECT3D11

#include <cassert>
#include "D3D11RenderDevice.hpp"
#include "D3D11BlendState.hpp"
#include "D3D11Buffer.hpp"
#include "D3D11DepthStencilState.hpp"
#include "D3D11RenderTarget.hpp"
#include "D3D11Shader.hpp"
#include "D3D11Texture.hpp"
#include "../../core/Engine.hpp"
#include "../../core/Window.hpp"
#include "../../core/windows/NativeWindowWin.hpp"
#include "../../utils/Log.hpp"
#include "stb_image_write.h"

namespace ouzel::graphics::d3d11
{
    namespace
    {
        class ErrorCategory final: public std::error_category
        {
        public:
            const char* name() const noexcept final
            {
                return "Direct3D11";
            }

            std::string message(int condition) const final
            {
                switch (condition)
                {
                    case D3D11_ERROR_FILE_NOT_FOUND: return "D3D11_ERROR_FILE_NOT_FOUND";
                    case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS: return "D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS";
                    case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS: return "D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS";
                    case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD: return "D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD";
                    case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
                    case DXGI_ERROR_WAS_STILL_DRAWING: return "DXGI_ERROR_WAS_STILL_DRAWING";
                    case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";
                    case E_FAIL: return "E_FAIL";
                    case E_INVALIDARG: return "E_INVALIDARG";
                    case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
                    case E_NOTIMPL: return "E_NOTIMPL";
                    default: return "Unknown error (" + std::to_string(condition) + ")";
                }
            }
        };

        const ErrorCategory errorCategory {};

        constexpr DXGI_FORMAT getIndexFormat(std::uint32_t indexSize)
        {
            switch (indexSize)
            {
                case 2: return DXGI_FORMAT_R16_UINT;
                case 4: return DXGI_FORMAT_R32_UINT;
                default: throw std::runtime_error("Invalid index size");
            }
        }

        constexpr D3D_PRIMITIVE_TOPOLOGY getPrimitiveTopology(DrawMode drawMode)
        {
            switch (drawMode)
            {
                case DrawMode::pointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                case DrawMode::lineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                case DrawMode::lineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                case DrawMode::triangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                case DrawMode::triangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                default: throw std::runtime_error("Invalid draw mode");
            }
        }

        constexpr D3D11_TEXTURE_ADDRESS_MODE getTextureAddressMode(SamplerAddressMode address)
        {
            switch (address)
            {
                case SamplerAddressMode::clampToEdge:
                    return D3D11_TEXTURE_ADDRESS_CLAMP;
                case SamplerAddressMode::clampToBorder:
                    return D3D11_TEXTURE_ADDRESS_BORDER;
                case SamplerAddressMode::repeat:
                    return D3D11_TEXTURE_ADDRESS_WRAP;
                case SamplerAddressMode::mirrorRepeat:
                    return D3D11_TEXTURE_ADDRESS_MIRROR;
                default:
                    throw std::runtime_error("Invalid address mode");
            }
        }
    }

    const std::error_category& getErrorCategory() noexcept
    {
        return errorCategory;
    }

    RenderDevice::RenderDevice(const std::function<void(const Event&)>& initCallback):
        graphics::RenderDevice(Driver::direct3D11, initCallback)
    {
        apiVersion = ApiVersion(11, 0);
    }

    RenderDevice::~RenderDevice()
    {
        running = false;
        CommandBuffer commandBuffer;
        commandBuffer.pushCommand(std::make_unique<PresentCommand>());
        submitCommandBuffer(std::move(commandBuffer));

        if (renderThread.isJoinable()) renderThread.join();
    }

    void RenderDevice::init(core::Window* newWindow,
                            const Size2U& newSize,
                            std::uint32_t newSampleCount,
                            bool newSrgb,
                            bool newVerticalSync,
                            bool newDepth,
                            bool newStencil,
                            bool newDebugRenderer)
    {
        graphics::RenderDevice::init(newWindow,
                                     newSize,
                                     newSampleCount,
                                     newSrgb,
                                     newVerticalSync,
                                     newDepth,
                                     newStencil,
                                     newDebugRenderer);

        anisotropicFilteringSupported = true;
        renderTargetsSupported = true;
        clampToBorderSupported = true;
        multisamplingSupported = true;
        uintIndicesSupported = true;

        UINT deviceCreationFlags = 0;

        if (debugRenderer)
            deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;

        ID3D11Device* newDevice;
        ID3D11DeviceContext* newContext;

        D3D_FEATURE_LEVEL featureLevel;
        if (const auto hr = D3D11CreateDevice(nullptr, // adapter
                                              D3D_DRIVER_TYPE_HARDWARE,
                                              nullptr, // software rasterizer (unused)
                                              deviceCreationFlags,
                                              nullptr, // feature levels
                                              0, // no feature levels
                                              D3D11_SDK_VERSION,
                                              &newDevice,
                                              &featureLevel,
                                              &newContext); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to create the Direct3D 11 device");

        device = newDevice;
        context = newContext;

        if (featureLevel >= D3D_FEATURE_LEVEL_10_0)
            npotTexturesSupported = true;


        void* dxgiDevicePtr;
        device->QueryInterface(IID_IDXGIDevice, &dxgiDevicePtr);

        Pointer<IDXGIDevice> dxgiDevice = static_cast<IDXGIDevice*>(dxgiDevicePtr);

        void* newAdapter;
        if (const auto hr = dxgiDevice->GetParent(IID_IDXGIAdapter, &newAdapter); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to get the DXGI adapter");

        adapter = static_cast<IDXGIAdapter*>(newAdapter);

        void* factoryPtr;
        if (const auto hr = adapter->GetParent(IID_IDXGIFactory, &factoryPtr); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to get the DXGI factory");

        Pointer<IDXGIFactory> factory = static_cast<IDXGIFactory*>(factoryPtr);

        DXGI_ADAPTER_DESC adapterDesc;
        if (const auto hr = adapter->GetDesc(&adapterDesc); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to get the DXGI adapter description");
        else
        {
            const int bufferSize = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
            if (bufferSize != 0)
            {
                std::vector<char> buffer(bufferSize);
                if (WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, buffer.data(), bufferSize, nullptr, nullptr) != 0)
                    engine->log(Log::Level::info) << "Using " << buffer.data() << " for rendering";
            }
        }

        auto windowWin = static_cast<core::windows::NativeWindow*>(window->getNativeWindow());

        frameBufferWidth = static_cast<UINT>(newSize.v[0]);
        frameBufferHeight = static_cast<UINT>(newSize.v[1]);

        UINT qualityLevels;
        UINT supportedSampleCount;
        for (supportedSampleCount = sampleCount; supportedSampleCount > 1; --supportedSampleCount)
        {
            if (const auto hr = device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, supportedSampleCount, &qualityLevels); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to check Direct3D 11 multisample quality levels");
            else if (qualityLevels)
                break;
        }

        if (supportedSampleCount != sampleCount)
        {
            sampleCount = supportedSampleCount;
            engine->log(Log::Level::warning) << "Chosen sample count not supported, using: " << sampleCount;
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc;
        swapChainDesc.BufferDesc.Width = frameBufferWidth;
        swapChainDesc.BufferDesc.Height = frameBufferHeight;
        swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
        swapChainDesc.SampleDesc.Count = sampleCount;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 1;
        swapChainDesc.OutputWindow = windowWin->getNativeWindow();
        swapChainDesc.Windowed = (windowWin->isExclusiveFullscreen() && windowWin->isFullscreen()) ? FALSE : TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        swapInterval = verticalSync ? 1 : 0;

        IDXGISwapChain* newSwapCahin;

        if (const auto hr = factory->CreateSwapChain(device.get(), &swapChainDesc, &newSwapCahin); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to create the Direct3D 11 swap chain");

        swapChain = newSwapCahin;

        factory->MakeWindowAssociation(windowWin->getNativeWindow(), DXGI_MWA_NO_ALT_ENTER);

        // Backbuffer
        void* newBackBuffer;
        if (const auto hr = swapChain->GetBuffer(0, IID_ID3D11Texture2D, &newBackBuffer); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to retrieve Direct3D 11 backbuffer");

        backBuffer = static_cast<ID3D11Texture2D*>(newBackBuffer);

        ID3D11RenderTargetView* newRenderTargetView;
        if (const auto hr = device->CreateRenderTargetView(backBuffer.get(), nullptr, &newRenderTargetView); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 render target view");

        renderTargetView = newRenderTargetView;

        // Rasterizer state
        D3D11_RASTERIZER_DESC rasterStateDesc;
        rasterStateDesc.FrontCounterClockwise = FALSE;
        rasterStateDesc.DepthBias = 0;
        rasterStateDesc.DepthBiasClamp = 0;
        rasterStateDesc.SlopeScaledDepthBias = 0;
        rasterStateDesc.DepthClipEnable = TRUE;
        rasterStateDesc.MultisampleEnable = (sampleCount > 1) ? TRUE : FALSE;
        rasterStateDesc.AntialiasedLineEnable = TRUE;

        std::uint32_t rasterStateIndex = 0;

        for (std::uint32_t fillMode = 0; fillMode < 2; ++fillMode)
        {
            for (std::uint32_t scissorEnable = 0; scissorEnable < 2; ++scissorEnable)
            {
                for (std::uint32_t cullMode = 0; cullMode < 3; ++cullMode)
                {
                    rasterStateDesc.FillMode = (fillMode == 0) ? D3D11_FILL_SOLID : D3D11_FILL_WIREFRAME;
                    rasterStateDesc.ScissorEnable = (scissorEnable == 0) ? FALSE : TRUE;
                    switch (cullMode)
                    {
                        case 0: rasterStateDesc.CullMode = D3D11_CULL_NONE; break;
                        case 1: rasterStateDesc.CullMode = D3D11_CULL_FRONT; break;
                        case 2: rasterStateDesc.CullMode = D3D11_CULL_BACK; break;
                    }

                    ID3D11RasterizerState* newRasterizerState;

                    if (const auto hr = device->CreateRasterizerState(&rasterStateDesc, &newRasterizerState); FAILED(hr))
                        throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 rasterizer state");

                    rasterizerStates[rasterStateIndex] = newRasterizerState;

                    ++rasterStateIndex;
                }
            }
        }

        if (depth)
        {
            D3D11_TEXTURE2D_DESC depthStencilDesc;
            depthStencilDesc.Width = frameBufferWidth;
            depthStencilDesc.Height = frameBufferHeight;
            depthStencilDesc.MipLevels = 1;
            depthStencilDesc.ArraySize = 1;
            depthStencilDesc.Format = stencil ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT;
            depthStencilDesc.SampleDesc.Count = sampleCount;
            depthStencilDesc.SampleDesc.Quality = 0;
            depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
            depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            depthStencilDesc.CPUAccessFlags = 0;
            depthStencilDesc.MiscFlags = 0;

            ID3D11Texture2D* newDepthStencilTexture;
            if (const auto hr = device->CreateTexture2D(&depthStencilDesc, nullptr, &newDepthStencilTexture); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 depth stencil texture");

            depthStencilTexture = newDepthStencilTexture;

            ID3D11DepthStencilView* newDepthStencilView;
            if (const auto hr = device->CreateDepthStencilView(depthStencilTexture.get(), nullptr, &newDepthStencilView); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 depth stencil view");

            depthStencilView = newDepthStencilView;
        }

        D3D11_DEPTH_STENCIL_DESC depthStencilStateDesc;
        depthStencilStateDesc.DepthEnable = FALSE;
        depthStencilStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthStencilStateDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depthStencilStateDesc.StencilEnable = FALSE;
        depthStencilStateDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        depthStencilStateDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        depthStencilStateDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depthStencilStateDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilStateDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        depthStencilStateDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilStateDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depthStencilStateDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilStateDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        depthStencilStateDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;

        ID3D11DepthStencilState* newDefaultDepthStencilState;
        if (const auto hr = device->CreateDepthStencilState(&depthStencilStateDesc, &newDefaultDepthStencilState); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 depth stencil state");

        defaultDepthStencilState = newDefaultDepthStencilState;

        running = true;
        renderThread = Thread(&RenderDevice::renderMain, this);
    }

    void RenderDevice::setFullscreen(bool newFullscreen)
    {
        executeOnRenderThread([newFullscreen, this]() {
            if (const auto hr = swapChain->SetFullscreenState(newFullscreen ? TRUE : FALSE, nullptr); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to set fullscreen state");
        });
    }

    void RenderDevice::process()
    {
        graphics::RenderDevice::process();
        executeAll();

        std::vector<float> shaderData;

        std::uint32_t fillModeIndex = 0;
        std::uint32_t scissorEnableIndex = 0;
        std::uint32_t cullModeIndex = 0;
        RenderTarget* currentRenderTarget = nullptr;
        Shader* currentShader = nullptr;

        std::vector<ID3D11ShaderResourceView*> currentResourceViews;
        std::vector<ID3D11SamplerState*> currentSamplerStates;

        CommandBuffer commandBuffer;
        std::unique_ptr<Command> command;

        for (;;)
        {
            std::unique_lock lock(commandQueueMutex);
            while (commandQueue.empty()) commandQueueCondition.wait(lock);
            commandBuffer = std::move(commandQueue.front());
            commandQueue.pop();
            lock.unlock();

            while (!commandBuffer.isEmpty())
            {
                command = commandBuffer.popCommand();

                switch (command->type)
                {
                    case Command::Type::resize:
                    {
                        auto resizeCommand = static_cast<const ResizeCommand*>(command.get());
                        resizeBackBuffer(static_cast<UINT>(resizeCommand->size.v[0]),
                                            static_cast<UINT>(resizeCommand->size.v[1]));
                        break;
                    }

                    case Command::Type::present:
                    {
                        if (currentRenderTarget)
                            currentRenderTarget->resolve();

                        swapChain->Present(swapInterval, 0);
                        break;
                    }

                    case Command::Type::deleteResource:
                    {
                        auto deleteResourceCommand = static_cast<const DeleteResourceCommand*>(command.get());
                        resources[deleteResourceCommand->resource - 1].reset();
                        break;
                    }

                    case Command::Type::initRenderTarget:
                    {
                        auto initRenderTargetCommand = static_cast<const InitRenderTargetCommand*>(command.get());

                        std::set<Texture*> colorTextures;
                        for (const auto colorTextureId : initRenderTargetCommand->colorTextures)
                            colorTextures.insert(getResource<Texture>(colorTextureId));

                        auto renderTarget = std::make_unique<RenderTarget>(*this,
                                                                            colorTextures,
                                                                            getResource<Texture>(initRenderTargetCommand->depthTexture));

                        if (initRenderTargetCommand->renderTarget > resources.size())
                            resources.resize(initRenderTargetCommand->renderTarget);
                        resources[initRenderTargetCommand->renderTarget - 1] = std::move(renderTarget);
                        break;
                    }

                    case Command::Type::setRenderTarget:
                    {
                        auto setRenderTargetCommand = static_cast<const SetRenderTargetCommand*>(command.get());

                        if (currentRenderTarget)
                            currentRenderTarget->resolve();

                        if (setRenderTargetCommand->renderTarget)
                        {
                            currentRenderTarget = getResource<RenderTarget>(setRenderTargetCommand->renderTarget);
                            assert(currentRenderTarget);
                            context->OMSetRenderTargets(static_cast<UINT>(currentRenderTarget->getRenderTargetViews().size()),
                                                        currentRenderTarget->getRenderTargetViews().data(),
                                                        currentRenderTarget->getDepthStencilView());
                        }
                        else
                        {
                            currentRenderTarget = nullptr;
                            ID3D11RenderTargetView* renderTargetViews[] = {renderTargetView.get()};
                            context->OMSetRenderTargets(1, renderTargetViews, depthStencilView.get());
                        }
                        break;
                    }

                    case Command::Type::clearRenderTarget:
                    {
                        auto clearCommand = static_cast<const ClearRenderTargetCommand*>(command.get());

                        FLOAT frameBufferClearColor[4]{clearCommand->clearColor.normR(),
                            clearCommand->clearColor.normG(),
                            clearCommand->clearColor.normB(),
                            clearCommand->clearColor.normA()};

                        if (currentRenderTarget)
                        {
                            if (clearCommand->clearColorBuffer)
                                for (ID3D11RenderTargetView* view : currentRenderTarget->getRenderTargetViews())
                                    context->ClearRenderTargetView(view, frameBufferClearColor);

                            if (clearCommand->clearDepthBuffer || clearCommand->clearStencilBuffer)
                                if (ID3D11DepthStencilView* view = currentRenderTarget->getDepthStencilView())
                                    context->ClearDepthStencilView(view,
                                                                    (clearCommand->clearDepthBuffer ? D3D11_CLEAR_DEPTH : 0) | (clearCommand->clearStencilBuffer ? D3D11_CLEAR_STENCIL : 0),
                                                                    clearCommand->clearDepth,
                                                                    static_cast<UINT8>(clearCommand->clearStencil));
                        }
                        else
                        {
                            if (clearCommand->clearColorBuffer)
                                context->ClearRenderTargetView(renderTargetView.get(), frameBufferClearColor);

                            if (clearCommand->clearDepthBuffer)
                                context->ClearDepthStencilView(depthStencilView.get(),
                                                                (clearCommand->clearDepthBuffer ? D3D11_CLEAR_DEPTH : 0) | (clearCommand->clearStencilBuffer ? D3D11_CLEAR_STENCIL : 0),
                                                                clearCommand->clearDepth,
                                                                static_cast<UINT8>(clearCommand->clearStencil));
                        }

                        break;
                    }

                    case Command::Type::blit:
                    {
                        auto blitCommand = static_cast<const BlitCommand*>(command.get());

                        auto sourceTexture = getResource<Texture>(blitCommand->sourceTexture);
                        auto destinationTexture = getResource<Texture>(blitCommand->destinationTexture);

                        D3D11_BOX box;
                        box.left = blitCommand->sourceX;
                        box.top = blitCommand->sourceY;
                        box.front = 0;
                        box.right = blitCommand->sourceX + blitCommand->sourceWidth;
                        box.bottom = blitCommand->sourceY + blitCommand->sourceHeight;
                        box.back = 0;

                        context->CopySubresourceRegion(destinationTexture->getTexture().get(),
                                                        blitCommand->destinationLevel,
                                                        blitCommand->destinationX,
                                                        blitCommand->destinationY,
                                                        0,
                                                        sourceTexture->getTexture().get(),
                                                        blitCommand->sourceLevel,
                                                        &box);
                        break;
                    }

                    case Command::Type::setScissorTest:
                    {
                        auto setScissorTestCommand = static_cast<const SetScissorTestCommand*>(command.get());

                        if (setScissorTestCommand->enabled)
                        {
                            D3D11_RECT rect;
                            rect.left = static_cast<LONG>(setScissorTestCommand->rectangle.position.v[0]);
                            rect.top = static_cast<LONG>(setScissorTestCommand->rectangle.position.v[1]);
                            rect.right = static_cast<LONG>(setScissorTestCommand->rectangle.position.v[0] + setScissorTestCommand->rectangle.size.v[0]);
                            rect.bottom = static_cast<LONG>(setScissorTestCommand->rectangle.position.v[1] + setScissorTestCommand->rectangle.size.v[1]);
                            context->RSSetScissorRects(1, &rect);
                        }

                        scissorEnableIndex = (setScissorTestCommand->enabled) ? 1 : 0;

                        const std::uint32_t rasterizerStateIndex = fillModeIndex * 6 + scissorEnableIndex * 3 + cullModeIndex;
                        context->RSSetState(rasterizerStates[rasterizerStateIndex].get());

                        break;
                    }

                    case Command::Type::setViewport:
                    {
                        auto setViewportCommand = static_cast<const SetViewportCommand*>(command.get());

                        D3D11_VIEWPORT viewport;
                        viewport.MinDepth = 0.0F;
                        viewport.MaxDepth = 1.0F;
                        viewport.TopLeftX = setViewportCommand->viewport.position.v[0];
                        viewport.TopLeftY = setViewportCommand->viewport.position.v[1];
                        viewport.Width = setViewportCommand->viewport.size.v[0];
                        viewport.Height = setViewportCommand->viewport.size.v[1];
                        context->RSSetViewports(1, &viewport);

                        break;
                    }

                    case Command::Type::initDepthStencilState:
                    {
                        auto initDepthStencilStateCommand = static_cast<const InitDepthStencilStateCommand*>(command.get());
                        auto depthStencilState = std::make_unique<DepthStencilState>(*this,
                                                                                        initDepthStencilStateCommand->depthTest,
                                                                                        initDepthStencilStateCommand->depthWrite,
                                                                                        initDepthStencilStateCommand->compareFunction,
                                                                                        initDepthStencilStateCommand->stencilEnabled,
                                                                                        initDepthStencilStateCommand->stencilReadMask,
                                                                                        initDepthStencilStateCommand->stencilWriteMask,
                                                                                        initDepthStencilStateCommand->frontFaceStencilFailureOperation,
                                                                                        initDepthStencilStateCommand->frontFaceStencilDepthFailureOperation,
                                                                                        initDepthStencilStateCommand->frontFaceStencilPassOperation,
                                                                                        initDepthStencilStateCommand->frontFaceStencilCompareFunction,
                                                                                        initDepthStencilStateCommand->backFaceStencilFailureOperation,
                                                                                        initDepthStencilStateCommand->backFaceStencilDepthFailureOperation,
                                                                                        initDepthStencilStateCommand->backFaceStencilPassOperation,
                                                                                        initDepthStencilStateCommand->backFaceStencilCompareFunction);

                        if (initDepthStencilStateCommand->depthStencilState > resources.size())
                            resources.resize(initDepthStencilStateCommand->depthStencilState);
                        resources[initDepthStencilStateCommand->depthStencilState - 1] = std::move(depthStencilState);
                        break;
                    }

                    case Command::Type::setDepthStencilState:
                    {
                        auto setDepthStencilStateCommand = static_cast<const SetDepthStencilStateCommand*>(command.get());

                        if (setDepthStencilStateCommand->depthStencilState)
                        {
                            auto depthStencilState = getResource<DepthStencilState>(setDepthStencilStateCommand->depthStencilState);
                            context->OMSetDepthStencilState(depthStencilState->getDepthStencilState().get(),
                                                            setDepthStencilStateCommand->stencilReferenceValue);
                        }
                        else
                            context->OMSetDepthStencilState(defaultDepthStencilState.get(),
                                                            setDepthStencilStateCommand->stencilReferenceValue);

                        break;
                    }

                    case Command::Type::setPipelineState:
                    {
                        auto setPipelineStateCommand = static_cast<const SetPipelineStateCommand*>(command.get());

                        auto blendState = getResource<BlendState>(setPipelineStateCommand->blendState);
                        auto shader = getResource<Shader>(setPipelineStateCommand->shader);
                        currentShader = shader;

                        if (blendState)
                            context->OMSetBlendState(blendState->getBlendState().get(), nullptr, 0xFFFFFFFF);
                        else
                            context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

                        if (shader)
                        {
                            assert(shader->getFragmentShader());
                            assert(shader->getVertexShader());
                            assert(shader->getInputLayout());

                            context->PSSetShader(shader->getFragmentShader().get(), nullptr, 0);
                            context->VSSetShader(shader->getVertexShader().get(), nullptr, 0);
                            context->IASetInputLayout(shader->getInputLayout().get());
                        }
                        else
                        {
                            context->PSSetShader(nullptr, nullptr, 0);
                            context->VSSetShader(nullptr, nullptr, 0);
                            context->IASetInputLayout(nullptr);
                        }

                        switch (setPipelineStateCommand->cullMode)
                        {
                            case CullMode::none: cullModeIndex = 0; break;
                            case CullMode::front: cullModeIndex = 1; break;
                            case CullMode::back: cullModeIndex = 2; break;
                            default: throw std::runtime_error("Invalid cull mode");
                        }

                        switch (setPipelineStateCommand->fillMode)
                        {
                            case FillMode::solid: fillModeIndex = 0; break;
                            case FillMode::wireframe: fillModeIndex = 1; break;
                            default: throw std::runtime_error("Invalid fill mode");
                        }

                        const std::uint32_t rasterizerStateIndex = fillModeIndex * 6 + scissorEnableIndex * 3 + cullModeIndex;
                        context->RSSetState(rasterizerStates[rasterizerStateIndex].get());
                        break;
                    }

                    case Command::Type::draw:
                    {
                        auto drawCommand = static_cast<const DrawCommand*>(command.get());

                        // draw mesh buffer
                        auto indexBuffer = getResource<Buffer>(drawCommand->indexBuffer);
                        auto vertexBuffer = getResource<Buffer>(drawCommand->vertexBuffer);

                        assert(indexBuffer);
                        assert(indexBuffer->getBuffer());
                        assert(vertexBuffer);
                        assert(vertexBuffer->getBuffer());

                        ID3D11Buffer* buffers[] = {vertexBuffer->getBuffer().get()};
                        UINT strides[] = {sizeof(Vertex)};
                        UINT offsets[] = {0};
                        context->IASetVertexBuffers(0, 1, buffers, strides, offsets);
                        context->IASetIndexBuffer(indexBuffer->getBuffer().get(),
                                                    getIndexFormat(drawCommand->indexSize), 0);
                        context->IASetPrimitiveTopology(getPrimitiveTopology(drawCommand->drawMode));

                        assert(drawCommand->indexCount);
                        assert(indexBuffer->getSize());
                        assert(vertexBuffer->getSize());

                        context->DrawIndexed(drawCommand->indexCount, drawCommand->startIndex, 0);

                        break;
                    }

                    case Command::Type::pushDebugMarker:
                    {
                        // D3D11 does not support debug markers
                        break;
                    }

                    case Command::Type::popDebugMarker:
                    {
                        // D3D11 does not support debug markers
                        break;
                    }

                    case Command::Type::initBlendState:
                    {
                        auto initBlendStateCommand = static_cast<const InitBlendStateCommand*>(command.get());

                        auto blendState = std::make_unique<BlendState>(*this,
                                                                        initBlendStateCommand->enableBlending,
                                                                        initBlendStateCommand->colorBlendSource,
                                                                        initBlendStateCommand->colorBlendDest,
                                                                        initBlendStateCommand->colorOperation,
                                                                        initBlendStateCommand->alphaBlendSource,
                                                                        initBlendStateCommand->alphaBlendDest,
                                                                        initBlendStateCommand->alphaOperation,
                                                                        initBlendStateCommand->colorMask);

                        if (initBlendStateCommand->blendState > resources.size())
                            resources.resize(initBlendStateCommand->blendState);
                        resources[initBlendStateCommand->blendState - 1] = std::move(blendState);
                        break;
                    }

                    case Command::Type::initBuffer:
                    {
                        auto initBufferCommand = static_cast<const InitBufferCommand*>(command.get());

                        auto buffer = std::make_unique<Buffer>(*this,
                                                                initBufferCommand->bufferType,
                                                                initBufferCommand->flags,
                                                                initBufferCommand->data,
                                                                initBufferCommand->size);

                        if (initBufferCommand->buffer > resources.size())
                            resources.resize(initBufferCommand->buffer);
                        resources[initBufferCommand->buffer - 1] = std::move(buffer);
                        break;
                    }

                    case Command::Type::setBufferData:
                    {
                        auto setBufferDataCommand = static_cast<const SetBufferDataCommand*>(command.get());

                        auto buffer = getResource<Buffer>(setBufferDataCommand->buffer);
                        buffer->setData(setBufferDataCommand->data);
                        break;
                    }

                    case Command::Type::initShader:
                    {
                        auto initShaderCommand = static_cast<const InitShaderCommand*>(command.get());

                        auto shader = std::make_unique<Shader>(*this,
                                                                initShaderCommand->fragmentShader,
                                                                initShaderCommand->vertexShader,
                                                                initShaderCommand->vertexAttributes,
                                                                initShaderCommand->fragmentShaderConstantInfo,
                                                                initShaderCommand->vertexShaderConstantInfo,
                                                                initShaderCommand->fragmentShaderFunction,
                                                                initShaderCommand->vertexShaderFunction);

                        if (initShaderCommand->shader > resources.size())
                            resources.resize(initShaderCommand->shader);
                        resources[initShaderCommand->shader - 1] = std::move(shader);
                        break;
                    }

                    case Command::Type::setShaderConstants:
                    {
                        auto setShaderConstantsCommand = static_cast<const SetShaderConstantsCommand*>(command.get());

                        if (!currentShader)
                            throw std::runtime_error("No shader set");

                        // pixel shader constants
                        const std::vector<Shader::Location>& fragmentShaderConstantLocations = currentShader->getFragmentShaderConstantLocations();

                        if (setShaderConstantsCommand->fragmentShaderConstants.size() > fragmentShaderConstantLocations.size())
                            throw std::runtime_error("Invalid pixel shader constant size");

                        shaderData.clear();

                        for (std::size_t i = 0; i < setShaderConstantsCommand->fragmentShaderConstants.size(); ++i)
                        {
                            const Shader::Location& fragmentShaderConstantLocation = fragmentShaderConstantLocations[i];
                            const std::vector<float>& fragmentShaderConstant = setShaderConstantsCommand->fragmentShaderConstants[i];

                            if (sizeof(float) * fragmentShaderConstant.size() != fragmentShaderConstantLocation.size)
                                throw std::runtime_error("Invalid pixel shader constant size");

                            shaderData.insert(shaderData.end(), fragmentShaderConstant.begin(), fragmentShaderConstant.end());
                        }

                        uploadBuffer(currentShader->getFragmentShaderConstantBuffer().get(),
                                        shaderData.data(),
                                        static_cast<std::uint32_t>(sizeof(float) * shaderData.size()));

                        ID3D11Buffer* fragmentShaderConstantBuffers[1] = {currentShader->getFragmentShaderConstantBuffer().get()};
                        context->PSSetConstantBuffers(0, 1, fragmentShaderConstantBuffers);

                        // vertex shader constants
                        const std::vector<Shader::Location>& vertexShaderConstantLocations = currentShader->getVertexShaderConstantLocations();

                        if (setShaderConstantsCommand->vertexShaderConstants.size() > vertexShaderConstantLocations.size())
                            throw std::runtime_error("Invalid vertex shader constant size");

                        shaderData.clear();

                        for (std::size_t i = 0; i < setShaderConstantsCommand->vertexShaderConstants.size(); ++i)
                        {
                            const Shader::Location& vertexShaderConstantLocation = vertexShaderConstantLocations[i];
                            const std::vector<float>& vertexShaderConstant = setShaderConstantsCommand->vertexShaderConstants[i];

                            if (sizeof(float) * vertexShaderConstant.size() != vertexShaderConstantLocation.size)
                                throw std::runtime_error("Invalid vertex shader constant size");

                            shaderData.insert(shaderData.end(), vertexShaderConstant.begin(), vertexShaderConstant.end());
                        }

                        uploadBuffer(currentShader->getVertexShaderConstantBuffer().get(),
                                        shaderData.data(),
                                        static_cast<std::uint32_t>(sizeof(float) * shaderData.size()));

                        ID3D11Buffer* vertexShaderConstantBuffers[1] = {currentShader->getVertexShaderConstantBuffer().get()};
                        context->VSSetConstantBuffers(0, 1, vertexShaderConstantBuffers);

                        break;
                    }

                    case Command::Type::initTexture:
                    {
                        auto initTextureCommand = static_cast<const InitTextureCommand*>(command.get());

                        auto texture = std::make_unique<Texture>(*this,
                                                                    initTextureCommand->levels,
                                                                    initTextureCommand->textureType,
                                                                    initTextureCommand->flags,
                                                                    initTextureCommand->sampleCount,
                                                                    initTextureCommand->pixelFormat,
                                                                    initTextureCommand->filter,
                                                                    initTextureCommand->maxAnisotropy);

                        if (initTextureCommand->texture > resources.size())
                            resources.resize(initTextureCommand->texture);
                        resources[initTextureCommand->texture - 1] = std::move(texture);
                        break;
                    }

                    case Command::Type::setTextureData:
                    {
                        auto setTextureDataCommand = static_cast<const SetTextureDataCommand*>(command.get());

                        auto texture = getResource<Texture>(setTextureDataCommand->texture);
                        texture->setData(setTextureDataCommand->levels);

                        break;
                    }

                    case Command::Type::setTextureParameters:
                    {
                        auto setTextureParametersCommand = static_cast<const SetTextureParametersCommand*>(command.get());

                        auto texture = getResource<Texture>(setTextureParametersCommand->texture);
                        texture->setFilter(setTextureParametersCommand->filter);
                        texture->setAddressX(setTextureParametersCommand->addressX);
                        texture->setAddressY(setTextureParametersCommand->addressY);
                        texture->setAddressZ(setTextureParametersCommand->addressZ);
                        texture->setMaxAnisotropy(setTextureParametersCommand->maxAnisotropy);

                        break;
                    }

                    case Command::Type::setTextures:
                    {
                        auto setTexturesCommand = static_cast<const SetTexturesCommand*>(command.get());

                        currentResourceViews.clear();
                        currentSamplerStates.clear();

                        for (const auto textureId : setTexturesCommand->textures)
                            if (auto texture = getResource<Texture>(textureId))
                            {
                                currentResourceViews.push_back(texture->getResourceView().get());
                                currentSamplerStates.push_back(texture->getSamplerState());
                            }
                            else
                            {
                                currentResourceViews.push_back(nullptr);
                                currentSamplerStates.push_back(nullptr);
                            }

                        context->PSSetShaderResources(0, static_cast<UINT>(currentResourceViews.size()), currentResourceViews.data());
                        context->PSSetSamplers(0, static_cast<UINT>(currentSamplerStates.size()), currentSamplerStates.data());

                        break;
                    }

                    default:
                        throw std::runtime_error("Invalid command");
                }

                if (command->type == Command::Type::present) return;
            }
        }
    }

    IDXGIOutput* RenderDevice::getOutput() const
    {
        auto windowWin = static_cast<core::windows::NativeWindow*>(window->getNativeWindow());
        auto monitor = windowWin->getMonitor();

        if (!monitor)
            throw std::runtime_error("Window is not on any monitor");

        HRESULT hr;
        IDXGIOutput* output;
        for (UINT i = 0; (hr = adapter->EnumOutputs(i, &output)) != DXGI_ERROR_NOT_FOUND; ++i)
            if (SUCCEEDED(hr))
            {
                DXGI_OUTPUT_DESC outputDesc;
                hr = output->GetDesc(&outputDesc);

                if (SUCCEEDED(hr) && outputDesc.Monitor == monitor)
                    return output;

                output->Release();
            }

        return nullptr;
    }

    std::vector<Size2U> RenderDevice::getSupportedResolutions() const
    {
        std::vector<Size2U> result;

        IDXGIOutput* output = getOutput();

        if (!output) return result;

        UINT numModes = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (const auto hr = output->GetDisplayModeList(format, 0, &numModes, nullptr); FAILED(hr))
        {
            output->Release();
            throw std::system_error(hr, errorCategory, "Failed to get display mode list");
        }

        if (numModes > 0)
        {
            std::vector<DXGI_MODE_DESC> displayModes(numModes);
            output->GetDisplayModeList(format, 0, &numModes, displayModes.data());

            for (const auto& displayMode : displayModes)
                result.emplace_back(static_cast<std::uint32_t>(displayMode.Width),
                                    static_cast<std::uint32_t>(displayMode.Height));
        }

        output->Release();

        return result;
    }

    void RenderDevice::generateScreenshot(const std::string& filename)
    {
        void* backBufferTexturePtr;
        if (const auto hr = backBuffer->QueryInterface(IID_ID3D11Texture2D, &backBufferTexturePtr); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to get Direct3D 11 back buffer texture");

        Pointer<ID3D11Texture2D> backBufferTexture = static_cast<ID3D11Texture2D*>(backBufferTexturePtr);

        D3D11_TEXTURE2D_DESC backBufferDesc;
        backBufferTexture->GetDesc(&backBufferDesc);

        D3D11_TEXTURE2D_DESC textureDesc;
        textureDesc.Width = backBufferDesc.Width;
        textureDesc.Height = backBufferDesc.Height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.BindFlags = 0;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        textureDesc.MiscFlags = 0;

        ID3D11Texture2D* texturePtr;
        if (const auto hr = device->CreateTexture2D(&textureDesc, nullptr, &texturePtr); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 texture");

        Pointer<ID3D11Texture2D> texture = texturePtr;

        if (backBufferDesc.SampleDesc.Count > 1)
        {
            D3D11_TEXTURE2D_DESC resolveTextureDesc;
            resolveTextureDesc.Width = backBufferDesc.Width;
            resolveTextureDesc.Height = backBufferDesc.Height;
            resolveTextureDesc.MipLevels = 1;
            resolveTextureDesc.ArraySize = 1;
            resolveTextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            resolveTextureDesc.SampleDesc.Count = 1;
            resolveTextureDesc.SampleDesc.Quality = 0;
            resolveTextureDesc.Usage = D3D11_USAGE_DEFAULT;
            resolveTextureDesc.BindFlags = 0;
            resolveTextureDesc.CPUAccessFlags = 0;
            resolveTextureDesc.MiscFlags = 0;

            ID3D11Texture2D* resolveTexturePtr;
            if (const auto hr = device->CreateTexture2D(&resolveTextureDesc, nullptr, &resolveTexturePtr); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 texture");

            Pointer<ID3D11Texture2D> resolveTexture = resolveTexturePtr;

            context->ResolveSubresource(resolveTexture.get(), 0, backBuffer.get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            context->CopyResource(texture.get(), resolveTexture.get());
        }
        else
            context->CopyResource(texture.get(), backBuffer.get());

        D3D11_MAPPED_SUBRESOURCE mappedSubresource;
        if (const auto hr = context->Map(texture.get(), 0, D3D11_MAP_READ, 0, &mappedSubresource); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to map Direct3D 11 resource");

        if (!stbi_write_png(filename.c_str(), textureDesc.Width, textureDesc.Height, 4, mappedSubresource.pData, static_cast<int>(mappedSubresource.RowPitch)))
        {
            context->Unmap(texture.get(), 0);
            throw std::runtime_error("Failed to save screenshot to file");
        }

        context->Unmap(texture.get(), 0);
    }

    void RenderDevice::resizeBackBuffer(UINT newWidth, UINT newHeight)
    {
        if (frameBufferWidth != newWidth || frameBufferHeight != newHeight)
        {
            if (const auto hr = swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to resize Direct3D 11 backbuffer");

            void* newBackBuffer;
            if (const auto hr = swapChain->GetBuffer(0, IID_ID3D11Texture2D, &newBackBuffer); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to retrieve Direct3D 11 backbuffer");

            backBuffer = static_cast<ID3D11Texture2D*>(newBackBuffer);

            ID3D11RenderTargetView* newRenderTargetView;
            if (const auto hr = device->CreateRenderTargetView(backBuffer.get(), nullptr, &newRenderTargetView); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 render target view");

            renderTargetView = newRenderTargetView;

            D3D11_TEXTURE2D_DESC desc;
            backBuffer->GetDesc(&desc);

            if (depth)
            {
                D3D11_TEXTURE2D_DESC depthStencilDesc;
                depthStencilDesc.Width = desc.Width;
                depthStencilDesc.Height = desc.Height;
                depthStencilDesc.MipLevels = 1;
                depthStencilDesc.ArraySize = 1;
                depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
                depthStencilDesc.SampleDesc.Count = sampleCount;
                depthStencilDesc.SampleDesc.Quality = 0;
                depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
                depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                depthStencilDesc.CPUAccessFlags = 0;
                depthStencilDesc.MiscFlags = 0;

                ID3D11Texture2D* newDepthStencilTexture;
                if (const auto hr = device->CreateTexture2D(&depthStencilDesc, nullptr, &newDepthStencilTexture); FAILED(hr))
                    throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 depth stencil texture");

                depthStencilTexture = newDepthStencilTexture;

                ID3D11DepthStencilView* newDepthStencilView;
                if (const auto hr = device->CreateDepthStencilView(depthStencilTexture.get(), nullptr, &newDepthStencilView); FAILED(hr))
                    throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 depth stencil view");

                depthStencilView = newDepthStencilView;
            }

            frameBufferWidth = desc.Width;
            frameBufferHeight = desc.Height;
        }
    }

    void RenderDevice::uploadBuffer(ID3D11Buffer* buffer, const void* data, std::uint32_t dataSize)
    {
        D3D11_MAPPED_SUBRESOURCE mappedSubresource;
        if (const auto hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource); FAILED(hr))
            throw std::system_error(hr, errorCategory, "Failed to lock Direct3D 11 buffer");

        std::copy(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + dataSize, static_cast<std::uint8_t*>(mappedSubresource.pData));

        context->Unmap(buffer, 0);
    }

    ID3D11SamplerState* RenderDevice::getSamplerState(const SamplerStateDesc& desc)
    {
        auto samplerStatesIterator = samplerStates.find(desc);

        if (samplerStatesIterator != samplerStates.end())
            return samplerStatesIterator->second.get();
        else
        {
            D3D11_SAMPLER_DESC samplerStateDesc;

            if (desc.maxAnisotropy > 1)
                samplerStateDesc.Filter = D3D11_FILTER_ANISOTROPIC;
            else
            {
                switch (desc.filter)
                {
                    case SamplerFilter::point:
                        samplerStateDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                        break;
                    case SamplerFilter::linear:
                        samplerStateDesc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
                        break;
                    case SamplerFilter::bilinear:
                        samplerStateDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
                        break;
                    case SamplerFilter::trilinear:
                        samplerStateDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                        break;
                    default:
                        throw std::runtime_error("Invalid texture filter");
                }
            }

            samplerStateDesc.AddressU = getTextureAddressMode(desc.addressX);
            samplerStateDesc.AddressV = getTextureAddressMode(desc.addressY);
            samplerStateDesc.AddressW = getTextureAddressMode(desc.addressZ);
            samplerStateDesc.MipLODBias = 0.0F;
            samplerStateDesc.MaxAnisotropy = desc.maxAnisotropy;
            samplerStateDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

            samplerStateDesc.BorderColor[0] = 0.0F;
            samplerStateDesc.BorderColor[1] = 0.0F;
            samplerStateDesc.BorderColor[2] = 0.0F;
            samplerStateDesc.BorderColor[3] = 0.0F;

            samplerStateDesc.MinLOD = 0.0F;
            samplerStateDesc.MaxLOD = D3D11_FLOAT32_MAX;

            ID3D11SamplerState* samplerState;
            if (const auto hr = device->CreateSamplerState(&samplerStateDesc, &samplerState); FAILED(hr))
                throw std::system_error(hr, errorCategory, "Failed to create Direct3D 11 sampler state");

            samplerStates[desc] = samplerState;

            return samplerState;
        }
    }

    void RenderDevice::renderMain()
    {
        Thread::setCurrentThreadName("Render");

        while (running)
        {
            try
            {
                process();
            }
            catch (const std::exception& e)
            {
                engine->log(Log::Level::error) << e.what();
            }
        }
    }
}

#endif
