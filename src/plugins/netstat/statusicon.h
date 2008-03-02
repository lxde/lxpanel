#ifndef HAVE_NS_STATUSICON_H
#define HAVE_NS_STATUSICON_H

#include "fnetdaemon.h"

statusicon *create_statusicon(GtkWidget *box, const char *filename, const char *tooltips);
void statusicon_destroy(statusicon *icon);
void set_statusicon_image_from_file(statusicon *widget, const char *filename);
void set_statusicon_tooltips(statusicon *widget, const char *tooltips);
void set_statusicon_visible(statusicon *widget, gboolean b);

#endif
