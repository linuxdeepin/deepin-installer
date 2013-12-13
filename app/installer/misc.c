/**
 * Copyright (c) 2011 ~ 2013 Deepin, Inc.
 *               2011 ~ 2013 Long Wei
 *
 * Author:      Long Wei <yilang2007lw@gmail.com>
 * Maintainer:  Long Wei <yilang2007lw@gamil.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 **/

#include "misc.h"
#include "fs_util.h"

#define WHITE_LIST_PATH RESOURCE_DIR"/installer/whitelist.ini"

static GList *timezone_list = NULL;
static GList *filelist = NULL;

extern int chroot (const char *path);

void installer_reboot ()
{
    GError *error = NULL;
    GDBusProxy *ck_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
            G_DBUS_PROXY_FLAGS_NONE,
            NULL,
            "org.freedesktop.ConsoleKit",
            "/org/freedesktop/ConsoleKit/Manager",
            "org.freedesktop.ConsoleKit.Manager",
            NULL,
            &error);

    g_assert (ck_proxy != NULL);
    if (error != NULL) {
        g_warning ("installer reboot: ck proxy %s\n", error->message);
        g_error_free (error);
    }
    error = NULL;

    GVariant *can_restart_var = g_dbus_proxy_call_sync (ck_proxy,
                                "CanRestart",
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &error);

    g_assert (can_restart_var != NULL);
    if (error != NULL) {
        g_warning ("installer reboot: CanRestart %s\n", error->message);
        g_error_free (error);
    }
    error = NULL;

    gboolean can_restart = FALSE;
    g_variant_get (can_restart_var, "(b)", &can_restart);
    g_variant_unref (can_restart_var);
    if (can_restart) {
        g_dbus_proxy_call (ck_proxy,
                           "Restart",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           NULL,
                           NULL);
    } else {
        g_warning ("installer reboot: can restart is false\n");
    }

    g_object_unref (ck_proxy);
}

JS_EXPORT_API
JSObjectRef installer_get_timezone_list ()
{
    JSObjectRef timezones = json_array_create ();
    gsize index = 0;
    GError *error = NULL;

    GFile *file = g_file_new_for_path ("/usr/share/zoneinfo/zone.tab");
    if (!g_file_query_exists (file, NULL)) {
        g_warning ("get timezone list:zone.tab not exists\n");
        return timezones;
    }

    GFileInputStream *input = g_file_read (file, NULL, &error);
    if (error != NULL){
        g_warning ("get timezone list:read zone.tab error->%s", error->message);
        g_error_free (error);
        g_object_unref (file);
        return timezones;
    }
    error = NULL;

    GDataInputStream *data_input = g_data_input_stream_new ((GInputStream *) input);
    if (data_input == NULL) {
        g_warning ("get timezone list:get data input stream failed\n");
        g_object_unref (input);
        return timezones;
    }
    
    char *data = (char *) 1;
    while (data) {
        gsize length = 0;
        data = g_data_input_stream_read_line (data_input, &length, NULL, &error);
        if (error != NULL) {
            g_warning ("get timezone list:read line error");
            g_error_free (error);
            continue;
        }
        error = NULL;
        if (data != NULL) {
            if (g_str_has_prefix (data, "#")){
                g_debug ("get timezone list:comment line, just pass");
                continue;
            } else {
                gchar **line = g_strsplit (data, "\t", -1);
                if (line == NULL) {
                    g_warning ("get timezone list:split %s failed\n", data);
                } else {
                    json_array_insert (timezones, index, jsvalue_from_cstr (get_global_context (), line[2]));
                    index++;
                }
                g_strfreev (line);
            }
        } else {
            break;
        }
    }

    g_object_unref (data_input);
    g_object_unref (input);
    g_object_unref (file);

    return timezones;
}

