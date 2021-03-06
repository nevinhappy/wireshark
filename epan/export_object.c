/* export_object.c
 * GUI independent helper routines common to all export object taps.
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>

#include "proto.h"
#include "packet_info.h"
#include "export_object.h"

struct register_eo {
    int proto_id;                        /* protocol id (0-indexed) */
    const char* tap_listen_str;          /* string used in register_tap_listener (NULL to use protocol name) */
    tap_packet_cb eo_func;               /* function to be called for new incoming packets for SRT */
    export_object_gui_reset_cb reset_cb; /* function to parse parameters of optional arguments of tap string */
};

static GSList *registered_eo_tables = NULL;

static gint
insert_sorted_by_table_name(gconstpointer aparam, gconstpointer bparam)
{
    const register_eo_t *a = (const register_eo_t *)aparam;
    const register_eo_t *b = (const register_eo_t *)bparam;

    return g_ascii_strcasecmp(proto_get_protocol_filter_name(a->proto_id), proto_get_protocol_filter_name(b->proto_id));
}

int
register_export_object(const int proto_id, tap_packet_cb export_packet_func, export_object_gui_reset_cb reset_cb)
{
    register_eo_t *table;
    DISSECTOR_ASSERT(export_packet_func);

    table = g_new(register_eo_t,1);

    table->proto_id      = proto_id;
    table->tap_listen_str = g_strdup_printf("%s_eo", proto_get_protocol_filter_name(proto_id));
    table->eo_func = export_packet_func;
    table->reset_cb = reset_cb;

    registered_eo_tables = g_slist_insert_sorted(registered_eo_tables, table, insert_sorted_by_table_name);
    return register_tap(table->tap_listen_str);
}

int get_eo_proto_id(register_eo_t* eo)
{
    if (!eo) {
        return -1;
    }
    return eo->proto_id;
}

const char* get_eo_tap_listener_name(register_eo_t* eo)
{
    return eo->tap_listen_str;
}

tap_packet_cb get_eo_packet_func(register_eo_t* eo)
{
    return eo->eo_func;
}

export_object_gui_reset_cb get_eo_reset_func(register_eo_t* eo)
{
    return eo->reset_cb;
}

register_eo_t* get_eo_by_name(const char* name)
{
    guint i, size = g_slist_length(registered_eo_tables);
    register_eo_t* eo;
    GSList   *slist;

    for (i = 0; i < size; i++) {
        slist = g_slist_nth(registered_eo_tables, i);
        eo = (register_eo_t*)slist->data;

        if (strcmp(name, proto_get_protocol_filter_name(eo->proto_id)) == 0)
            return eo;
    }

    return NULL;
}

void eo_iterate_tables(GFunc func, gpointer user_data)
{
    g_slist_foreach(registered_eo_tables, func, user_data);
}

static GString *eo_rename(GString *gstr, int dupn)
{
    GString *gstr_tmp;
    gchar *tmp_ptr;
    GString *ext_str;

    gstr_tmp = g_string_new("(");
    g_string_append_printf (gstr_tmp, "%d)", dupn);
    if ( (tmp_ptr = strrchr(gstr->str, '.')) != NULL ) {
        /* Retain the extension */
        ext_str = g_string_new(tmp_ptr);
        gstr = g_string_truncate(gstr, gstr->len - ext_str->len);
        if ( gstr->len >= (EXPORT_OBJECT_MAXFILELEN - (strlen(gstr_tmp->str) + ext_str->len)) )
            gstr = g_string_truncate(gstr, EXPORT_OBJECT_MAXFILELEN - (strlen(gstr_tmp->str) + ext_str->len));
        gstr = g_string_append(gstr, gstr_tmp->str);
        gstr = g_string_append(gstr, ext_str->str);
        g_string_free(ext_str, TRUE);
    }
    else {
        if ( gstr->len >= (EXPORT_OBJECT_MAXFILELEN - strlen(gstr_tmp->str)) )
            gstr = g_string_truncate(gstr, EXPORT_OBJECT_MAXFILELEN - strlen(gstr_tmp->str));
        gstr = g_string_append(gstr, gstr_tmp->str);
    }
    g_string_free(gstr_tmp, TRUE);
    return gstr;
}

GString *
eo_massage_str(const gchar *in_str, gsize maxlen, int dupn)
{
    gchar *tmp_ptr;
    /* The characters in "reject" come from:
     * http://msdn.microsoft.com/en-us/library/aa365247%28VS.85%29.aspx.
     * Add to the list as necessary for other OS's.
     */
    const gchar *reject = "<>:\"/\\|?*"
        "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
    "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
    "\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";
    GString *out_str;
    GString *ext_str;

    out_str = g_string_new("");

    /* Find all disallowed characters/bytes and replace them with %xx */
    while ( (tmp_ptr = strpbrk(in_str, reject)) != NULL ) {
        out_str = g_string_append_len(out_str, in_str, tmp_ptr - in_str);
        g_string_append_printf(out_str, "%%%02x", *tmp_ptr);
        in_str = tmp_ptr + 1;
    }
    out_str = g_string_append(out_str, in_str);
    if ( out_str->len > maxlen ) {
        if ( (tmp_ptr = strrchr(out_str->str, '.')) != NULL ) {
            /* Retain the extension */
            ext_str = g_string_new(tmp_ptr);
            out_str = g_string_truncate(out_str, maxlen - ext_str->len);
            out_str = g_string_append(out_str, ext_str->str);
            g_string_free(ext_str, TRUE);
        }
        else
            out_str = g_string_truncate(out_str, maxlen);
    }
    if ( dupn != 0 )
        out_str = eo_rename(out_str, dupn);
    return out_str;
}

const char *
eo_ct2ext(const char *content_type)
{
    /* TODO: Map the content type string to an extension string.  If no match,
     * return NULL. */
    return content_type;
}

void eo_free_entry(export_object_entry_t *entry)
{
    g_free(entry->hostname);
    g_free(entry->content_type);
    g_free(entry->filename);
    g_free(entry->payload_data);

    g_free(entry);
}
/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
