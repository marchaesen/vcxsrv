/*
 * Copyright © 2006-2007 Daniel Stone
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Daniel Stone <daniel@fooishbar.org>
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <dbus/dbus.h>
#include <string.h>

#include <X11/X.h>

#include "config-backends.h"
#include "opaque.h"             /* for 'display': there should be a better way. */
#include "input.h"
#include "inputstr.h"

#define API_VERSION 2

#define MATCH_RULE "type='method_call',interface='org.x.config.input'"

#define MALFORMED_MSG "[config/dbus] malformed message, dropping"
#define MALFORMED_MESSAGE() { DebugF(MALFORMED_MSG "\n"); \
                            ret = BadValue; \
                            goto unwind; }
#define MALFORMED_MESSAGE_ERROR() { DebugF(MALFORMED_MSG ": %s, %s", \
                                       error->name, error->message); \
                                  ret = BadValue; \
                                  goto unwind; }

struct connection_info {
    char busobject[32];
    char busname[64];
    DBusConnection *connection;
};

static void
reset_info(struct connection_info *info)
{
    info->connection = NULL;
    info->busname[0] = '\0';
    info->busobject[0] = '\0';
}

static int
add_device(DBusMessage * message, DBusMessage * reply, DBusError * error)
{
    DBusMessageIter iter, reply_iter, subiter;
    InputOption *input_options = NULL;
    int ret, err;
    DeviceIntPtr dev = NULL;

    dbus_message_iter_init_append(reply, &reply_iter);

    if (!dbus_message_iter_init(message, &iter)) {
        ErrorF("[config/dbus] couldn't initialise iterator\n");
        MALFORMED_MESSAGE();
    }

    input_options = input_option_new(input_options, "_source", "client/dbus");
    if (!input_options) {
        ErrorF("[config/dbus] couldn't allocate first key/value pair\n");
        ret = BadAlloc;
        goto unwind;
    }

    /* signature should be [ss][ss]... */
    while (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        char *key, *value;

        dbus_message_iter_recurse(&iter, &subiter);

        if (dbus_message_iter_get_arg_type(&subiter) != DBUS_TYPE_STRING)
            MALFORMED_MESSAGE();

        dbus_message_iter_get_basic(&subiter, &key);
        if (!key)
            MALFORMED_MESSAGE();
        /* The _ prefix refers to internal settings, and may not be given by
         * the client. */
        if (key[0] == '_') {
            ErrorF("[config/dbus] attempted subterfuge: option name %s given\n",
                   key);
            MALFORMED_MESSAGE();
        }

        if (!dbus_message_iter_has_next(&subiter))
            MALFORMED_MESSAGE();
        dbus_message_iter_next(&subiter);
        if (dbus_message_iter_get_arg_type(&subiter) != DBUS_TYPE_STRING)
            MALFORMED_MESSAGE();

        dbus_message_iter_get_basic(&subiter, &value);
        if (!value)
            MALFORMED_MESSAGE();

        input_options = input_option_new(input_options, key, value);

        dbus_message_iter_next(&iter);
    }

    ret = NewInputDeviceRequest(input_options, NULL, &dev);
    if (ret != Success) {
        DebugF("[config/dbus] NewInputDeviceRequest failed\n");
        goto unwind;
    }

    if (!dev) {
        DebugF("[config/dbus] NewInputDeviceRequest provided no device\n");
        ret = BadImplementation;
        goto unwind;
    }

    /* XXX: If we fail halfway through, we don't seem to have any way to
     *      empty the iterator, so you'll end up with some device IDs,
     *      plus an error.  This seems to be a shortcoming in the D-Bus
     *      API. */
    for (; dev; dev = dev->next) {
        if (!dbus_message_iter_append_basic(&reply_iter, DBUS_TYPE_INT32,
                                            &dev->id)) {
            ErrorF("[config/dbus] couldn't append to iterator\n");
            ret = BadAlloc;
            goto unwind;
        }
    }

 unwind:
    if (ret != Success) {
        if (dev)
            RemoveDevice(dev, TRUE);

        err = -ret;
        dbus_message_iter_append_basic(&reply_iter, DBUS_TYPE_INT32, &err);
    }

    input_option_free_list(&input_options);

    return ret;
}

static int
remove_device(DBusMessage * message, DBusMessage * reply, DBusError * error)
{
    int deviceid, ret, err;
    DeviceIntPtr dev;
    DBusMessageIter iter, reply_iter;

    dbus_message_iter_init_append(reply, &reply_iter);

    if (!dbus_message_iter_init(message, &iter)) {
        ErrorF("[config/dbus] failed to init iterator\n");
        MALFORMED_MESSAGE();
    }

    if (!dbus_message_get_args(message, error, DBUS_TYPE_UINT32,
                               &deviceid, DBUS_TYPE_INVALID)) {
        MALFORMED_MESSAGE_ERROR();
    }

    dixLookupDevice(&dev, deviceid, serverClient, DixDestroyAccess);
    if (!dev) {
        DebugF("[config/dbus] bogus device id %d given\n", deviceid);
        ret = BadMatch;
        goto unwind;
    }

    DebugF("[config/dbus] removing device %s (id %d)\n", dev->name, deviceid);

    /* Call PIE here so we don't try to dereference a device that's
     * already been removed. */
    OsBlockSignals();
    ProcessInputEvents();
    DeleteInputDeviceRequest(dev);
    OsReleaseSignals();

    ret = Success;

 unwind:
    err = (ret == Success) ? ret : -ret;
    dbus_message_iter_append_basic(&reply_iter, DBUS_TYPE_INT32, &err);

    return ret;
}

