#include <stdlib.h>
#include <assert.h>
#include <gtk/gtk.h>
#include "engine.h"
#include "UI.h"
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
            pe.button = 1;
        }
        break;
    default:
        assert(FALSE);
    }

    return pe;
}

typedef struct {
    PointerEvent event;
    IBusHandwriteEngine * engine;
} PostEvent;

gboolean
handle_pointer_event(PostEvent * pe)
{
    PointerEvent event = pe->event;
    IBusHandwriteEngine * engine = pe->engine;

    /* currently we only support the first mouse button. */
    if (event.button != 1)
        return FALSE;

    /* re-calculate positions. */
    double width = gtk_widget_get_allocated_width(engine->drawpanel);
    double height = gtk_widget_get_allocated_height(engine->drawpanel);

    event.x = event.x * width / event.width;
    event.y = event.y * height / event.height;

    GdkWindow * root_window = gdk_screen_get_root_window
	    (gdk_screen_get_default());

    switch (event.type) {
    case ButtonPress:
        {
            GdkCursorType ct = GDK_PENCIL;
            gdk_window_set_cursor(root_window, gdk_cursor_new(ct));

            engine->mouse_state = GDK_BUTTON_PRESS;

            g_print("mouse clicked\n");

            engine->currentstroke.segments = 1;

            engine->currentstroke.points = g_new(GdkPoint,1);

            engine->currentstroke.points[0].x = event.x;
            engine->currentstroke.points[0].y = event.y;
        }
        break;
    case ButtonRelease:
        {
            gdk_window_set_cursor(root_window, NULL);

            engine->mouse_state = GDK_BUTTON_RELEASE;

            ibus_handwrite_recog_append_stroke(engine->engine,engine->currentstroke);

            engine->currentstroke.segments = 0;
            g_free(engine->currentstroke.points);

            engine->currentstroke.points = NULL;

            ibus_handwrite_recog_domatch(engine->engine,10);

            g_print("mouse released\n");

            gtk_widget_queue_draw(engine->drawpanel);

            regen_loopuptable(engine->lookuppanel,engine);
        }
        break;
    case MotionNotify:
        {
            engine->currentstroke.points
                = g_renew(GdkPoint,engine->currentstroke.points,
                          engine->currentstroke.segments +1  );

            engine->currentstroke.points[engine->currentstroke.segments].x
                = event.x;
            engine->currentstroke.points[engine->currentstroke.segments].y
                = event.y;
            engine->currentstroke.segments++;
            printf("move, x= %lf, Y=%lf, segments = %d \n",event.x,event.y
                   ,engine->currentstroke.segments);

            gtk_widget_queue_draw(engine->drawpanel);
        }
        break;
    default:
        assert(FALSE);
    }

    free(pe);
    return FALSE;
}

gboolean
PostXEvent(XEvent * event, IBusHandwriteEngine * engine)
{
    PointerEvent pe = ConvertXEvent(event);
    PostEvent * ev = malloc(sizeof(PostEvent));
    ev->event = pe;
    ev->engine = engine;
    gdk_threads_add_idle((GSourceFunc)handle_pointer_event, ev);
    return TRUE;
}
