// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#include "Fade.hpp"
#include "scene/Node.hpp"

namespace ouzel
{
    namespace scene
    {
        Fade::Fade(float aLength, float aOpacity, bool aRelative):
            Animator(aLength), opacity(aOpacity), relative(aRelative)
        {
        }

        void Fade::play()
        {
            Animator::play();

            if (targetNode)
            {
                startOpacity = targetNode->getOpacity();
                targetOpacity = relative ? startOpacity + opacity : opacity;

                diff = targetOpacity - startOpacity;
            }
        }

        void Fade::updateProgress()
        {
            Animator::updateProgress();

            if (targetNode)
            {
                targetNode->setOpacity(startOpacity + (diff * progress));
            }
        }
    } // namespace scene
} // namespace ouzel
