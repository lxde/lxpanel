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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>

#include "ptk-xml-tree.h"
#include "ptk-ui-xml.h"

#include "glib-mem.h" /* compatibility macros for g_slice API */

struct _PtkUIXml
{
    guint n_ref;
    GHashTable* hash;
    GtkAccelGroup* acc;
    GtkTooltips* tips;
    GtkWidget* top_level;
    GtkWidget* def;
    GtkWidget* focus;
};

static GModule* this_mod = NULL;

static GQuark ptk_ui_xml_id = 0;

static PtkUIXml* ptk_ui_xml_new();
static void ptk_ui_xml_free( PtkUIXml* xml );

static GtkWidget* create_widget( GtkWidget* parent, XmlNode* node,
                                const char* internal, PtkUIXml* xml );

static gboolean strtobool( const char* str )
{
    return str && !strcmp(str, "True");
}
static gboolean enum_to_int( GType type, const char* name, int* val );

static char* get_prop( XmlNode* node, const char* name )
{
    GSList* l;
    for( l = node->children; l; l = l->next )
    {
        XmlNode* child = (XmlNode*)l->data;
        const char* prop_name;
        if( 0 == strcmp(child->name, "property") )
        {
            prop_name = xml_node_get_prop( child, "name" );
            if( prop_name && 0 == strcmp(prop_name, name) )
                return child->cdata;
        }
    }
    return NULL;
}

static char* get_prop_name( XmlNode* prop_node )
{
    char **prop, ** val;
    if( prop_node->name && (0 == strcmp( prop_node->name, "property" )) && prop_node->props )
    {
        for( prop = prop_node->props, val = prop_node->vals; *prop; ++prop, ++val )
        {
            if( 0 == strcmp( "name", *prop ) )
                return *val;
        }
    }
    return NULL;
}

static void init_combo_box( GtkComboBox* combo )
{
    GtkListStore* list;
    GtkCellRenderer* cell;
    list = gtk_list_store_new( 1, G_TYPE_STRING );
    gtk_combo_box_set_model( combo, GTK_TREE_MODEL(list) );
    g_object_unref( list );

    if( GTK_IS_COMBO_BOX_ENTRY( combo ) )
    {
        gtk_combo_box_entry_set_text_column((GtkComboBoxEntry*)combo, 0);
    }
    else
    {
        /* Taken from gtkcombox.c of gtk+ */
        cell = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo),
                                    cell, TRUE );
        gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT(combo),
                                        cell,
                                        "text", 0,
                                        NULL);
    }
}

static gboolean is_translatable( XmlNode* prop_node )
{
    const char* translatable = xml_node_get_prop( prop_node, "translatable");
    return (translatable && 0 == strcmp(translatable, "yes" ) );
}

