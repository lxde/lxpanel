/*
 *      ptk-ui-xml.h -Lightweight parser for *.glade
 *
 *      Copyright 2008 PCMan <pcman.tw@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifndef _PTK_UI_XML_H_
#define _PTK_UI_XML_H_

#include <gtk/gtk.h>
#include "ptk-xml-tree.h"

G_BEGIN_DECLS

typedef struct _PtkUIXml PtkUIXml;

GtkWidget* ptk_ui_xml_create_widget( XmlNode* tree );
GtkWidget* ptk_ui_xml_create_widget_from_file( const char* file );

GtkWidget* ptk_ui_xml_get_widget( GtkWidget* top_widget, const char* name );

PtkUIXml* ptk_ui_xml_get( GtkWidget* widget );
PtkUIXml* ptk_ui_xml_destroy( PtkUIXml*xml );

void ptk_ui_xml_ref( PtkUIXml* xml );
void ptk_ui_xml_unref( PtkUIXml* xml );

GtkWidget* ptk_ui_xml_lookup( PtkUIXml* xml,
                              const char* name );


G_END_DECLS

#endif