static int
list_devices(DBusMessage * message, DBusMessage * reply, DBusError * error)
{
    DeviceIntPtr dev;
    DBusMessageIter iter, subiter;

    dbus_message_iter_init_append(reply, &iter);

    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
                                              &subiter)) {
            ErrorF("[config/dbus] couldn't init container\n");
            return BadAlloc;
        }
        if (!dbus_message_iter_append_basic(&subiter, DBUS_TYPE_UINT32,
                                            &dev->id)) {
            ErrorF("[config/dbus] couldn't append to iterator\n");
            return BadAlloc;
        }
        if (!dbus_message_iter_append_basic(&subiter, DBUS_TYPE_STRING,
                                            &dev->name)) {
            ErrorF("[config/dbus] couldn't append to iterator\n");
            return BadAlloc;
        }
        if (!dbus_message_iter_close_container(&iter, &subiter)) {
            ErrorF("[config/dbus] couldn't close container\n");
            return BadAlloc;
        }
    }

    return Success;
}

static int
get_version(DBusMessage * message, DBusMessage * reply, DBusError * error)
{
    DBusMessageIter iter;
    unsigned int version = API_VERSION;

    dbus_message_iter_init_append(reply, &iter);
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &version)) {
        ErrorF("[config/dbus] couldn't append version\n");
        return BadAlloc;
    }

    return Success;
}

static DBusHandlerResult
message_handler(DBusConnection * connection, DBusMessage * message, void *data)
{
    DBusError error;
    DBusMessage *reply;
    struct connection_info *info = data;

    /* ret is the overall D-Bus handler result, whereas err is the internal
     * X error from our individual functions. */
    int ret = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    int err;

    DebugF("[config/dbus] received a message for %s\n",
           dbus_message_get_interface(message));

    dbus_error_init(&error);

    reply = dbus_message_new_method_return(message);
    if (!reply) {
        ErrorF("[config/dbus] failed to create reply\n");
        ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
        goto err_start;
    }

    if (strcmp(dbus_message_get_member(message), "add") == 0)
        err = add_device(message, reply, &error);
    else if (strcmp(dbus_message_get_member(message), "remove") == 0)
        err = remove_device(message, reply, &error);
    else if (strcmp(dbus_message_get_member(message), "listDevices") == 0)
        err = list_devices(message, reply, &error);
    else if (strcmp(dbus_message_get_member(message), "version") == 0)
        err = get_version(message, reply, &error);
    else
        goto err_reply;

    /* Failure to allocate is a special case. */
    if (err == BadAlloc) {
        ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
        goto err_reply;
    }

    /* While failure here is always an OOM, we don't return that,
     * since that would result in devices being double-added/removed. */
    if (dbus_connection_send(info->connection, reply, NULL))
        dbus_connection_flush(info->connection);
    else
        ErrorF("[config/dbus] failed to send reply\n");

    ret = DBUS_HANDLER_RESULT_HANDLED;

 err_reply:
    dbus_message_unref(reply);
 err_start:
    dbus_error_free(&error);

    return ret;
}

static void
connect_hook(DBusConnection * connection, void *data)
{
    DBusError error;
    DBusObjectPathVTable vtable = {.message_function = message_handler, };
    struct connection_info *info = data;

    info->connection = connection;

    dbus_error_init(&error);

    dbus_bus_request_name(info->connection, info->busname, 0, &error);
    if (dbus_error_is_set(&error)) {
        ErrorF("[config/dbus] couldn't take over org.x.config: %s (%s)\n",
               error.name, error.message);
        goto err_start;
    }

    /* blocks until we get a reply. */
    dbus_bus_add_match(info->connection, MATCH_RULE, &error);
    if (dbus_error_is_set(&error)) {
        ErrorF("[config/dbus] couldn't add match: %s (%s)\n", error.name,
               error.message);
        goto err_name;
    }

    if (!dbus_connection_register_object_path(info->connection,
                                              info->busobject, &vtable, info)) {
        ErrorF("[config/dbus] couldn't register object path\n");
        goto err_match;
    }

    DebugF("[dbus] registered %s, %s\n", info->busname, info->busobject);

    dbus_error_free(&error);

    return;

 err_match:
    dbus_bus_remove_match(info->connection, MATCH_RULE, &error);
 err_name:
    dbus_bus_release_name(info->connection, info->busname, &error);
 err_start:
    dbus_error_free(&error);

    reset_info(info);
}

static void
disconnect_hook(void *data)
{
}

#if 0
void
pre_disconnect_hook(void)
{
    DBusError error;

    dbus_error_init(&error);
    dbus_connection_unregister_object_path(connection_data->connection,
                                           connection_data->busobject);
    dbus_bus_remove_match(connection_data->connection, MATCH_RULE, &error);
    dbus_bus_release_name(connection_data->connection,
                          connection_data->busname, &error);
    dbus_error_free(&error);
}
#endif

static struct connection_info connection_data;

static struct config_dbus_core_hook core_hook = {
    .connect = connect_hook,
    .disconnect = disconnect_hook,
    .data = &connection_data,
};

int
config_dbus_init(void)
{
    snprintf(connection_data.busname, sizeof(connection_data.busname),
             "org.x.config.display%d", atoi(display));
    snprintf(connection_data.busobject, sizeof(connection_data.busobject),
             "/org/x/config/%d", atoi(display));

    return config_dbus_core_add_hook(&core_hook);
}

void
config_dbus_fini(void)
{
    config_dbus_core_remove_hook(&core_hook);
    connection_data.busname[0] = '\0';
    connection_data.busobject[0] = '\0';
}
