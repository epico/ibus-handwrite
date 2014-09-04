#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XIproto.h>
#include <X11/Xos.h>

#include <stdio.h>
#include <pthread.h>
#include <glib.h>

#include "xgrab.h"

/* The X11 display name. */
static const char * x11_display_name = NULL;

/* The X11 display handle. */
static Display * x11_display = NULL;

/* The X11 root window handle. */
static Window root_window;

/* thread state */
/* static gboolean running; */

/* Abort the main loop. */
static gboolean is_aborted = TRUE;

/* The activity monitor thread. */
static pthread_t thread_id;

void ErrorTrapEnter();
void ErrorTrapExit();
void HandleCreate(XEvent *event);
void HandleButtonPress(XEvent *event, IBusHandwriteEngine * engine);
void HandleMotion(XEvent *event, IBusHandwriteEngine * engine);
void HandleButtonRelease(XEvent *event, IBusHandwriteEngine * engine);
void ToggleXGrab(IBusHandwriteEngine * engine);

#ifndef HAVE_APP_GTK
static int (*old_handler)(Display *dpy, XErrorEvent *error);
#endif

#ifndef HAVE_APP_GTK
//! Intercepts X11 protocol errors.
static int
errorHandler(Display *dpy, XErrorEvent *error)
{
    (void)dpy;

    if (error->error_code == BadWindow || error->error_code==BadDrawable)
        return 0;
    return 0;
}
#endif

/* Obtains the next X11 event with specified timeout. */
static Bool
XNextEventTimed(Display* dsp, XEvent* event_return, long millis)
{
    if (millis == 0)
        {
            XNextEvent(dsp, event_return);
            return True;
        }

    struct timeval tv;
    tv.tv_sec = millis / 1000;
    tv.tv_usec = (millis % 1000) * 1000;

    XFlush(dsp);
    if (XPending(dsp))
        {
            XNextEvent(dsp, event_return);
            return True;
        }
    else
        {
            int fd = ConnectionNumber(dsp);
            fd_set readset;
            FD_ZERO(&readset);
            FD_SET(fd, &readset);
            if (select(fd+1, &readset, NULL, NULL, &tv) <= 0)
                {
                    return False;
                }
            else
                {
                    if (XPending(dsp))
                        {
                            XNextEvent(dsp, event_return);
                            return True;
                        }
                    else
                        {
                            return False;
                        }
                }
        }
}


static void
SetEventMask(Window window)
{
    XWindowAttributes   attrs;
    unsigned long   events;
    Window    root,parent,*children;
    unsigned int    nchildren;
    char      *name;

    if (!XQueryTree(x11_display, window, &root, &parent, &children, &nchildren))
        return;

    if (XFetchName(x11_display, window, &name))
        {
            /* printf("Watching: %s\n", name); */
            XFree(name);
        }

    if (parent == None)
        {
            attrs.all_event_masks = ButtonPressMask|PointerMotionMask|ButtonReleaseMask;
            attrs.do_not_propagate_mask = NoEventMask;
        }
    else
        {
            XGetWindowAttributes(x11_display, window, &attrs);
        }

    events=((attrs.all_event_masks | attrs.do_not_propagate_mask) & (ButtonPressMask|PointerMotionMask|ButtonReleaseMask));

    XSelectInput(x11_display, window, SubstructureNotifyMask|PropertyChangeMask|EnterWindowMask|events);

    if (children)
        {
            while (nchildren)
                {
                    SetEventMask(children[--nchildren]);
                }
            XFree(children);
        }
}

void
SetAllEvent(Window window)
{
    ErrorTrapEnter();

    SetEventMask(window);
    XSync(x11_display,False);

    ErrorTrapExit();
}


void *
StartXGrab(IBusHandwriteEngine * engine)
{
    if ((x11_display = XOpenDisplay(x11_display_name)) == NULL)
        {
            return NULL;
        }

    ErrorTrapEnter();

    root_window = DefaultRootWindow(x11_display);
    SetAllEvent(root_window);

    XGrabButton(x11_display, AnyButton, AnyModifier, root_window, True,
                ButtonPressMask|PointerMotionMask|ButtonReleaseMask,
                GrabModeAsync, GrabModeAsync, None, None);

    XSync(x11_display,False);

    ErrorTrapExit();

    while (1)
        {
            XEvent event;
            Bool gotEvent = XNextEventTimed(x11_display, &event, 100);

            if (is_aborted)
                {
                    return NULL;
                }

            if (gotEvent)
                {
                    ErrorTrapEnter();

                    switch (event.xany.type)
                        {
                        case CreateNotify:
                            HandleCreate(&event);
                            break;

                        case ButtonPress:
			    HandleButtonPress(&event, engine);
                            break;                            

                        case MotionNotify:
                            HandleMotion(&event, engine);
                            break;                            

                        case ButtonRelease:
                            HandleButtonRelease(&event, engine);
                            break;
                        }

                    ErrorTrapExit();
                }
        }

    return NULL;
}

void
HandleCreate(XEvent *event)
{
    printf("received create event.\n");
    SetAllEvent(event->xcreatewindow.window);
}

void
HandleButtonPress(XEvent *event, IBusHandwriteEngine * engine)
{
    printf("received press event.\n");

    PostXEvent(event, engine);
}

void
HandleMotion(XEvent *event, IBusHandwriteEngine * engine)
{
    printf("received motion event.\n");

    PostXEvent(event, engine);
}

void
HandleButtonRelease(XEvent *event, IBusHandwriteEngine * engine)
{
    printf("received release event.\n");

    PostXEvent(event, engine);
}

void
ErrorTrapEnter()
{
#ifdef HAVE_APP_GTK
    gdk_error_trap_push();
#else
    old_handler = XSetErrorHandler(&errorHandler);
#endif
}

void
ErrorTrapExit()
{
#ifdef HAVE_APP_GTK
    gdk_flush ();
    gint err = gdk_error_trap_pop();
    (void) err;
#else
    XSetErrorHandler(old_handler);
#endif
}

void
ToggleXGrab(IBusHandwriteEngine * engine)
{
    if (is_aborted) { /* start thread. */
        is_aborted = FALSE;
        pthread_create(&thread_id, NULL, StartXGrab, engine);
    } else { /* wait thread exit.*/
        is_aborted = TRUE;
        pthread_join(thread_id, NULL);
    }
}