static void
set_widget_special_property( GtkWidget* widget,
                                         GSList* special_props,
                                         PtkUIXml* xml )
{
    GSList* l;
    XmlNode* prop_node;
    const char* name;

    for( l = special_props; l; l = l->next )
    {
        prop_node = (XmlNode*)l->data;
        if( G_UNLIKELY( ! prop_node->cdata || ! *prop_node->cdata ) )
            continue;
        name = get_prop_name( prop_node );
        if( name )
        {
            if( 0 == strcmp( name, "tooltip" ) )
            {
                if( G_UNLIKELY(! xml->tips ) )
                {
                    xml->tips = gtk_tooltips_new();
        #if GTK_CHECK_VERSION(2, 10, 0)
                    g_object_ref_sink( xml->tips );
                    g_object_unref ( xml->tips );
        #else
                    gtk_object_sink( (GtkObject*)xml->tips );
        #endif
                }
                if( is_translatable( prop_node ) )
                    gtk_tooltips_set_tip( xml->tips, widget, _(prop_node->cdata), _(prop_node->cdata) );
                else
                    gtk_tooltips_set_tip( xml->tips, widget, prop_node->cdata, prop_node->cdata );
           }
            else if( 0 == strcmp( name, "text" ) && GTK_IS_TEXT_VIEW( widget ) )
            {
                GtkTextBuffer* buf = gtk_text_buffer_new( NULL );
                const char* text = NULL;
            if (is_translatable( prop_node ))
            text = _(text);
                gtk_text_buffer_set_text( buf, text, strlen(text) );
                gtk_text_view_set_buffer( GTK_TEXT_VIEW(widget), buf );
                g_object_unref( buf );
            }
            else if( 0 == strcmp( name, "display_options" ) && GTK_IS_CALENDAR( widget ) )
            {
                int val = 0;
                if( enum_to_int( GTK_TYPE_CALENDAR_DISPLAY_OPTIONS, prop_node->cdata, &val ) )
                    gtk_calendar_set_display_options( GTK_CALENDAR(widget), val);
            }
            else if( 0 == strcmp( name, "response_id" ) && GTK_IS_BUTTON(widget) )
            {
                if( G_LIKELY( GTK_IS_DIALOG( xml->top_level ) ) )
                {
                    int id = atoi( prop_node->cdata );
                    if( id != 0 )
                        gtk_dialog_add_action_widget( GTK_DIALOG(xml->top_level), widget, id );
                }
            }
            else if( 0 == strcmp( name, "items" ) && GTK_IS_COMBO_BOX(widget) )
            {
                if( prop_node->cdata )
                {
                    const char* lines = is_translatable( prop_node ) ? _(prop_node->cdata) : prop_node->cdata;
                    char** items = g_strsplit(lines, "\n", -1);
                    char** item;

                    for( item = items; *item ; ++item )
                    {
                        if( **item )
                            gtk_combo_box_append_text( GTK_COMBO_BOX(widget),
                                                    *item );
                    }
                    g_strfreev( items );
                }
            }
            else if( GTK_IS_WINDOW( widget ) )
            {
                /*
                if( 0 == strcmp( name, "wmclass_name" ) )
                {
                }
                else if( 0 == strcmp( name, "wmclass_class" ) )
                {
                }
                */
                g_warning( "%s not supported", name );
            }
        }
    }
}

gboolean enum_to_int( GType type, const char* name, int* val )
{
    gchar *endptr;
    int ret;

    if( G_UNLIKELY( !name || !*name ) )
    {
        *val = 0;
        return TRUE;
    }
    ret = strtoul(name, &endptr, 0);
    if (endptr != name) /* parsed a number */
    {
        *val = ret;
        return TRUE;
    }

    if( !type ) /* guess the type */
    {
        if( g_str_has_prefix(name, "GTK_SHADOW") )
            type = GTK_TYPE_SHADOW_TYPE;
        else if( g_str_has_prefix(name, "GTK_SELECTION") )
            type = GTK_TYPE_SELECTION_MODE;
        else if( g_str_has_prefix(name, "GTK_TREE_VIEW") )
            type = GTK_TYPE_TREE_VIEW_MODE;
        else if( g_str_has_prefix(name, "GTK_METRIC") )
            type = GTK_TYPE_METRIC_TYPE;
        else if( g_str_has_prefix(name, "GTK_PREVIEW") )
            type = GTK_TYPE_METRIC_TYPE;
    }

    if( G_LIKELY(type) )
    {
        gpointer klass = g_type_class_ref(type);
        if( G_IS_ENUM_CLASS( klass ) )
        {
            GEnumClass *ec = (GEnumClass*)klass;
            GEnumValue *ev;
            ev = g_enum_get_value_by_name(ec, name);
            if (!ev)
                ev = g_enum_get_value_by_nick(ec, name);
            if (ev)
            {
                ret = ev->value;
                g_type_class_unref(klass);
                *val = ret;
                /* g_debug("ENUM: %s=%d", name, *val); */
                return TRUE;
            }
        }
        else if( G_IS_FLAGS_CLASS( klass ) )
        {
            GFlagsClass *fc = (GFlagsClass*)klass;
            GFlagsValue *fv;
            fv = g_flags_get_value_by_name(fc, name);
            if (!fv)
                fv = g_flags_get_value_by_nick(fc, name);
            if (fv)
            {
                ret = fv->value;
                g_type_class_unref(klass);
                *val = ret;
                /* g_debug("FLAG: %s=%d", name, *val); */
                return TRUE;
            }
        }
        g_type_class_unref(klass);
    }
    return FALSE;
}

