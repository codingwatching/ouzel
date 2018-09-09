// Copyright 2015-2018 Elviss Strazdins. All rights reserved.

#include "MeshRenderer.hpp"
#include "core/Engine.hpp"

namespace ouzel
{
    namespace scene
    {
        MeshRenderer::MeshRenderer():
            Component(CLASS)
        {
            whitePixelTexture = engine->getCache()->getTexture(TEXTURE_WHITE_PIXEL);
        }

        MeshRenderer::MeshRenderer(const MeshData& meshData):
            Component(CLASS)
        {
            init(meshData);
        }

        MeshRenderer::MeshRenderer(const std::string& filename):
            Component(CLASS)
        {
            init(filename);
        }

        void MeshRenderer::init(const MeshData& meshData)
        {
            boundingBox = meshData.boundingBox;
            material = meshData.material;
            indexCount = meshData.indexCount;
            indexSize = meshData.indexSize;
            indexBuffer = meshData.indexBuffer;
            vertexBuffer = meshData.vertexBuffer;
        }

        void MeshRenderer::init(const std::string& filename)
        {
            init(*engine->getCache()->getMeshData(filename));
        }

        void MeshRenderer::draw(const Matrix4& transformMatrix,
                                 float opacity,
                                 const Matrix4& renderViewProjection,
                                 bool wireframe)
        {
            Component::draw(transformMatrix,
                            opacity,
                            renderViewProjection,
                            wireframe);

            material->cullMode = graphics::Renderer::CullMode::NONE;

            Matrix4 modelViewProj = renderViewProjection * transformMatrix;
            float colorVector[] = {material->diffuseColor.normR(), material->diffuseColor.normG(), material->diffuseColor.normB(), material->diffuseColor.normA() * opacity * material->opacity};

            std::vector<std::vector<float>> fragmentShaderConstants(1);
            fragmentShaderConstants[0] = {std::begin(colorVector), std::end(colorVector)};

            std::vector<std::vector<float>> vertexShaderConstants(1);
            vertexShaderConstants[0] = {std::begin(modelViewProj.m), std::end(modelViewProj.m)};

            std::vector<graphics::RenderResource*> textures;
            if (wireframe) textures.push_back(whitePixelTexture->getResource());
            else
            {
                for (const std::shared_ptr<graphics::Texture>& texture : material->textures)
                    textures.push_back(texture ? texture->getResource() : nullptr);
            }

            engine->getRenderer()->setCullMode(material->cullMode);
            engine->getRenderer()->setPipelineState(material->blendState->getResource(),
                                                    material->shader->getResource());
            engine->getRenderer()->setShaderConstants(fragmentShaderConstants,
                                                      vertexShaderConstants);
            engine->getRenderer()->setTextures(textures);
            engine->getRenderer()->draw(indexBuffer->getResource(),
                                        indexCount,
                                        indexSize,
                                        vertexBuffer->getResource(),
                                        graphics::Renderer::DrawMode::TRIANGLE_LIST,
                                        0);
        }
    } // namespace scene
} // namespace ouzel
