// Copyright (C) 2017 Elviss Strazdins
// This file is part of the Ouzel engine.

#include <cstdlib>
#include "ApplicationAndroid.h"
#include "core/Engine.h"

namespace ouzel
{
    ApplicationAndroid::ApplicationAndroid(JNIEnv* aJNIEnv, jobject aMainActivity, jobject aAssetManager):
        jniEnv(aJNIEnv), mainActivity(aMainActivity)
    {
        assetManager = AAssetManager_fromJava(jniEnv, aAssetManager);
    }

    int ApplicationAndroid::run()
    {
        ouzelMain(ouzel::sharedApplication->getArgs());

        if (!sharedEngine)
        {
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    bool ApplicationAndroid::step()
    {
        executeAll();

        if (!sharedEngine->draw())
        {
            return false;
        }

        return active;
    }
}
