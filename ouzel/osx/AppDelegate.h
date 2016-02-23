// Copyright (C) 2016 Elviss Strazdins
// This file is part of the Ouzel engine.

#import <Cocoa/Cocoa.h>

@class OpenGLView;

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
{
    OpenGLView* _openGLView;
    BOOL _fullscreen;
}

@property (strong, nonatomic) NSWindow* window;

@end
