/*
 * @file instance.c
 * @author Vincent Wei (https://github.com/VincentWei)
 * @date 2021/07/02
 * @brief The instance of PurC.
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

#include "purc.h"

#include "config.h"

#include "private/instance.h"
#include "private/errors.h"
#include "private/tls.h"
#include "private/utils.h"
#include "private/ports.h"
#include "private/rwstream.h"
#include "private/ejson.h"
#include "private/html.h"
#include "private/vdom.h"
#include "private/dom.h"
#include "private/dvobjs.h"
#include "private/executor.h"
#include "private/atom-buckets.h"
#include "private/fetcher.h"
#include "private/pcrdr.h"
#include "purc-runloop.h"

#include <locale.h>
#if USE(PTHREADS)          /* { */
#include <pthread.h>
#endif                     /* } */
#include <stdio.h>  // fclose on inst->fp_log
#include <stdlib.h>
#include <string.h>
#include <time.h>


#include "generic_err_msgs.inc"

#define FETCHER_MAX_CONNS        100
#define FETCHER_CACHE_QUOTA      10240

static struct const_str_atom _except_names[] = {
    { "OK", 0 },
    { "BadEncoding", 0 },
    { "BadHVMLTag", 0 },
    { "BadHVMLAttrName", 0 },
    { "BadHVMLAttrValue", 0 },
    { "BadHVMLContent", 0 },
    { "BadTargetHTML", 0 },
    { "BadTargetXGML", 0 },
    { "BadTargetXML", 0 },
    { "BadExpression", 0 },
    { "BadExecutor", 0 },
    { "BadName", 0 },
    { "NoData", 0 },
    { "NotIterable", 0 },
    { "BadIndex", 0 },
    { "NoSuchKey", 0 },
    { "DuplicateKey", 0 },
    { "ArgumentMissed", 0 },
    { "WrongDataType", 0 },
    { "InvalidValue", 0 },
    { "MaxIterationCount", 0 },
    { "MaxRecursionDepth", 0 },
    { "Unauthorized", 0 },
    { "Timeout", 0 },
    { "eDOMFailure", 0 },
    { "LostRenderer", 0 },
    { "MemoryFailure", 0 },
    { "InternalFailure", 0 },
    { "ZeroDivision", 0 },
    { "Overflow", 0 },
    { "Underflow", 0 },
    { "InvalidFloat", 0 },
    { "AccessDenied", 0 },
    { "IOFailure", 0 },
    { "TooSmall", 0 },
    { "TooMany", 0 },
    { "TooLong", 0 },
    { "TooLarge", 0 },
    { "NotDesiredEntity", 0 },
    { "InvalidOperand", 0 },
    { "EntityNotFound", 0 },
    { "EntityExists", 0 },
    { "NoStorageSpace", 0 },
    { "BrokenPipe", 0 },
    { "ConnectionAborted", 0 },
    { "ConnectionRefused", 0 },
    { "ConnectionReset", 0 },
    { "NameResolutionFailed", 0 },
    { "RequestFailed", 0 },
    { "SystemFault", 0 },
    { "OSFailure", 0 },
    { "NotReady", 0 },
    { "NotImplemented", 0 },
    { "Unsupported", 0 },
    { "Incompleted", 0 },
    { "DuplicateName", 0 },
};

/* Make sure the number of error messages matches the number of error codes */
#define _COMPILE_TIME_ASSERT(name, x)               \
       typedef int _dummy_ ## name[(x) * 2 - 1]

_COMPILE_TIME_ASSERT(msgs,
        PCA_TABLESIZE(generic_err_msgs) == PURC_ERROR_NR);

_COMPILE_TIME_ASSERT(excepts,
        PCA_TABLESIZE(_except_names) == PURC_EXCEPT_NR);

#undef _COMPILE_TIME_ASSERT

static struct err_msg_seg _generic_err_msgs_seg = {
    { NULL, NULL },
    PURC_ERROR_OK, PURC_ERROR_OK + PCA_TABLESIZE(generic_err_msgs) - 1,
    generic_err_msgs,
};

bool purc_is_except_atom (purc_atom_t atom)
{
    if (atom < _except_names[0].atom ||
            atom > _except_names[PURC_EXCEPT_NR - 1].atom)
        return false;

    return true;
}

purc_atom_t purc_get_except_atom_by_id (int id)
{
    if (id < PURC_EXCEPT_NR)
        return _except_names[id].atom;

    return 0;
}