static gboolean
property_to_gparameter( GObjectClass* objcls,
    const char* prop_name, XmlNode* prop_node, GParameter* param )
{
    GParamSpec* spec;
    const char* translatable;
    int ival;
    GValue* gv = &param->value;

    if( G_UNLIKELY( ! prop_node->cdata) )
        return FALSE;
    param->name = prop_name;

    spec = g_object_class_find_property( objcls, param->name );
    if( G_UNLIKELY( !spec ) )
        goto failed;

    g_value_init( gv, spec->value_type );
    /* g_debug("property: %s, %s", param->name, G_VALUE_TYPE_NAME(gv)); */

    switch( G_TYPE_FUNDAMENTAL( spec->value_type ) )
    {
        case G_TYPE_INT:
            if( enum_to_int((GType)NULL, prop_node->cdata, &ival) )
                g_value_set_int( gv, ival );
            else
                goto failed;
            break;
        case G_TYPE_UINT:
            if( enum_to_int((GType)NULL, prop_node->cdata, &ival) )
                g_value_set_uint( gv, ival );
            else
                goto failed;
            break;
        case G_TYPE_STRING:
            translatable = xml_node_get_prop( prop_node, "translatable" );
            if( translatable && 0 == strcmp("yes", translatable) )
                g_value_set_string( gv, _( prop_node->cdata ) );
            else
                g_value_set_string( gv, prop_node->cdata );
            break;
        case G_TYPE_BOOLEAN:
            g_value_set_boolean( gv, strtobool(prop_node->cdata) );
            break;
        case G_TYPE_DOUBLE:
            g_value_set_double( gv,
                        g_strtod(prop_node->cdata, NULL) );
            break;
        case G_TYPE_FLOAT:
            g_value_set_float( gv,
                        (gfloat)g_strtod(prop_node->cdata, NULL) );
            break;
        case G_TYPE_INT64:
            g_value_set_int64( gv, (gint64)atol(prop_node->cdata) );
            break;
        case G_TYPE_UINT64:
            g_value_set_uint64( gv, (guint64)atol(prop_node->cdata) );
            break;
        case G_TYPE_LONG:
            g_value_set_long( gv, (glong)atol(prop_node->cdata) );
            break;
        case G_TYPE_ULONG:
            g_value_set_ulong( gv, (gulong)atol(prop_node->cdata) );
            break;
        case G_TYPE_CHAR:
            g_value_set_char( gv, prop_node->cdata[0] );
            break;
        case G_TYPE_UCHAR:
            g_value_set_uchar( gv, g_utf8_get_char(prop_node->cdata) );
            break;
        case G_TYPE_ENUM:
            if( enum_to_int( spec->value_type, prop_node->cdata, &ival ) )
                g_value_set_enum( gv, ival );
            else
                goto failed;
            break;
        case G_TYPE_FLAGS:
            {
                char** flags = g_strsplit( prop_node->cdata, "| ", 0 );
                int flags_val = 0;
                if( flags )
                {
                    char** flag;
                    for( flag = flags; *flag; ++flag )
                    {
                        if( enum_to_int( spec->value_type, *flag, &ival ) )
                            flags_val |= ival;
                    }
                    g_value_set_flags( gv, flags_val );
                    g_free( flags );
                }
                else
                    goto failed;
            }
            break;
        case G_TYPE_BOXED:
            if( G_UNLIKELY( spec->value_type == GDK_TYPE_COLOR ) )
            {
                GdkColor clr;
                gdk_color_parse( prop_node->cdata, &clr );
                g_value_set_boxed( gv, &clr);
            }
            else if(  G_UNLIKELY( spec->value_type == G_TYPE_STRV )  )
            {
                char **strv;
                if( is_translatable( prop_node ) )
                    strv = g_strsplit ( _(prop_node->cdata), "\n", 0 );
                else
                    strv = g_strsplit ( prop_node->cdata, "\n", 0 );
                g_value_take_boxed (gv, strv);
            }
            break;
       case G_TYPE_OBJECT:
            if( G_UNLIKELY( spec->value_type == GTK_TYPE_ADJUSTMENT ) )
            {
                float val, lower, upper, step, page_inc, page_size;
                GtkObject* adj;
                /* FIXME: There are some prolems here!! */
                sscanf( prop_node->cdata, "%f %f %f %f %f %f",
                        &val, &lower, &upper,
                        &step, &page_inc, &page_size );
                adj = gtk_adjustment_new( val, lower, upper,
                                          step, page_inc, page_size );

                g_value_set_object( gv, adj );

#if GTK_CHECK_VERSION( 2, 10, 0 )
                g_object_ref_sink( adj );
#else
                g_object_ref( adj );
                gtk_object_sink(GTK_OBJECT(adj));
#endif
            }
            else
                goto failed;
            break;
        default:
            g_warning("%s(%s) is not supported", g_type_name(G_TYPE_FUNDAMENTAL( spec->value_type )), G_VALUE_TYPE_NAME(gv) );
            goto failed;
            /*
            if( enum_to_int( spec->value_type, prop_node->cdata, &val ) )
                g_object_set(obj, name, val, NULL);
            */
            break;
/*      unsupported
        case G_TYPE_POINTER:
        case G_TYPE_OBJECT:
        case G_TYPE_BOXED:
        default:
            break;
*/
    };
    return TRUE;
failed:
    param->name = NULL;
    if( G_IS_VALUE( gv ) )
        g_value_unset( gv );
    return FALSE;
}

