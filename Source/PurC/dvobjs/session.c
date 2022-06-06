/*
 * @file session.c
 * @author Xue Shuming
 * @date 2022/01/04
 * @brief The implementation of Session dynamic variant object.
 *
 * Copyright (C) 2021 FMSoft <https://www.fmsoft.cn>
 *
 * This file is a part of PurC (short for Purring Cat), an HVML interpreter.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "private/instance.h"
#include "private/errors.h"
#include "private/vdom.h"
#include "private/dvobjs.h"
#include "private/url.h"
#include "purc-variant.h"
#include "helper.h"

#include <limits.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define KN_USER_OBJ     "myobj"

static purc_variant_t
user_getter(purc_variant_t root, size_t nr_args, purc_variant_t *argv,
        bool silently)
{
    purc_variant_t myobj = purc_variant_object_get_by_ckey(root,
            KN_USER_OBJ);
    if (myobj == PURC_VARIANT_INVALID) {
        pcinst_set_error(PURC_ERROR_NOT_DESIRED_ENTITY);
        goto failed;
    }

    if (nr_args < 1) {
        return purc_variant_ref(myobj);
    }

    const char *keyname;
    keyname = purc_variant_get_string_const(argv[0]);
    if (keyname == NULL) {
        pcinst_set_error(PURC_ERROR_WRONG_DATA_TYPE);
        goto failed;
    }

    purc_variant_t var = purc_variant_object_get(myobj, argv[0]);
    if (var != PURC_VARIANT_INVALID) {
        return purc_variant_ref(var);
    }

failed:
    if (silently)
        return purc_variant_make_undefined();

    return PURC_VARIANT_INVALID;
}

static purc_variant_t
user_setter(purc_variant_t root, size_t nr_args, purc_variant_t *argv,
        bool silently)
{
    purc_variant_t myobj = purc_variant_object_get_by_ckey(root,
            KN_USER_OBJ);
    if (myobj == PURC_VARIANT_INVALID) {
        pcinst_set_error(PURC_ERROR_NOT_DESIRED_ENTITY);
        goto failed;
    }

    if (nr_args < 2) {
        pcinst_set_error(PURC_ERROR_ARGUMENT_MISSED);
        goto failed;
    }

    if (!purc_variant_is_string(argv[0])) {
        pcinst_set_error(PURC_ERROR_WRONG_DATA_TYPE);
        goto failed;
    }

    if (purc_variant_is_undefined(argv[1])) {
        if (!purc_variant_object_remove(myobj, argv[0], false))
            goto failed;
    }
    else {
        if (!purc_variant_object_set(myobj, argv[0], argv[1]))
            goto failed;
    }

    return purc_variant_make_boolean(true);

failed:
    if (silently)
        return purc_variant_make_boolean(false);

    return PURC_VARIANT_INVALID;
}


purc_variant_t
purc_dvobj_session_new(void)
{
    purc_variant_t retv = PURC_VARIANT_INVALID;

    static struct purc_dvobj_method method [] = {
        { "user",   user_getter,    user_setter },
    };

    retv = purc_dvobj_make_from_methods(method, PCA_TABLESIZE(method));
    if (retv == PURC_VARIANT_INVALID) {
        return PURC_VARIANT_INVALID;
    }

    purc_variant_t myobj = purc_variant_make_object(0,
            PURC_VARIANT_INVALID, PURC_VARIANT_INVALID);
    if (myobj == PURC_VARIANT_INVALID) {
        purc_variant_unref(retv);
        return PURC_VARIANT_INVALID;
    }

    // TODO: set a pre-listener to avoid remove the myobj property.
    purc_variant_object_set_by_static_ckey(retv, KN_USER_OBJ, myobj);
    purc_variant_unref(myobj);

    return retv;
}