static gpointer
thread_set_timezone (gpointer data)
{
    gchar *timezone = (gchar *) data;
    gboolean ret = FALSE;
    GError *error = NULL;
    gchar *timezone_file = NULL;
    gchar *zoneinfo_path = NULL;
    gchar *localtime_path = NULL;
    GFile *zoneinfo_file = NULL;
    GFile *localtime_file = NULL;

    if (timezone == NULL) {
        g_warning ("set timezone:timezone NULL\n");
        goto out;
    }
    timezone_file = g_strdup ("/etc/timezone");
    g_file_set_contents (timezone_file, timezone, -1, &error);
    if (error != NULL) {
        g_warning ("set timezone:write timezone %s\n", error->message);
        goto out;
    }
    zoneinfo_path = g_strdup_printf ("/usr/share/zoneinfo/%s", timezone);
    localtime_path = g_strdup ("/etc/localtime");
    zoneinfo_file = g_file_new_for_path (zoneinfo_path);
    localtime_file = g_file_new_for_path (localtime_path);
    g_file_copy (zoneinfo_file, localtime_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
    if (error != NULL) {
        g_warning ("set timezone:cp %s to /etc/localtime %s\n", zoneinfo_path, error->message);
        goto out;
    }
    ret = TRUE;
    goto out;

out:
    g_free (timezone);
    g_free (timezone_file);
    g_free (zoneinfo_path);
    g_free (localtime_path);
    if (error != NULL) {
        g_error_free (error);
    }
    if (zoneinfo_file != NULL) {
        g_object_unref (zoneinfo_file);
    }
    if (localtime_file != NULL) {
        g_object_unref (localtime_file);
    }
    if (ret) {
        emit_progress ("timezone", "finish");
    } else {
        g_warning ("set timezone failed, just skip this step");
        emit_progress ("timezone", "finish");
    }
    return NULL;
}

JS_EXPORT_API 
void installer_set_timezone (const gchar *timezone)
{
    GThread *thread = g_thread_new ("timezone", (GThreadFunc) thread_set_timezone, g_strdup (timezone));
    g_thread_unref (thread);
}

JS_EXPORT_API
gboolean installer_mount_procfs ()
{
    gboolean ret = FALSE;
    GError *error = NULL;
    extern const gchar* target;
    gchar *dev_target = NULL;
    gchar *devpts_target = NULL;
    gchar *proc_target = NULL;
    gchar *sys_target = NULL;
    gchar *mount_dev = NULL;
    gchar *mount_devpts = NULL;
    gchar *mount_proc = NULL;
    gchar *mount_sys = NULL;

    if (target == NULL) {
        g_warning ("mount procfs:target is NULL\n");
        goto out;
    }
    dev_target = g_strdup_printf ("%s/dev", target);
    devpts_target = g_strdup_printf ("%s/dev/pts", target);
    proc_target = g_strdup_printf ("%s/proc", target);
    sys_target = g_strdup_printf ("%s/sys", target);

    mount_dev = g_strdup_printf ("mount -v --bind /dev %s/dev", target);
    mount_devpts = g_strdup_printf ("mount -vt devpts devpts %s/dev/pts", target);
    mount_proc = g_strdup_printf ("mount -vt proc proc %s/proc", target);
    mount_sys = g_strdup_printf ("mount -vt sysfs sysfs %s/sys", target);

    guint dev_before = get_mount_target_count (dev_target);
    g_spawn_command_line_sync (mount_dev, NULL, NULL, NULL, &error);
    if (error != NULL) {
        g_warning ("mount procfs:mount dev %s\n", error->message);
        goto out;
    }
    guint dev_after = get_mount_target_count (dev_target);
    if (dev_after != dev_before + 1) {
        g_warning ("mount procfs:mount dev not changed\n");
        goto out;
    }
    
    guint devpts_before = get_mount_target_count (devpts_target);
    g_spawn_command_line_sync (mount_devpts, NULL, NULL, NULL, &error);
    if (error != NULL) {
        g_warning ("mount procfs:mount devpts %s\n", error->message);
        goto out;
    }
    guint devpts_after  = get_mount_target_count (devpts_target);
    if (devpts_after != devpts_before + 1) {
        g_warning ("mount procfs:mount devpts not changed\n");
        goto out;
    }

    guint proc_before = get_mount_target_count (proc_target);
    g_spawn_command_line_sync (mount_proc, NULL, NULL, NULL, &error);
    if (error != NULL) {
        g_warning ("mount procfs:mount proc %s\n", error->message);
        goto out;
    }
    guint proc_after = get_mount_target_count (proc_target);
    if (proc_after != proc_before + 1) {
        g_warning ("mount procfs:mount proc not changed\n");
        goto out;
    }

    guint sys_before = get_mount_target_count (sys_target);
    g_spawn_command_line_sync (mount_sys, NULL, NULL, NULL, &error);
    if (error != NULL) {
        g_warning ("mount procfs:mount sys %s\n", error->message);
        goto out;
    }
    guint sys_after = get_mount_target_count (sys_target);
    if (sys_after != sys_before + 1) {
        g_warning ("mount procfs:mount sys not changed\n");
        goto out;
    }
    ret = TRUE;
    goto out;

out:
    g_free (dev_target);
    g_free (devpts_target);
    g_free (proc_target);
    g_free (sys_target);

    g_free (mount_dev);
    g_free (mount_devpts);
    g_free (mount_proc);
    g_free (mount_sys);
    if (error != NULL) {
        g_error_free (error);
    }
    return ret;
}

JS_EXPORT_API
gboolean installer_chroot_target ()
{
    gboolean ret = FALSE;

    extern const gchar* target;
    if (target == NULL) {
        g_warning ("chroot:target is NULL\n");
        return ret;
    }
    extern int chroot_fd;
    if ((chroot_fd = open (".", O_RDONLY)) < 0) {
        g_warning ("chroot:set chroot fd failed\n");
        return ret;
    }

    extern gboolean in_chroot;
    if (chroot (target) == 0) {
        in_chroot = TRUE;
        ret = TRUE;
    } else {
        g_warning ("chroot:chroot to %s falied:%s\n", target, strerror (errno));
    }

    emit_progress ("chroot", "finish");
    return ret;
}

//copy whitelist file just after extract and before chroot
JS_EXPORT_API 
void installer_copy_whitelist ()
{
    GError *error = NULL;
    gchar *cmd = NULL;
    gchar *contents = NULL;
    gchar **strarray = NULL;

    extern const gchar *target;
    if (target == NULL) {
        g_warning ("copy whitelist:target NULL\n");
        goto out;
    }

    if (!g_file_test (WHITE_LIST_PATH, G_FILE_TEST_EXISTS)) {
        g_warning ("copy whitelist:%s not exists\n", WHITE_LIST_PATH);
        goto out;
    }
    g_file_get_contents (WHITE_LIST_PATH, &contents, NULL, &error);
    if (error != NULL) {
        g_warning ("copy whitelist:get packages list %s\n", error->message);
        g_error_free (error);
        goto out;
    }
    if (contents == NULL) {
        g_warning ("copy whitelist:contents NULL\n");
        goto out;
    }
    strarray = g_strsplit (contents, "\n", -1);
    if (strarray == NULL) {
       g_warning ("copy whitelist:strarray NULL\n"); 
       goto out;
    }
    guint count = g_strv_length (strarray);
    g_printf ("copy whitelist:file count %d\n", count);
    guint index = 0;
    for (index = 0; index < count; index++) {
        gchar *item = g_strdup (strarray[index]);
        g_printf ("copy whitelist:start copy file %s\n", item);
        if (!g_file_test (item, G_FILE_TEST_EXISTS)) {
            g_warning ("copy whitelist:file %s not exists\n", item);
            g_free (item);
            continue;
        }
        GFile *src = g_file_new_for_path (item);
        gchar *dest_path = g_strdup_printf ("%s%s", target, item);
        GFile *dest = g_file_new_for_path (dest_path);

        g_file_copy (src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);

        g_free (item);
        g_free (dest_path);
        g_object_unref (src);
        g_object_unref (dest);
        if (error != NULL) {
            g_warning ("copy whiltelist:file %s error->%s\n", item, error->message);
            g_error_free (error);
        }
    }
    goto out;

out:
    g_free (cmd);
    g_free (contents);
    g_strfreev (strarray);
    if (error != NULL) {
        g_error_free (error);
    }
}

JS_EXPORT_API 
double installer_get_memory_size ()
{
    struct sysinfo info;
    if (sysinfo (&info) != 0) {
        g_warning ("get memory size:%s\n", strerror (errno));
        return 0;
    }

    return info.totalram;
}

void emit_progress (const gchar *step, const gchar *progress)
{
    js_post_message_simply ("progress", "{\"stage\":\"%s\",\"progress\":\"%s\"}", step, progress);
}

gchar *
get_matched_string (const gchar *target, const gchar *regex_string) 
{
    gchar *result = NULL;
    GError *error = NULL;
    GRegex *regex;
    GMatchInfo *match_info;

    if (target == NULL || regex_string == NULL) {
        g_warning ("get matched string:paramemter NULL\n");
        return NULL;
    }

    regex = g_regex_new (regex_string, 0, 0, &error);
    if (error != NULL) {
        g_warning ("get matched string:%s\n", error->message);
        g_error_free (error);
        return NULL;
    }
    error = NULL;

    g_regex_match (regex, target, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        result = g_match_info_fetch (match_info, 0);

    } else {
        g_warning ("get matched string failed!\n");
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);

    return result;
}