int strtoint( const char* str )
{
    return str ? atoi( str ) : 0;
}

static gboolean pack_widget( GtkWidget* parent,
                      GtkWidget* widget, XmlNode* node )
{
    GSList* l;
    GSList* pack_props = node->children;
    const char* type = get_prop( node, "type");

    if( G_UNLIKELY( type ) )
    {
        if( 0 == strcmp( type, "tab" ) )
        {
            GtkWidget* page;
            int n = gtk_notebook_get_n_pages( GTK_NOTEBOOK(parent) );
            if( n <= 0 )
                return FALSE;
            page = gtk_notebook_get_nth_page( GTK_NOTEBOOK(parent), n-1);
            gtk_notebook_set_tab_label( GTK_NOTEBOOK(parent),
                                        page, widget );
        }
        else if( 0 == strcmp( type, "label_item" ) )
        {
            if( GTK_IS_EXPANDER( parent ) )
                gtk_expander_set_label_widget(  (GtkExpander*)parent, widget );
            else if( GTK_IS_FRAME( parent ) )
                gtk_frame_set_label_widget(  (GtkFrame*)parent, widget );
        }
    }

    if( ! gtk_widget_is_ancestor( widget, parent ) )
        gtk_container_add( (GtkContainer*)parent, widget );

    for( l = pack_props; l; l = l->next )
    {
        XmlNode* node = (XmlNode*)l->data;
        const char* name = NULL;
        if( !node->name || strcmp( node->name, "property" ) )
            continue;

        name = xml_node_get_prop( node, "name" );
        if( G_LIKELY( name ) )
        {
            GObjectClass* objcls;
            GParamSpec* spec;

            objcls = G_OBJECT_GET_CLASS(parent);
            if( !objcls )
                continue;
            spec = gtk_container_class_find_child_property(
                                         objcls, name );
            if( !spec )
                continue;

            /* g_debug(  "PACK_CHILD_PROP: %s (%s)", name, g_type_name(spec->value_type) ); */
            if( spec->value_type == G_TYPE_BOOLEAN )
            {
                gtk_container_child_set( (GtkContainer*)parent, widget,
                                         name,
                                         strtobool(node->cdata),
                                         NULL );
            }
            else if( spec->value_type )
            {
                int val = 0;
                if( enum_to_int( spec->value_type, node->cdata, &val ) )
                    gtk_container_child_set( (GtkContainer*)parent, widget,
                                             name, val, NULL );
                /* g_debug( "PACKING: %s = %d", name, val ); */
            }
        }
    }
    return TRUE;
}

