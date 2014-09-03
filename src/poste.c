#include <assert.h>
#include "xgrab.h"

PointerEvent
ConvertXEvent(XEvent * event)
{
    PointerEvent pe;
    XWindowAttributes attrs;

    switch (event->type) {
    case ButtonPress:
        {
            XButtonEvent * ev = (XButtonEvent *) event;
            pe.type = GDK_BUTTON_PRESS;
            pe.x = ev->x; pe.y = ev->y;
            XGetWindowAttributes(ev->display, ev->window, &attrs);
            pe.width = attrs.width; pe.height = attrs.width;
            pe.button = ev->button;
        }
        break;
    case ButtonRelease:
        {
            XButtonEvent * ev = (XButtonEvent *) event;
            pe.type = GDK_BUTTON_RELEASE;
            pe.x = ev->x; pe.y = ev->y;
            XGetWindowAttributes(ev->display, ev->window, &attrs);
            pe.width = attrs.width; pe.height = attrs.width;
            pe.button = ev->button;
        }
        break;
    case MotionNotify:
        {
            XMotionEvent * ev = (XMotionEvent *) event;
            pe.type = GDK_MOTION_NOTIFY;
            pe.x = ev->x; pe.y = ev->y;
            XGetWindowAttributes(ev->display, ev->window, &attrs);
            pe.width = attrs.width; pe.height = attrs.width;
            /* currently we only support the first mouse button. */
            pe.button = 0;
        }
        break;
    default:
        assert(FALSE);
    }

    return pe;
}