static int
except_init_once(void)
{
    for (size_t n = 0; n < PURC_EXCEPT_NR; n++) {
        _except_names[n].atom =
            purc_atom_from_static_string_ex(ATOM_BUCKET_EXCEPT,
                _except_names[n].str);

        if (!_except_names[n].atom)
            return -1;
    }

    return 0;
}

struct pcmodule _module_except = {
    .id              = PURC_HAVE_UTILS,
    .module_inited   = 0,

    .init_once       = except_init_once,
    .init_instance   = NULL,
};

#if 0
locale_t __purc_locale_c;
static void free_locale_c(void)
{
    if (__purc_locale_c)
        freelocale(__purc_locale_c);
    __purc_locale_c = NULL;
}
#endif

static int
locale_init_once(void)
{
    tzset();
    setlocale(LC_ALL, "");
    return 0;
}

struct pcmodule _module_locale = {
    .id              = PURC_HAVE_UTILS,
    .module_inited   = 0,

    .init_once       = locale_init_once,
    .init_instance   = NULL,
};

static int
errmsg_init_once(void)
{
    pcinst_register_error_message_segment(&_generic_err_msgs_seg);
    return 0;
}

struct pcmodule _module_errmsg = {
    .id              = PURC_HAVE_UTILS,
    .module_inited   = 0,

    .init_once       = errmsg_init_once,
    .init_instance   = NULL,
};

extern struct pcmodule _module_atom;
extern struct pcmodule _module_keywords;
extern struct pcmodule _module_runloop;
extern struct pcmodule _module_rwstream;
extern struct pcmodule _module_dom;
extern struct pcmodule _module_html;
extern struct pcmodule _module_variant;
extern struct pcmodule _module_mvheap;
extern struct pcmodule _module_mvbuf;
extern struct pcmodule _module_ejson;
extern struct pcmodule _module_dvobjs;
extern struct pcmodule _module_hvml;
extern struct pcmodule _module_executor;
extern struct pcmodule _module_interpreter;
extern struct pcmodule _module_fetcher_local;
extern struct pcmodule _module_fetcher_remote;
extern struct pcmodule _module_renderer;

struct pcmodule* _pc_modules[] = {
    &_module_locale,
    &_module_atom,

    &_module_except,
    &_module_keywords,

    &_module_errmsg,

    &_module_runloop,

    &_module_rwstream,
    &_module_dom,
    &_module_html,

    &_module_variant,
    &_module_mvheap,
    &_module_mvbuf,

    &_module_ejson,
    &_module_dvobjs,
    &_module_hvml,

    &_module_executor,
    &_module_interpreter,

    &_module_fetcher_local,
    &_module_fetcher_remote,

    &_module_renderer,
};

struct hvml_app {
#if USE(PTHREADS)          /* { */
    pthread_mutex_t               locker;
#endif                     /* } */
    struct list_head              instances;  // struct pcinst

    bool                          init_ok;

    char                         *name;
};

static struct hvml_app _app;

static bool _init_ok = false;

struct hvml_app* hvml_app_get(void)
{
    if (!_init_ok)
        return NULL;

    if (_app.init_ok == false)
        return NULL;

    return &_app;
}

const char* hvml_app_name(void)
{
    struct hvml_app *app = hvml_app_get();
    if (!app)
        return NULL;

    return app->name;
}

static void app_lock(struct hvml_app *app)
{
#if USE(PTHREADS)          /* { */
    pthread_mutex_lock(&app->locker);
#endif                     /* }*/
}

static void app_unlock(struct hvml_app *app)
{
#if USE(PTHREADS)          /* { */
    pthread_mutex_unlock(&app->locker);
#endif                     /* }*/
}

static int app_set_name(struct hvml_app *app, const char *app_name)
{
    int r = PURC_ERROR_OK;

    do {
        if (app->name && strcmp(app->name, app_name)) {
            r = PURC_ERROR_DUPLICATED;
            break;
        }
        if (!app->name) {
            app->name = strdup(app_name);
            if (!app->name) {
                r = PURC_ERROR_OUT_OF_MEMORY;
                break;
            }
        }
    } while (0);

    return r;
}

static void app_cleanup_once(void)
{
#if 0         /* { */
    app_lock(&_app);
    while (!list_empty(&_app.instances)) {
        app_unlock(&_app);
        app_lock(&_app);
    }
    app_unlock(&_app);
#endif        /* } */

    PC_ASSERT(list_empty(&_app.instances));
#if USE(PTHREADS)          /* { */
    pthread_mutex_destroy(&_app.locker);
#endif                     /* } */

    if (_app.name) {
        free(_app.name);
        _app.name = NULL;
    }
}

