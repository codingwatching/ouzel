// Copyright 2015-2020 Elviss Strazdins. All rights reserved.

#import <UIKit/UIKit.h>
#include "EngineTVOS.hpp"

@interface AppDelegate: UIResponder<UIApplicationDelegate>
@end

@implementation AppDelegate

- (BOOL)application:(__unused UIApplication*)application willFinishLaunchingWithOptions:(__unused NSDictionary*)launchOptions
{
    ouzel::engine->init();

    return YES;
}

- (BOOL)application:(__unused UIApplication*)application didFinishLaunchingWithOptions:(__unused NSDictionary*)launchOptions
{
    if (ouzel::engine)
        ouzel::engine->start();

    return YES;
}

- (void)applicationDidBecomeActive:(__unused UIApplication*)application
{
    ouzel::engine->resume();
}

- (void)applicationWillResignActive:(__unused UIApplication*)application
{
    ouzel::engine->pause();
}

- (void)applicationDidEnterBackground:(__unused UIApplication*)application
{
}

- (void)applicationWillEnterForeground:(__unused UIApplication*)application
{
}

- (void)applicationWillTerminate:(__unused UIApplication*)application
{
    ouzel::engine->exit();
}

- (void)applicationDidReceiveMemoryWarning:(__unused UIApplication*)application
{
    if (ouzel::engine)
    {
        auto event = std::make_unique<ouzel::SystemEvent>();
        event->type = ouzel::Event::Type::lowMemory;

        ouzel::engine->getEventDispatcher().postEvent(std::move(event));
    }
}
@end

@interface ExecuteHandler: NSObject
@end

@implementation ExecuteHandler
{
    ouzel::EngineTVOS* engine;
}

- (id)initWithEngine:(ouzel::EngineTVOS*)initEngine
{
    if (self = [super init])
        engine = initEngine;

    return self;
}

- (void)executeAll
{
    engine->executeAll();
}
@end

namespace ouzel
{
    EngineTVOS::EngineTVOS(int initArgc, char* initArgv[]):
        argc(initArgc), argv(initArgv)
    {
        for (int i = 0; i < initArgc; ++i)
            args.push_back(initArgv[i]);

        pool = [[NSAutoreleasePool alloc] init];
        executeHanlder = [[ExecuteHandler alloc] initWithEngine:this];
    }

    EngineTVOS::~EngineTVOS()
    {
        if (executeHanlder) [executeHanlder release];
        if (pool) [pool release];
    }

    void EngineTVOS::run()
    {
        UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }

    void EngineTVOS::runOnMainThread(const std::function<void()>& func)
    {
        std::unique_lock<std::mutex> lock(executeMutex);
        executeQueue.push(func);
        lock.unlock();

        [executeHanlder performSelectorOnMainThread:@selector(executeAll) withObject:nil waitUntilDone:NO];
    }

    void EngineTVOS::openUrl(const std::string& url)
    {
        executeOnMainThread([url](){
            NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
            NSURL* nsUrl = [NSURL URLWithString:urlString];

            [[UIApplication sharedApplication] openURL:nsUrl];
        });
    }

    void EngineTVOS::setScreenSaverEnabled(bool newScreenSaverEnabled)
    {
        Engine::setScreenSaverEnabled(newScreenSaverEnabled);

        executeOnMainThread([newScreenSaverEnabled]() {
            [UIApplication sharedApplication].idleTimerDisabled = newScreenSaverEnabled ? YES : NO;
        });
    }

    void EngineTVOS::executeAll()
    {
        std::function<void()> func;

        for (;;)
        {
            std::unique_lock<std::mutex> lock(executeMutex);

            if (executeQueue.empty()) break;

            func = std::move(executeQueue.front());
            executeQueue.pop();
            lock.unlock();

            if (func) func();
        }
    }
}
