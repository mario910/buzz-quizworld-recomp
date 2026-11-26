#import <AppKit/AppKit.h>
#include <SDL.h>

#include "buzzer_input.h"
#include "buzzer_sdl.h"

namespace {
int sdl_key(unichar key) {
    if (key >= NSF1FunctionKey && key <= NSF12FunctionKey)
        return SDLK_F1 + key - NSF1FunctionKey;
    if (key >= NSF13FunctionKey && key <= NSF24FunctionKey)
        return SDLK_F13 + key - NSF13FunctionKey;
    switch (key) {
    case NSLeftArrowFunctionKey: return SDLK_LEFT;
    case NSUpArrowFunctionKey: return SDLK_UP;
    case NSRightArrowFunctionKey: return SDLK_RIGHT;
    case NSDownArrowFunctionKey: return SDLK_DOWN;
    case NSHomeFunctionKey: return SDLK_HOME;
    case NSEndFunctionKey: return SDLK_END;
    case NSPageUpFunctionKey: return SDLK_PAGEUP;
    case NSPageDownFunctionKey: return SDLK_PAGEDOWN;
    case NSInsertFunctionKey: return SDLK_INSERT;
    case NSDeleteFunctionKey: return SDLK_DELETE;
    case NSDeleteCharacter: return SDLK_BACKSPACE;
    case NSEnterCharacter: return SDLK_RETURN;
    default: return key;
    }
}
}

void buzz_macos_input_start() {
    if (!NSApp) return;
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp
                                         handler:^NSEvent*(NSEvent* event) {
        if (!event.window.isKeyWindow || (event.modifierFlags & NSEventModifierFlagCommand))
            return event;
        NSString* key = event.charactersIgnoringModifiers.lowercaseString;
        if (key.length != 1) return event;
        const auto code = buzz_sdl_key_code(sdl_key([key characterAtIndex:0]));
        if (code) buzz_input_key(code, event.type == NSEventTypeKeyDown);
        return event;
    }];
}

void buzz_macos_input_frame() {
    @autoreleasepool {
        SDL_GameControllerUpdate();
        buzz_input_sdl_frame(NSApp.isActive && NSApp.keyWindow != nil);
    }
}