static void _app_init_once(void)
{
    INIT_LIST_HEAD(&_app.instances);
    int r;
#if USE(PTHREADS)          /* { */
    r = pthread_mutex_init(&_app.locker, NULL);
    if (r)
        goto fail_mutex;
#endif                     /* } */

    r = atexit(app_cleanup_once);
    if (r)
        goto fail_atexit;

    _app.init_ok = true;
    return;

#if USE(PTHREADS)          /* { */
fail_atexit:
    pthread_mutex_destroy(&_app.locker);
#endif                     /* } */

fail_mutex:
    return;
}

static void _init_once(void)
{
#if 0
     __purc_locale_c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    atexit(free_locale_c);
#endif

    _app_init_once();
    if (!_app.init_ok)
        return;

    for (size_t i=0; i<PCA_TABLESIZE(_pc_modules); ++i) {
        struct pcmodule *m = _pc_modules[i];
        if (!m->init_once)
            continue;

        if (m->init_once())
            return;

        m->module_inited = 1;
    }

    _init_ok = true;
}

static inline void init_once(void)
{
    static int inited = false;
    if (inited)
        return;

#if USE(PTHREADS)          /* { */
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, _init_once);
#else                      /* }{ */
    _init_once();
#endif                     /* } */

    inited = true;
}

PURC_DEFINE_THREAD_LOCAL(struct pcinst, inst);

static int app_new_inst(struct hvml_app *app, const char *runner_name,
        struct pcinst **pcurr_inst)
{
    int r = PURC_ERROR_OK;

    do {
        struct pcinst *curr_inst = PURC_GET_THREAD_LOCAL(inst);
        if (curr_inst == NULL) {
            r = PURC_ERROR_OUT_OF_MEMORY;
            break;
        }

        if (curr_inst->modules || curr_inst->runner_name) {
            r = PURC_ERROR_DUPLICATED;
            break;
        }

        if (curr_inst->node.prev || curr_inst->node.next) {
            r = PURC_ERROR_DUPLICATED;
            break;
        }

        struct pcinst *p;
        list_for_each_entry(p, &app->instances, node) {
            if (p == curr_inst) {
                r = PURC_ERROR_DUPLICATED;
                break;
            }
            if (0 == strcmp(p->runner_name, runner_name)) {
                r = PURC_ERROR_DUPLICATED;
                break;
            }
        }
        if (r)
            break;

        curr_inst->runner_name = strdup(runner_name);
        if (!curr_inst->runner_name) {
            r = PURC_ERROR_OUT_OF_MEMORY;
            break;
        }

        curr_inst->errcode = PURC_ERROR_OK;
        curr_inst->app_name = app->name;

        curr_inst->running_loop = purc_runloop_get_current();
        curr_inst->running_thread = pthread_self();

        list_add_tail(&curr_inst->node, &app->instances);

        *pcurr_inst = curr_inst;
        PC_ASSERT(r == 0);
        PC_ASSERT(r == PURC_ERROR_OK);
    } while (0);

    return r;
}

struct pcinst* pcinst_current(void)
{
    struct pcinst* curr_inst;
    curr_inst = PURC_GET_THREAD_LOCAL(inst);

    if (curr_inst == NULL || curr_inst->app_name == NULL) {
        return NULL;
    }

    return curr_inst;
}

static void cleanup_instance(struct hvml_app *app, struct pcinst *curr_inst)
{
    if (curr_inst->local_data_map) {
        pcutils_map_destroy(curr_inst->local_data_map);
        curr_inst->local_data_map = NULL;
    }

    if (curr_inst->fp_log && curr_inst->fp_log != LOG_FILE_SYSLOG) {
        fclose(curr_inst->fp_log);
        curr_inst->fp_log = NULL;
    }

    if (curr_inst->bt) {
        pcdebug_backtrace_unref(curr_inst->bt);
        curr_inst->bt = NULL;
    }

    app_lock(app);
    if (curr_inst->node.next || curr_inst->node.prev)
        list_del(&curr_inst->node);

    if (curr_inst->runner_name) {
        free(curr_inst->runner_name);
        curr_inst->runner_name = NULL;
    }

    if (curr_inst->app_name)
        curr_inst->app_name = NULL;

    curr_inst->modules = 0;
    app_unlock(app);
}

static int _init_instance(struct pcinst *curr_inst,
        unsigned int modules, const purc_instance_extra_info* extra_info)
{
    for (size_t i=0; i<PCA_TABLESIZE(_pc_modules); ++i) {
        struct pcmodule *m = _pc_modules[i];
        if ((m->id & modules) != m->id)
            continue;
        if (m->init_instance == NULL)
            continue;
        if( m->init_instance(curr_inst, extra_info))
            return -1;
    }

    return 0;
}