static void load_child_widget( GtkWidget* parent,
                               XmlNode* child,
                               PtkUIXml* xml )
{
    GSList* l;
    GtkWidget* child_widget = NULL;
    gboolean packing = FALSE;
    const char* internal = xml_node_get_prop(child, "internal-child");

    for( l = child->children; l; l = l->next )
    {
        XmlNode* child_node = (XmlNode*)l->data;
        if( 0 == strcmp( "widget", child_node->name ) )
            child_widget = create_widget( parent, child_node,
                                          internal, xml );
        else if( !packing && 0 == strcmp( "packing", child_node->name ) )
        {
            if( child_widget && parent )
            {
                packing = pack_widget( parent,
                                       child_widget,
                                       child_node );
            }
            child_widget = NULL;
        }
    }
    /* g_debug( "PACK %s to %s", G_OBJECT_TYPE_NAME(child_widget), G_OBJECT_TYPE_NAME(parent) ); */
    /* don't pack internal child */
    if( !packing && child_widget && parent && !internal )
    {
        if( !gtk_widget_is_ancestor( child_widget, parent ) )
            gtk_container_add( GTK_CONTAINER(parent), child_widget );
    }
}

static void set_widget_accel( GtkWidget* parent, XmlNode* child )
{
#if 0
    const char* key = xml_node_get_prop( child, "key" );
    const char* mod = xml_node_get_prop( child, "modifiers" );
    const char* signal = xml_node_get_prop( child, "signal" );
/*
    gtk_widget_add_accelerator( widget,
                    signal,
                    accel_group,
                    stock_item.keyval,
                    stock_item.modifier,
                    GTK_ACCEL_VISIBLE);
*/
#endif
}

static GType load_type( const char* type_name )
{
    char func_name[ 256 ];
    char* pname = func_name;
    GType (*func)();
    GType type = g_type_from_name( type_name );
    if( type )
        return type;

    while( *type_name )
    {
        *pname = g_ascii_tolower(*type_name);
        ++pname;
        ++type_name;
        if( g_ascii_isupper(*type_name) && !g_ascii_isupper(*(type_name-1)) )
        {
            *pname = '_';
            ++pname;
        }
    }
    strcpy( pname, "_get_type" );
    if( g_module_symbol( this_mod, func_name, (gpointer*) (gpointer) &func ) )
        return func();
    return 0;
}

