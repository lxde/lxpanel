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

#ifndef _PTK_XML_TREE_H_
#define _PTK_XML_TREE_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct _XmlNode XmlNode;
struct _XmlNode
{
    char* name;
    char** props;
    char** vals;
    char* cdata;
    XmlNode* parent;
    GSList* children;
};

XmlNode* xml_tree_load( const char* filename );
/*
XmlNode* xml_node_get_from_path( XmlNode* tree, const char* path );
GSList* xml_node_get_elements_by_name( XmlNode* node, const char* tag_name );
*/

void xml_node_free( XmlNode* tree );
#define xml_tree_free( tree )   xml_node_free( tree )
const char* xml_node_get_tag_name( XmlNode* node );
const char* xml_node_get_prop( XmlNode* node, const char* name );

G_END_DECLS

#endif