static void _cleanup_instance(struct pcinst *curr_inst)
{
    for (size_t i=PCA_TABLESIZE(_pc_modules); i>0; ) {
        struct pcmodule *m = _pc_modules[--i];
        if (m->cleanup_instance == NULL)
            continue;
        m->cleanup_instance(curr_inst);
    }
}

static void enable_log_on_demand(void)
{
    const char *env_value;

    env_value = getenv(PURC_ENVV_LOG_ENABLE);
    if (env_value == NULL)
        return;

    bool enable = (*env_value == '1' ||
            pcutils_strcasecmp(env_value, "true") == 0);
    if (!enable)
        return;

    bool use_syslog = false;
    if ((env_value = getenv(PURC_ENVV_LOG_SYSLOG))) {
        use_syslog = (*env_value == '1' ||
                pcutils_strcasecmp(env_value, "true") == 0);
    }

    purc_enable_log(true, use_syslog);
}

static int instance_init_modules(struct pcinst *curr_inst,
        unsigned int modules, const purc_instance_extra_info* extra_info)
{
    curr_inst->modules = modules;

    // endpoint_atom
    char endpoint_name [PURC_LEN_ENDPOINT_NAME + 1];
    purc_atom_t endpoint_atom;

    if (purc_assemble_endpoint_name(PCRDR_LOCALHOST,
                curr_inst->app_name, curr_inst->runner_name,
                endpoint_name) == 0) {
        return PURC_ERROR_INVALID_VALUE;
    }

    endpoint_atom = purc_atom_try_string_ex(PURC_ATOM_BUCKET_USER,
            endpoint_name);
    if (curr_inst->endpoint_atom == 0 && endpoint_atom) {
        return PURC_ERROR_DUPLICATED;
    }

    /* check whether app_name or runner_name changed */
    if (curr_inst->endpoint_atom &&
            curr_inst->endpoint_atom != endpoint_atom) {
        return PURC_ERROR_INVALID_VALUE;
    }

    curr_inst->endpoint_atom =
        purc_atom_from_string_ex(PURC_ATOM_BUCKET_USER, endpoint_name);
    assert(curr_inst->endpoint_atom);

    enable_log_on_demand();

    // map for local data
    curr_inst->local_data_map =
        pcutils_map_create(copy_key_string,
                free_key_string, NULL, NULL, comp_key_string, false);

    if (curr_inst->endpoint_atom == 0) {
        return PURC_ERROR_OUT_OF_MEMORY;
    }

    curr_inst->max_conns                  = FETCHER_MAX_CONNS;
    curr_inst->cache_quota                = FETCHER_CACHE_QUOTA;
    curr_inst->enable_remote_fetcher      = modules & PURC_HAVE_FETCHER_R;

    _init_instance(curr_inst, modules, extra_info);

    /* VW NOTE: eDOM and HTML modules should work without instance
    pcdom_init_instance(curr_inst);
    pchtml_init_instance(curr_inst); */

    // TODO: init XML modules here

    // TODO: init XGML modules here

    return PURC_ERROR_OK;
}

static bool pcinst_cleanup(struct hvml_app *app, struct pcinst *curr_inst)
{
    if (curr_inst == NULL || curr_inst->app_name == NULL)
        return false;

    PURC_VARIANT_SAFE_CLEAR(curr_inst->err_exinfo);

    // TODO: clean up other fields in reverse order

    _cleanup_instance(curr_inst);

    /* VW NOTE: eDOM and HTML modules should work without instance
       pchtml_cleanup_instance(curr_inst);
       pcdom_cleanup_instance(curr_inst); */

    cleanup_instance(app, curr_inst);

    return true;
}

