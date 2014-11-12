#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
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
void HandleXI2Event(XEvent *event, IBusHandwriteEngine * engine);
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


    /* register for XI2, claim we support 2.0 */
    int major = 2,
        minor = 0;
    XIQueryVersion(x11_display, &major, &minor);

    /* Now search for the actual device that we want. XI2 doesn't
       actually provide all we need, so we mix XI 1.x for finding the device
       and switching its mode, then XI2 for the events. sorry...
    */
    int deviceid = 0; /* for XI2 */
    XDevice *xdevice = NULL; /* for XI 1.x calls */
    int touchpad_type = XInternAtom(x11_display, "TOUCHPAD", True);

    if (touchpad_type == None) {
        printf("Nothing of type touchpad ever initialized\n");
        return NULL;
    }

    XDeviceInfo *info;
    int ndevices;
    info = XListInputDevices(x11_display, &ndevices);

    int i;
    for (i = 0; i < ndevices; i++) {
        const XDeviceInfo *dev = &info[i];

        if (dev->type == touchpad_type) {
            xdevice = XOpenDevice(x11_display, dev->id);
            deviceid = dev->id;
            printf("Using touchpad '%s'\n", dev->name);
            break;
        }
    }

    XFreeDeviceList(info);

    if (deviceid == 0 || xdevice == NULL) {
        printf("failed to find device\n");
        return NULL;
    }

    /* note: this isn't needed anymore if you just want the touchpad events */
    root_window = DefaultRootWindow(x11_display);
    SetAllEvent(root_window);

    /* set up the XI2 event mask */
    unsigned char evmask[XIMaskLen(XI_LASTEVENT)] = {0};
    XISetMask(evmask, XI_ButtonPress);
    XISetMask(evmask, XI_ButtonRelease);
    XISetMask(evmask, XI_Motion);

    XIEventMask mask;
    mask.deviceid = deviceid; /* not really needed, ignored by XIGrabDevice */
    mask.mask = evmask;
    mask.mask_len = sizeof(evmask);

    /* now grab the one device that we actually care about directly.
       This will detach it from the system cursor until ungrabbed, but
       all other devices will continue to work just fine */
    int status;
    status = XIGrabDevice(x11_display, deviceid, root_window,
                          CurrentTime, None,
                          GrabModeAsync, GrabModeAsync,
                          False, &mask);
    if (status != Success)
        printf("Failed to grab device: %d\n", status);

    /* ok, we have the grab on the device.
       Now switch it to absolute mode.
       This needs to be undone on toggle.
       */
    XSetDeviceMode(x11_display, xdevice, Absolute);

    /* FIXME: what's missing here is to disable the touchpad's extra features
      (software buttons, tapping, etc.). that needs to be done by hand atm
      until we find something that's reasonable for upstream.
     */

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
                        case GenericEvent:
                            /* must be an XI2 event, we didn't register
                               for any other generic events */
                            HandleXI2Event(&event, engine);
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
HandleXI2Event(XEvent *event, IBusHandwriteEngine *engine)
{
    XIDeviceEvent *xev;

    printf("%s\n", __func__);

    if (!XGetEventData(event->xcookie.display, &event->xcookie)) {
        printf("error getting event data\n");
        return;
    }

    xev = (XIDeviceEvent*)event->xcookie.data;

    printf("XI2 event type: %d\n", xev->evtype);

    /* for simplicity, convert the XI2 event into a core event
       and pass it to the engine. This only converts the fields
       that we use in ConvertXEvent */
    XEvent fake_event;
    fake_event.xbutton.display = xev->display;
    fake_event.xbutton.x = xev->event_x;
    fake_event.xbutton.y = xev->event_y;
    fake_event.xbutton.window = xev->event;
    /* XI2 evtypes == X11 core types for pointer events */
    fake_event.xbutton.type = xev->evtype;

    PostXEvent(&fake_event, engine);
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
