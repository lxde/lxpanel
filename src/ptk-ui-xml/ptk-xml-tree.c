/*
 *      ptk-xml-tree.h - Over-simplified mini xml dom implementation
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

#include <string.h>
#include "ptk-xml-tree.h"
#include "glib-mem.h"

typedef struct _ParseInfo ParseInfo;

struct _ParseInfo
{
    XmlNode* tree;
    GArray* parse_stack;
};

#define xml_node_new()    g_slice_new0(XmlNode);

static void start_element( GMarkupParseContext *context,
                    const gchar *element_name,
                    const gchar **attribute_names,
                    const gchar **attribute_values,
                    gpointer user_data,
                    GError **error )
{
    ParseInfo* info = (ParseInfo*)user_data;
    XmlNode *cur_node, *parent;
    if( info->parse_stack->len > 0 ) {
        parent = g_array_index( info->parse_stack,
                                XmlNode*,
                                info->parse_stack->len - 1 );
    } else {
        parent = NULL;
    }
    cur_node = xml_node_new();
    cur_node->name = g_strdup( element_name );
    cur_node->props = g_strdupv( (gchar**)attribute_names );
    cur_node->vals = g_strdupv( (gchar**)attribute_values );
    if( parent )
    {
        cur_node->parent = parent;
        parent->children = g_slist_prepend( parent->children, cur_node );
    }
    /* push current node to the stack */
    g_array_append_val( info->parse_stack, cur_node );
}

static void end_element( GMarkupParseContext *context,
                  const gchar *element_name,
                  gpointer user_data,
                  GError **error )
{
    ParseInfo* info = (ParseInfo*)user_data;
    int i = info->parse_stack->len - 1;
    XmlNode* cur_node = g_array_index( info->parse_stack,
                                       XmlNode*, i );

    if( cur_node->cdata )
    {
        GString* str = (GString*)cur_node->cdata;
        cur_node->cdata = g_strdup( str->str );
        g_string_free( str, TRUE );
    }

    if( cur_node->children )
        cur_node->children = g_slist_reverse( cur_node->children );

    /* begin tag is different from end tag, error! */
    if( strcmp( cur_node->name, element_name ) )
    {
        g_markup_parse_context_end_parse( context, NULL );
        return;
    }

    /* pop current node from the stack */
    g_array_remove_index( info->parse_stack, i );
}

static void cdata( GMarkupParseContext *context,
            const gchar *text,
            gsize text_len,
            gpointer user_data,
            GError **error )
{
    ParseInfo* info = (ParseInfo*)user_data;
    GString* str;
    XmlNode* cur_node = g_array_index( info->parse_stack,
                                       XmlNode*,
                                       info->parse_stack->len - 1 );
    if( !cur_node->cdata )
    {
        str = g_string_sized_new(256);
        cur_node->cdata = (char*)str;
    }
    else
        str = (GString*)cur_node->cdata;
    g_string_append_len( str, text, text_len );
}

static void parse_error( GMarkupParseContext *context,
                  GError *error, gpointer user_data )
{
    ParseInfo* info = (ParseInfo*)user_data;
    XmlNode* cur_node = g_array_index( info->parse_stack,
                                       XmlNode*,
                                       info->parse_stack->len - 1 );
    if( cur_node->cdata )
    {
        GString* str = (GString*)cur_node->cdata;
        g_string_free( str, TRUE );
        cur_node->cdata = NULL;
    }
    g_markup_parse_context_end_parse( context, NULL );
}

static GMarkupParser parser =
    {
        start_element,
        end_element,
        cdata,
        NULL,
        parse_error
    };

XmlNode* xml_tree_load( const char* filename )
{
    gsize data_len;
    char* data;

    if ( g_file_get_contents( filename, &data, &data_len, NULL ) )
    {
        GMarkupParseContext * ctx;
        ParseInfo info;
        info.tree = xml_node_new();
        info.parse_stack = g_array_sized_new(FALSE, TRUE, sizeof(XmlNode*), 32);
        g_array_append_val( info.parse_stack, info.tree );
        ctx = g_markup_parse_context_new( &parser,
                                          (GMarkupParseFlags)0,
                                          &info, NULL );
        if( !g_markup_parse_context_parse( ctx, data, data_len, NULL ) )
        {
            xml_tree_free( info.tree );
            info.tree = NULL;
        }
        g_markup_parse_context_free( ctx );
        g_array_free( info.parse_stack, TRUE );
        return info.tree;
    }
    return NULL;
}

void xml_node_free( XmlNode* node )
{
    GSList* l;
    XmlNode* child;

    if( node->children )
    {
        for( l = node->children; l; l = l->next )
        {
            child = (XmlNode*)l->data;
            /* break the linkage between parent and child,
                or subsequent recursive call to xml_node_free on
                the child node will try to remove it from the parent again. */
            child->parent = NULL;
            xml_node_free( child );
        }
        g_slist_free( node->children );
    }

    /* delete the child node from its parent, ifneeded */
    if( G_LIKELY( node->parent && node->parent->children) )
        node->parent->children = g_slist_remove( node->parent->children, node );

    g_free( node->name );
    g_free( node->cdata );
    g_strfreev( node->props );
    g_strfreev( node->vals );
    g_slice_free( XmlNode, node );
}

const char* xml_node_get_prop( XmlNode* node, const char* name )
{
    char **prop, **val;
    for( prop = node->props, val = node->vals; *prop; ++prop, ++val )
    {
        if( *prop && 0 == strcmp(name, *prop) )
            return *val;
    }
    return NULL;
}