int purc_init_ex(unsigned int modules,
        const char* app_name, const char* runner_name,
        const purc_instance_extra_info* extra_info)
{
    if (modules == 0) {
        modules = PURC_MODULE_ALL;
        if (modules == 0)
            return PURC_ERROR_NO_INSTANCE;
    }

    char cmdline[128];
    cmdline[0] = '\0';

    if (!app_name) {
        size_t len;
        len = pcutils_get_cmdline_arg(0, cmdline, sizeof(cmdline));
        if (len > 0)
            app_name = cmdline;
        else
            app_name = "unknown";
    }

    if (!runner_name)
        runner_name = "unknown";

    init_once();
    if (!_init_ok)
        return PURC_ERROR_NO_INSTANCE;

    struct hvml_app *app = hvml_app_get();
    if (!app)
        return PURC_ERROR_NO_INSTANCE;

    int ret = PURC_ERROR_OK;
    struct pcinst* curr_inst = NULL;

    app_lock(app);
    do {
        ret = app_set_name(app, app_name);
        if (ret)
            break;

        ret = app_new_inst(app, runner_name, &curr_inst);
        if (ret)
            break;

        if (curr_inst == NULL) {
            ret = PURC_ERROR_OUT_OF_MEMORY;
            break;
        }

        ret = instance_init_modules(curr_inst, modules, extra_info);
        if (ret) {
            list_del(&curr_inst->node);
            break;
        }
    } while (0);
    app_unlock(app);

    if (ret) {
        pcinst_cleanup(app, curr_inst);
        return ret;
    }

    return PURC_ERROR_OK;
}

bool purc_cleanup(void)
{
    struct hvml_app *app = hvml_app_get();
    if (!app)
        return false;

    struct pcinst* curr_inst;

    curr_inst = PURC_GET_THREAD_LOCAL(inst);

    return pcinst_cleanup(app, curr_inst);
}

bool
purc_set_local_data(const char* data_name, uintptr_t local_data,
        cb_free_local_data cb_free)
{
    struct pcinst* inst = pcinst_current();
    if (inst == NULL)
        return false;

    if (pcutils_map_find_replace_or_insert(inst->local_data_map,
                data_name, (void *)local_data, (free_val_fn)cb_free)) {
        inst->errcode = PURC_ERROR_OUT_OF_MEMORY;
        return false;
    }

    return true;
}

ssize_t
purc_remove_local_data(const char* data_name)
{
    struct pcinst* inst = pcinst_current();
    if (inst == NULL)
        return -1;

    if (data_name) {
        if (pcutils_map_erase (inst->local_data_map, (void*)data_name))
            return 1;
    }
    else {
        ssize_t sz = pcutils_map_get_size(inst->local_data_map);
        pcutils_map_clear(inst->local_data_map);
        return sz;
    }

    return 0;
}

int
purc_get_local_data(const char* data_name, uintptr_t *local_data,
        cb_free_local_data* cb_free)
{
    struct pcinst* inst;
    const pcutils_map_entry* entry = NULL;

    if ((inst = pcinst_current()) == NULL)
        return -1;

    if (data_name == NULL) {
        inst->errcode = PURC_ERROR_INVALID_VALUE;
        return -1;
    }

    if ((entry = pcutils_map_find(inst->local_data_map, data_name))) {
        if (local_data)
            *local_data = (uintptr_t)entry->val;

        if (cb_free)
            *cb_free = (cb_free_local_data)entry->free_val_alt;

        return 1;
    }

    return 0;
}

bool purc_bind_variable(const char* name, purc_variant_t variant)
{
    pcvarmgr_t varmgr = pcinst_get_variables();
    PC_ASSERT(varmgr);

    return pcvarmgr_add(varmgr, name, variant);
}

pcvarmgr_t pcinst_get_variables(void)
{
    struct pcinst* inst = pcinst_current();
    if (UNLIKELY(inst == NULL))
        return NULL;

    if (UNLIKELY(inst->variables == NULL)) {
        inst->variables = pcvarmgr_create();
    }

    return inst->variables;
}

purc_variant_t purc_get_variable(const char* name)
{
    pcvarmgr_t varmgr = pcinst_get_variables();
    PC_ASSERT(varmgr);

    return pcvarmgr_get(varmgr, name);
}

bool
purc_bind_document_variable(purc_vdom_t vdom, const char* name,
        purc_variant_t variant)
{
    return pcvdom_document_bind_variable(vdom, name, variant);
}

struct pcrdr_conn *
purc_get_conn_to_renderer(void)
{
    struct pcinst* inst = pcinst_current();
    if (inst == NULL)
        return NULL;

    return inst->conn_to_rdr;
}

#if 0
bool purc_unbind_variable(const char* name)
{
    struct pcinst* inst = pcinst_current();
    if (inst == NULL)
        return false;

    return pcvarmgr_remove(inst->variables, name);
}

bool
purc_unbind_document_variable(purc_vdom_t vdom, const char* name)
{
    return pcvdom_document_unbind_variable(vdom, name);
}
#endif

void pcinst_clear_error(struct pcinst *inst)
{
    if (!inst)
        return;

    inst->errcode = 0;
    PURC_VARIANT_SAFE_CLEAR(inst->err_exinfo);

    if (inst->bt) {
        pcdebug_backtrace_unref(inst->bt);
        inst->bt = NULL;
    }
}