static GtkWidget* get_internal_child( GtkWidget* parent, const char* name )
{
    if( GTK_IS_DIALOG(parent) )
    {
        if( 0 == strcmp(name, "vbox") )
            return GTK_DIALOG(parent)->vbox;
        else if( 0 == strcmp(name, "action_area") )
            return GTK_DIALOG(parent)->action_area;
        if( G_UNLIKELY( GTK_IS_FONT_SELECTION_DIALOG(parent) ) )
        {
            if (0 == strcmp(name, "ok_button"))
                return GTK_FONT_SELECTION_DIALOG(parent)->ok_button;
            if (0 == strcmp(name, "cancel_button"))
                return GTK_FONT_SELECTION_DIALOG(parent)->cancel_button;
            if (0 == strcmp(name, "apply_button"))
                return GTK_FONT_SELECTION_DIALOG(parent)->apply_button;
            if (0 == strcmp(name, "font_selection"))
                return GTK_FONT_SELECTION_DIALOG(parent)->fontsel;
        }
        else if( G_UNLIKELY( GTK_IS_FILE_SELECTION(parent) ) )
        {
            if (0 == strcmp(name, "ok_button"))
                return GTK_FILE_SELECTION(parent)->ok_button;
            if (0 == strcmp(name, "cancel_button"))
                return GTK_FILE_SELECTION(parent)->cancel_button;
            if (0 == strcmp(name, "help_button"))
                return GTK_FILE_SELECTION(parent)->help_button;
        }
        else if( G_UNLIKELY( GTK_IS_COLOR_SELECTION_DIALOG(parent) ) )
        {
            if (0 == strcmp(name, "ok_button"))
                return GTK_COLOR_SELECTION_DIALOG(parent)->ok_button;
            if (0 == strcmp(name, "cancel_button"))
                return GTK_COLOR_SELECTION_DIALOG(parent)->cancel_button;
            if (0 == strcmp(name, "help_button"))
                return GTK_COLOR_SELECTION_DIALOG(parent)->help_button;
            if (0 == strcmp(name, "color_selection"))
                return GTK_COLOR_SELECTION_DIALOG(parent)->colorsel;
        }
    }
    else if( GTK_IS_SCROLLED_WINDOW(parent) )
    {
        if( 0 == strcmp(name, "vscrollbar") )
            return GTK_SCROLLED_WINDOW(parent)->vscrollbar;
        if( 0 == strcmp(name, "hscrollbar") )
            return GTK_SCROLLED_WINDOW(parent)->hscrollbar;
    }
    else if( GTK_IS_COMBO_BOX_ENTRY(parent) )
    {
        if( 0 == strcmp(name, "entry") )
            return gtk_bin_get_child(GTK_BIN(parent));
    }
    parent = gtk_widget_get_parent( parent );
    return parent ? get_internal_child(parent, name) : NULL;
}

static int get_n_props( XmlNode* node )
{
    int n = 0;
    GSList* l;
    for( l = node->children; l; l = l->next )
    {
        XmlNode* child_node = (XmlNode*)l->data;
        if( G_UNLIKELY( !child_node->name ) )
            continue;
        if( 0 == strcmp( "property", child_node->name ) )
            ++n;
    }
    return n;
}

static gboolean delayed_unload_module( gpointer user_data )
{
    if( G_LIKELY(this_mod) )
    {
        g_module_close( this_mod );
        this_mod = NULL;
    }
    return FALSE;
}

static gboolean is_id_meaningful( const char* class_name, const char* id )
{
    class_name += 3;    /* skip "Gtk" */
    while( *class_name && *id )
    {
        if( g_ascii_tolower( *id ) != g_ascii_tolower( *class_name ) )
            return TRUE;
        ++id;
        ++class_name;
    }
    if( ! *id || ! g_ascii_isdigit( *id ) )
        return TRUE;
    return FALSE;
}

static gboolean
need_remove_from_hash( gpointer key, gpointer val, GtkWidget* widget )
{
    return (val == widget);
}

/* This is inefficient, but it's safer to do this */
static void remove_widget_from_hash( PtkUIXml* xml, GtkWidget* widget )
{
    if( G_LIKELY(xml && xml->hash) )
        g_hash_table_foreach_remove( xml->hash, (GHRFunc)need_remove_from_hash, widget );
}

