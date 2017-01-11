// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#include "Rotate.h"
#include "scene/Node.h"

namespace ouzel
{
    namespace scene
    {
        Rotate::Rotate(float aLength, const Vector3& aRotation, bool aRelative):
            Animator(aLength), rotation(aRotation), relative(aRelative)
        {
        }

        void Rotate::start(Node* newTargetNode)
        {
            Animator::start(newTargetNode);

            if (targetNode)
            {
                startRotation = targetNode->getRotation().getEulerAngles();

                targetRotation = relative ? startRotation + rotation : rotation;

                diff = targetRotation - startRotation;
            }
        }

        void Rotate::updateProgress()
        {
            Animator::updateProgress();

            if (targetNode)
            {
                targetNode->setRotation(startRotation + diff * progress);
            }
        }
    } // namespace scene
} // namespace ouzel
