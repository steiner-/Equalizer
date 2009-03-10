
/* Copyright (c) 2007-2009, Stefan Eilemann <eile@equalizergraphics.com> 
   All rights reserved. */

#include "aglEventHandler.h"

#include "global.h"
#include "log.h"
#include "pipe.h"
#include "window.h"
#include "aglWindow.h"
#include "aglWindowEvent.h"

using namespace eq::base;
using namespace std;

namespace eq
{
AGLEventHandler::AGLEventHandler( AGLWindowIF* window )
        : _window( window )
        , _eventHandler( 0 )
        , _eventDispatcher( 0 )
        , _lastDX( 0 )
        , _lastDY( 0 )
{
    const WindowRef carbonWindow = window->getCarbonWindow();
    if( !carbonWindow )
    {
        EQWARN << "Can't add window without native Carbon window to AGL event "
               << "handler" << endl;
        return;
    }
    
    Global::enterCarbon();
    EventHandlerUPP eventHandler = NewEventHandlerUPP( 
        eq::AGLEventHandler::_handleEventUPP );
    EventTypeSpec   eventType[]    = {
        { kEventClassWindow,   kEventWindowBoundsChanged },
        { kEventClassWindow,   kEventWindowZoomed },
        { kEventClassWindow,   kEventWindowUpdate },
        { kEventClassWindow,   kEventWindowDrawContent },
        { kEventClassWindow,   kEventWindowClosed },
        { kEventClassWindow,   kEventWindowHidden },
        { kEventClassWindow,   kEventWindowCollapsed },
        { kEventClassWindow,   kEventWindowShown },
        { kEventClassWindow,   kEventWindowExpanded },
        { kEventClassMouse,    kEventMouseMoved },
        { kEventClassMouse,    kEventMouseDragged },
        { kEventClassMouse,    kEventMouseDown },
        { kEventClassMouse,    kEventMouseUp },
        { kEventClassKeyboard, kEventRawKeyDown },
        { kEventClassKeyboard, kEventRawKeyUp },
        { kEventClassKeyboard, kEventRawKeyRepeat }
    };

    InstallWindowEventHandler( carbonWindow, eventHandler, 
                               sizeof( eventType ) / sizeof( EventTypeSpec ),
                               eventType, this, &_eventHandler );

    const Pipe* pipe = window->getPipe();
    if( pipe->isThreaded( ))
    {
        EQASSERT( GetCurrentEventQueue() != GetMainEventQueue( ));

        // dispatches to pipe thread queue
        EventHandlerUPP eventDispatcher = NewEventHandlerUPP( 
            eq::AGLEventHandler::_dispatchEventUPP );
        EventQueueRef target = GetCurrentEventQueue();

        InstallWindowEventHandler( carbonWindow, eventDispatcher, 
                                   sizeof( eventType ) / sizeof( EventTypeSpec),
                                   eventType, target, &_eventDispatcher );
    }
    else
        _eventDispatcher = 0;

    Global::leaveCarbon();

    EQINFO << "Installed event handlers for carbon window " << carbonWindow
           << endl;
}

AGLEventHandler::~AGLEventHandler()
{
    Global::enterCarbon();
    if( _eventDispatcher )
    {
        RemoveEventHandler( _eventDispatcher );
        _eventDispatcher = 0;
    }
    if( _eventHandler )
    {
        RemoveEventHandler( _eventHandler );
        _eventHandler = 0;
    }
    Global::leaveCarbon();
}

pascal OSStatus AGLEventHandler::_dispatchEventUPP( 
    EventHandlerCallRef nextHandler, EventRef event, void* userData )
{
    EventQueueRef target = static_cast< EventQueueRef >( userData );
    
    if( GetCurrentEventQueue() == target )
        return CallNextEventHandler( nextHandler, event );

    EQASSERT( GetCurrentEventQueue() == GetMainEventQueue( ));
    PostEventToQueue( target, event, kEventPriorityStandard );
    return noErr;
}

pascal OSStatus AGLEventHandler::_handleEventUPP( 
    EventHandlerCallRef nextHandler, EventRef event, void* userData )
{
    AGLEventHandler* handler = static_cast< AGLEventHandler* >( userData );
    AGLWindowIF*     window  = handler->_window;

    if( GetCurrentEventQueue() == GetMainEventQueue( )) // main thread
    {
        const Pipe* pipe = window->getPipe();
        if( !pipe->isThreaded( ))
            // non-threaded window, handle from main thread
            handler->_handleEvent( event );

    }
    else
        handler->_handleEvent( event );

    return CallNextEventHandler( nextHandler, event );
}

bool AGLEventHandler::_handleEvent( EventRef event )
{
    switch( GetEventClass( event ))
    {
        case kEventClassWindow:
            return _handleWindowEvent( event );
        case kEventClassMouse:
            return _handleMouseEvent( event );
        case kEventClassKeyboard:
            return _handleKeyEvent( event );
        default:
            EQINFO << "Unknown event class " << GetEventClass( event ) << endl;
            return false;
    }
}

bool AGLEventHandler::_handleWindowEvent( EventRef event )
{
    AGLWindowEvent windowEvent;
    windowEvent.carbonEventRef = event;
    Window* const window       = _window->getWindow();

    Rect      rect;
    WindowRef carbonWindow = _window->getCarbonWindow();
    GetWindowPortBounds( carbonWindow, &rect );
    windowEvent.resize.x = rect.top;
    windowEvent.resize.y = rect.left;
    windowEvent.resize.h = rect.bottom - rect.top;
    windowEvent.resize.w = rect.right  - rect.left;

    switch( GetEventKind( event ))
    {
        case kEventWindowBoundsChanged:
        case kEventWindowZoomed:
            windowEvent.type = Event::WINDOW_RESIZE;
            break;

        case kEventWindowUpdate:
            BeginUpdate( carbonWindow );
            EndUpdate( carbonWindow );
            // no break;
        case kEventWindowDrawContent:
            windowEvent.type = Event::EXPOSE;
            break;

        case kEventWindowClosed:
            windowEvent.type = Event::WINDOW_CLOSE;
            break;

        case kEventWindowHidden:
        case kEventWindowCollapsed:
            windowEvent.type = Event::WINDOW_HIDE;
            break;
            
        case kEventWindowShown:
        case kEventWindowExpanded:
            windowEvent.type = Event::WINDOW_SHOW;
            if( carbonWindow == FrontNonFloatingWindow( ))
                SetUserFocusWindow( carbonWindow );
            break;

        default:
            EQINFO << "Unhandled window event " << GetEventKind( event ) <<endl;
            windowEvent.type = Event::UNKNOWN;
            break;
    }
    windowEvent.originator = window->getID();

    EQLOG( LOG_EVENTS ) << "received event: " << windowEvent << endl;
    return _window->processEvent( windowEvent );
}

bool AGLEventHandler::_handleMouseEvent( EventRef event )
{
    HIPoint        pos;
    AGLWindowEvent windowEvent;

    windowEvent.carbonEventRef = event;
    Window* const window       = _window->getWindow();
    
    const bool    decoration =
        window->getIAttribute( Window::IATTR_HINT_DECORATION ) != OFF;
    const int32_t menuHeight = decoration ? EQ_AGL_MENUBARHEIGHT : 0 ;

    switch( GetEventKind( event ))
    {
        case kEventMouseMoved:
        case kEventMouseDragged:
            windowEvent.type                  = Event::POINTER_MOTION;
            windowEvent.pointerMotion.button  = PTR_BUTTON_NONE;
            // Note: Luckily GetCurrentEventButtonState returns the same bits as
            // our button definitions.
            windowEvent.pointerMotion.buttons = _getButtonState();

            if( windowEvent.pointerMotion.buttons == PTR_BUTTON1 )
            {   // Only left button pressed: implement apple-style middle/right
                // button if modifier keys are used.
                uint32_t keys = 0;
                GetEventParameter( event, kEventParamKeyModifiers, 
                                   typeUInt32, 0, sizeof( keys ), 0, &keys );
                if( keys & controlKey )
                    windowEvent.pointerMotion.buttons = PTR_BUTTON3;
                else if( keys & optionKey )
                    windowEvent.pointerMotion.buttons = PTR_BUTTON2;
            }

            GetEventParameter( event, kEventParamWindowMouseLocation, 
                               typeHIPoint, 0, sizeof( pos ), 0, 
                               &pos );
            if( pos.y < menuHeight )
                return false; // ignore pointer events on the menu bar

            windowEvent.pointerMotion.x = static_cast< int32_t >( pos.x );
            windowEvent.pointerMotion.y = static_cast< int32_t >( pos.y ) -
                                               menuHeight;

            GetEventParameter( event, kEventParamMouseDelta, 
                               typeHIPoint, 0, sizeof( pos ), 0, 
                               &pos );
            windowEvent.pointerMotion.dx = static_cast< int32_t >( pos.x );
            windowEvent.pointerMotion.dy = static_cast< int32_t >( pos.y );

            _lastDX = windowEvent.pointerMotion.dx;
            _lastDY = windowEvent.pointerMotion.dy;

            _getRenderContext( window, windowEvent );
            break;

        case kEventMouseDown:
            windowEvent.type = Event::POINTER_BUTTON_PRESS;
            windowEvent.pointerMotion.buttons = _getButtonState();
            windowEvent.pointerButtonPress.button  =
                _getButtonAction( event );

            if( windowEvent.pointerMotion.buttons == PTR_BUTTON1 )
            {   // Only left button pressed: implement apple-style middle/right
                // button if modifier keys are used.
                uint32_t keys = 0;
                GetEventParameter( event, kEventParamKeyModifiers, 
                                   typeUInt32, 0, sizeof( keys ), 0, &keys );
                if( keys & controlKey )
                    windowEvent.pointerMotion.buttons = PTR_BUTTON3;
                else if( keys & optionKey )
                    windowEvent.pointerMotion.buttons = PTR_BUTTON2;
            }

            GetEventParameter( event, kEventParamWindowMouseLocation, 
                               typeHIPoint, 0, sizeof( pos ), 0, 
                               &pos );
            if( pos.y < menuHeight )
                return false; // ignore pointer events on the menu bar

            windowEvent.pointerButtonPress.x = 
                static_cast< int32_t >( pos.x );
            windowEvent.pointerButtonPress.y = 
                static_cast< int32_t >( pos.y ) - menuHeight;

            windowEvent.pointerButtonPress.dx = _lastDX;
            windowEvent.pointerButtonPress.dy = _lastDY;
            _lastDX = 0;
            _lastDY = 0;

            _getRenderContext( window, windowEvent );
            break;

        case kEventMouseUp:
            windowEvent.type = Event::POINTER_BUTTON_RELEASE;
            windowEvent.pointerMotion.buttons = _getButtonState();
            windowEvent.pointerButtonRelease.button = 
                _getButtonAction( event );

            if( windowEvent.pointerMotion.buttons == PTR_BUTTON1 )
            {   // Only left button pressed: implement apple-style middle/right
                // button if modifier keys are used.
                uint32_t keys = 0;
                GetEventParameter( event, kEventParamKeyModifiers, 
                                   typeUInt32, 0, sizeof( keys ), 0, &keys );
                if( keys & controlKey )
                    windowEvent.pointerMotion.buttons = PTR_BUTTON3;
                else if( keys & optionKey )
                    windowEvent.pointerMotion.buttons = PTR_BUTTON2;
            }

            GetEventParameter( event, kEventParamWindowMouseLocation, 
                               typeHIPoint, 0, sizeof( pos ), 0, 
                               &pos );
            if( pos.y < menuHeight )
                return false; // ignore pointer events on the menu bar

            windowEvent.pointerButtonRelease.x = 
                static_cast< int32_t>( pos.x );
            windowEvent.pointerButtonRelease.y = 
                static_cast< int32_t>( pos.y ) - menuHeight;

            windowEvent.pointerButtonRelease.dx = _lastDX;
            windowEvent.pointerButtonRelease.dy = _lastDY;
            _lastDX = 0;
            _lastDY = 0;

            _getRenderContext( window, windowEvent );
            break;

        default:
            EQINFO << "Unhandled mouse event " << GetEventKind( event ) << endl;
            windowEvent.type = Event::UNKNOWN;
            break;
    }
    windowEvent.originator = window->getID();

    EQLOG( LOG_EVENTS ) << "received event: " << windowEvent << endl;
    return _window->processEvent( windowEvent );
}

bool AGLEventHandler::_handleKeyEvent( EventRef event )
{
    AGLWindowEvent windowEvent;

    windowEvent.carbonEventRef = event;
    Window* const window       = _window->getWindow();

    switch( GetEventKind( event ))
    {
        case kEventRawKeyDown:
        case kEventRawKeyRepeat:
            windowEvent.type         = Event::KEY_PRESS;
            windowEvent.keyPress.key = _getKey( event );
            break;

        case kEventRawKeyUp:
            windowEvent.type         = Event::KEY_RELEASE;
            windowEvent.keyPress.key = _getKey( event );
            break;

        default:
            EQINFO << "Unhandled keyboard event " << GetEventKind( event )
                   << endl;
            windowEvent.type = Event::UNKNOWN;
            break;
    }
    windowEvent.originator = window->getID();

    EQLOG( LOG_EVENTS ) << "received event: " << windowEvent << endl;
    return _window->processEvent( windowEvent );
}

uint32_t AGLEventHandler::_getButtonState()
{
    const uint32 buttons = GetCurrentEventButtonState();
    
    // swap button 2&3
    return ( (buttons & 0xfffffff9u) +
             ((buttons & EQ_BIT3) >> 1) +
             ((buttons & EQ_BIT2) << 1) );
}


uint32_t AGLEventHandler::_getButtonAction( EventRef event )
{
    EventMouseButton button;
    GetEventParameter( event, kEventParamMouseButton, 
                               typeMouseButton, 0, sizeof( button ), 0, 
                               &button );

    switch( button )
    {    
        case kEventMouseButtonPrimary:   return PTR_BUTTON1;
        case kEventMouseButtonSecondary: return PTR_BUTTON3;
        case kEventMouseButtonTertiary:  return PTR_BUTTON2;
        default: return PTR_BUTTON_NONE;
    }
}

uint32_t AGLEventHandler::_getKey( EventRef event )
{
    unsigned char key;
    GetEventParameter( event, kEventParamKeyMacCharCodes, typeChar, 0,
                       sizeof( char ), 0, &key );
    switch( key )
    {
        case kEscapeCharCode:     return KC_ESCAPE;    
        case kBackspaceCharCode:  return KC_BACKSPACE; 
        case kReturnCharCode:     return KC_RETURN;    
        case kTabCharCode:        return KC_TAB;       
        case kHomeCharCode:       return KC_HOME;       
        case kLeftArrowCharCode:  return KC_LEFT;       
        case kUpArrowCharCode:    return KC_UP;         
        case kRightArrowCharCode: return KC_RIGHT;      
        case kDownArrowCharCode:  return KC_DOWN;       
        case kPageUpCharCode:     return KC_PAGE_UP;    
        case kPageDownCharCode:   return KC_PAGE_DOWN;  
        case kEndCharCode:        return KC_END;        
#if 0
        case XK_F1:        return KC_F1;         
        case XK_F2:        return KC_F2;         
        case XK_F3:        return KC_F3;         
        case XK_F4:        return KC_F4;         
        case XK_F5:        return KC_F5;         
        case XK_F6:        return KC_F6;         
        case XK_F7:        return KC_F7;         
        case XK_F8:        return KC_F8;         
        case XK_F9:        return KC_F9;         
        case XK_F10:       return KC_F10;        
        case XK_F11:       return KC_F11;        
        case XK_F12:       return KC_F12;        
        case XK_F13:       return KC_F13;        
        case XK_F14:       return KC_F14;        
        case XK_F15:       return KC_F15;        
        case XK_F16:       return KC_F16;        
        case XK_F17:       return KC_F17;        
        case XK_F18:       return KC_F18;        
        case XK_F19:       return KC_F19;        
        case XK_F20:       return KC_F20;        
        case XK_Shift_L:   return KC_SHIFT_L;    
        case XK_Shift_R:   return KC_SHIFT_R;    
        case XK_Control_L: return KC_CONTROL_L;  
        case XK_Control_R: return KC_CONTROL_R;  
        case XK_Alt_L:     return KC_ALT_L;      
        case XK_Alt_R:     return KC_ALT_R;
#endif
            
        default: 
            // 'Useful' Latin1 characters
            if(( key >= ' ' && key <= '~' ) ||
               ( key >= 0xa0 /*XK_nobreakspace && key <= XK_ydiaeresis*/ ))

                return key;

            EQWARN << "Unrecognized key " << key << endl;
            return KC_VOID;
    }
}
}