GtkWidget* create_widget( GtkWidget* parent,
                                 XmlNode* node,
                                 const char* internal,
                                 PtkUIXml* xml )
{
    GtkWidget* widget = NULL;
    GSList* l, *special = NULL;
    char **prop, **val;
    char* class_name = NULL;
    char* id = NULL;
    GType type = 0;
    gboolean visible = FALSE, has_default = FALSE, has_focus = FALSE;

    if( G_UNLIKELY(!node->name || strcmp(node->name, "widget") ) )
        return NULL;

    if( G_UNLIKELY(internal) )  /* internal child */
    {
        widget = get_internal_child( parent, internal );
        /* g_debug("INTERNAL: %s, %p", internal, widget);  */
    }

    if( G_LIKELY( !widget ) )
    {
        int n;
        GParameter* params = NULL;
        GObjectClass* objcls = NULL;

        for( prop = node->props, val = node->vals; *prop; ++prop, ++val )
        {
            if( 0 == strcmp( "class", *prop ) )
                class_name = *val;
            else if( 0 == strcmp( "id", *prop ) )
                id = *val;
        }
        /* g_debug("class_name: %s, id=%s", class_name, id); */
        if( G_UNLIKELY(!class_name) )
            return NULL;
        type = load_type( class_name );
        if( G_UNLIKELY(!type) )
            return NULL;

        objcls = g_type_class_ref(type);

        if( G_UNLIKELY( ! objcls ) )
            return NULL;

        n = get_n_props( node );
        params = g_new0( GParameter, n );
        for( n = 0, l = node->children; l; l = l->next )
        {
            XmlNode* prop_node = (XmlNode*)l->data;
            const char* prop_name;
            if( ! prop_node->name || strcmp( prop_node->name, "property" ) )
                continue;
            prop_name = get_prop_name( prop_node );
            if( ! prop_name )
                continue;
            if( G_UNLIKELY( strcmp( prop_name, "visible" ) == 0 ) )
            {
                visible = TRUE;
                continue;
            }
            else if( G_UNLIKELY( strcmp( prop_name, "has_default" ) == 0 ) )
            {
                has_default = TRUE;
                continue;
            }
            else if( G_UNLIKELY( strcmp( prop_name, "has_focus" ) == 0 ) )
            {
                has_focus = TRUE;
                continue;
            }            if( property_to_gparameter( objcls, prop_name, prop_node, &params[n] ) )
                ++n;
            else if( prop_node )
                special = g_slist_prepend( special, prop_node );
        }
        widget = (GtkWidget*)g_object_newv( type, n, params );
        while( n >=0 )
        {
            if( params[n].name )
                g_value_unset( &params[n].value );
            --n;
        }
        g_type_class_unref( objcls );
        g_free( params );
    }

    if( G_UNLIKELY( !widget ) )
    {
        g_slist_free( special );
        return NULL;
    }

    if( G_UNLIKELY( parent == NULL && ! xml->top_level ) )
        xml->top_level = widget;

    /* NOTE: Here is a little trick.
     * is_id_meaningful() will check if the id is an meaningless id
     *  automaitcally generated by glade.
     *  meaningless names such as labal1, label2,...etc. will not be added.
     */
    if( G_LIKELY( id ) && G_UNLIKELY( is_id_meaningful(class_name, id) ) )
    {
        g_hash_table_insert( xml->hash, g_strdup(id), widget );
        g_object_weak_ref( G_OBJECT(widget), (GWeakNotify)remove_widget_from_hash, xml);
    }

    /* create a simple text model for combo boxes */
    if( GTK_IS_COMBO_BOX(widget))
        init_combo_box( (GtkComboBox*)widget );

    if( special )
    {
        set_widget_special_property( widget, special, xml );
        g_slist_free( special );
    }

    for( l = node->children; l; l = l->next )
    {
        XmlNode* child_node = (XmlNode*)l->data;
        if( G_UNLIKELY( ! child_node->name ) )
            continue;
        if( 0 == strcmp( "child", child_node->name ) )
            load_child_widget( widget, child_node, xml );
        else if( 0 == strcmp( "accelerator", child_node->name ) )
            set_widget_accel( widget, child_node );
    }

    if( visible )
        gtk_widget_show( widget );
    if( has_default )
        xml->def = widget;
    if( has_focus )
        xml->focus = widget;
    return widget;
}

