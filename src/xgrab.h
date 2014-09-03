#ifndef X_GRAB_H
#define X_GRAB_H

#define HAVE_APP_GTK

#ifdef HAVE_APP_GTK
#include <gdk/gdkx.h>
#endif

typedef struct {
    GdkEventType type;
    double x, y;
    int width, height;
    int button;
} PointerEvent;


#endif