GtkWidget* ptk_ui_xml_create_widget( XmlNode* tree )
{
    GSList* l;
    GtkWidget* widget;

    if( !tree->children )
        return NULL;

    tree = (XmlNode*)tree->children->data;

    if( !tree->children )
        return NULL;

    if( !tree->name || strcmp(tree->name, "glade-interface") )
        return NULL;

    if( G_LIKELY( ! this_mod ) )    /* load our own module for symbol lookup if it's not loaded */
        this_mod = g_module_open( NULL, 0 );

    widget = NULL;
    for( l = tree->children; l; l = l->next )
    {
        XmlNode* node = (XmlNode*)l->data;
        if( node->name && 0 == strcmp("widget", node->name) )
        {
            PtkUIXml* xml = ptk_ui_xml_new();
            widget = create_widget( NULL, node, NULL, xml );

            if( G_LIKELY(widget) )
            {
                ptk_ui_xml_ref( xml );
                g_object_set_qdata_full( (GObject*)widget, ptk_ui_xml_id,
                                     xml, (GDestroyNotify)ptk_ui_xml_unref );
                if( xml->def )
                    gtk_widget_grab_default( xml->def );
                if( xml->focus )
                    gtk_widget_grab_focus( xml->focus );
            }
            ptk_ui_xml_unref( xml );
        }
    }

    /* Unload the module in idle handler. So if several widgets need to be created,
        at the same time, we don't have to load/unload modules repeatedly. */
    g_idle_add_full( G_PRIORITY_LOW, (GSourceFunc)delayed_unload_module, NULL, NULL );

    return widget;
}

GtkWidget* ptk_ui_xml_create_widget_from_file( const char* file )
{
    XmlNode *tree;
    GtkWidget* widget = NULL;

    tree = xml_tree_load( file );
    if( G_LIKELY(tree) )
    {
        widget = ptk_ui_xml_create_widget( tree );
        xml_tree_free( tree );
    }
    return widget;
}

PtkUIXml* ptk_ui_xml_new()
{
    PtkUIXml* xml = g_slice_new0( PtkUIXml );
    xml->hash = g_hash_table_new_full( g_str_hash,
                                       g_str_equal,
                                       g_free, NULL );
    xml->n_ref = 1;

    if( G_UNLIKELY( ptk_ui_xml_id == 0) )
        ptk_ui_xml_id = g_quark_from_static_string("ptk_ui_xml_id");
    return xml;
}

void ptk_ui_xml_free( PtkUIXml* xml )
{
    g_hash_table_destroy( xml->hash );
    if( xml->tips )
        g_object_unref( xml->tips );
    g_slice_free( PtkUIXml, xml );
    /* g_debug("ptk_ui_XML_FREE"); */
}

PtkUIXml* ptk_ui_xml_get( GtkWidget* widget )
{
    return (PtkUIXml*)g_object_get_qdata( (GObject*)widget,
                                          ptk_ui_xml_id );
}

void ptk_ui_xml_ref( PtkUIXml* xml )
{
    g_atomic_int_inc( (gint *) &xml->n_ref );
}

void ptk_ui_xml_unref( PtkUIXml* xml )
{
    if( g_atomic_int_dec_and_test( (gint *) &xml->n_ref) )
        ptk_ui_xml_free( xml );
}

GtkWidget* ptk_ui_xml_lookup( PtkUIXml* xml,
                              const char* name )
{
    return GTK_WIDGET( g_hash_table_lookup( xml->hash, name ) );
}

GtkWidget* ptk_ui_xml_get_widget( GtkWidget* top_widget,
                                     const char* name )
{
    PtkUIXml* xml = ptk_ui_xml_get( top_widget );
    if( !xml )
        return NULL;
    return GTK_WIDGET( g_hash_table_lookup( xml->hash, name ) );
}

