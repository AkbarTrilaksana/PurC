/*
 * @file timer.cpp
 * @author XueShuming
 * @date 2021/12/20
 * @brief The C api for timer.
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

#include "internal.h"

#include "private/errors.h"
#include "private/timer.h"
#include "private/interpreter.h"
#include "purc-runloop.h"

#include <wtf/RunLoop.h>
#include <wtf/Seconds.h>

#include <stdlib.h>
#include <string.h>

struct event_timer_data {
    pcintr_timer_t            timer;
    const char               *id;
    pcintr_timer_fire_func    func;
};

static void on_event_fire(void *ud)
{
    struct event_timer_data *data;
    data = (struct event_timer_data*)ud;
    pcintr_timer_processed(data->timer);
    data->func(data->timer, data->id);
}

static void cancel_timer(void *ctxt)
{
    pcintr_timer_t timer = (pcintr_timer_t)ctxt;
    PC_ASSERT(timer);
    pcintr_timer_stop(timer);
}

class PurcTimer : public PurCWTF::RunLoop::TimerBase {
    public:
        PurcTimer(bool for_yielded, const char* id, pcintr_timer_fire_func func,
                RunLoop& runLoop, void *attach,
                pcintr_timer_attach_destroy_func attach_destroy_func)
            : TimerBase(runLoop)
            , m_id(id ? strdup(id) : NULL)
            , m_func(func)
            , m_coroutine(pcintr_get_coroutine())
            , m_fired(0)
            , m_for_yielded(for_yielded)
            , m_attach(attach)
            , m_attach_destroy_func(attach_destroy_func)
        {
            if (!m_for_yielded) {
                m_cancel = {};
                pcintr_cancel_init(&m_cancel, this, cancel_timer);

                m_data.timer = this;
                m_data.id    = m_id;
                m_data.func  = m_func;

                pcintr_register_cancel(&m_cancel);
            }
            else {
                PC_ASSERT(id == NULL);
                PC_ASSERT(func == NULL);
            }

            PC_ASSERT(m_coroutine);
            PC_ASSERT(!id || m_id);
        }

        ~PurcTimer()
        {
            PC_ASSERT(m_fired == 0);
            if (m_attach_destroy_func) {
                m_attach_destroy_func(m_attach);
            }
            if (!m_for_yielded) {
                pcintr_unregister_cancel(&m_cancel);
                stop();
            }
            if (m_id) {
                free(m_id);
            }
        }

        void setInterval(uint32_t interval) { m_interval = interval; }
        uint32_t getInterval() { return m_interval; }
        void processed(void) {
            --m_fired;
            PC_ASSERT(m_fired >= 0);
        }

    private:
        void fired() final {
            if (m_fired)
                return;

            if (m_coroutine->stack.exited) {
                PC_ASSERT(!m_for_yielded);
                pcintr_unregister_cancel(&m_cancel);
                stop();
                return;
            }

            PC_ASSERT(pcintr_get_coroutine() == NULL);

            pcintr_heap_t heap = pcintr_get_heap();
            PC_ASSERT(heap);
            PC_ASSERT(m_coroutine);

            pcintr_coroutine_t co = m_coroutine;
            if (co->state == CO_STATE_WAIT) {
                if (m_for_yielded) {
                    ++m_fired;
                    stop();
                    pcintr_set_current_co(m_coroutine);
                    pcintr_resume(NULL);
                    pcintr_set_current_co(NULL);
                    return;
                }

                ++m_fired;

                pcintr_set_current_co(m_coroutine);
                pcintr_post_msg_to_target(co, &m_data, on_event_fire);
                PC_ASSERT(co->state == CO_STATE_WAIT);
                pcintr_set_current_co(NULL);
                return;
            }

            PC_ASSERT(co->state == CO_STATE_READY);

            ++m_fired;

            pcintr_set_current_co(m_coroutine);

            pcintr_post_msg_to_target(m_coroutine, &m_data, on_event_fire);
            pcintr_check_after_execution();

            PC_ASSERT(co->state == CO_STATE_READY);

            pcintr_set_current_co(NULL);
        }

    private:
        char* m_id;
        pcintr_timer_fire_func m_func;
        pcintr_coroutine_t     m_coroutine;

        uint32_t m_interval;

        int m_fired;
        struct event_timer_data         m_data;
        struct pcintr_cancel            m_cancel;
        bool                            m_for_yielded;
        void                            *m_attach;
        pcintr_timer_attach_destroy_func m_attach_destroy_func;
};

pcintr_timer_t
pcintr_timer_create_ex(purc_runloop_t runloop, bool for_yielded, const char* id,
        pcintr_timer_fire_func func, void *attach,
        pcintr_timer_attach_destroy_func attach_destroy)
{
    RunLoop* loop = runloop ? (RunLoop*)runloop : &RunLoop::current();
    PurcTimer* timer = new PurcTimer(for_yielded, id, func, *loop,
            attach, attach_destroy);
    if (!timer) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    return timer;
}

void
pcintr_timer_set_interval(pcintr_timer_t timer, uint32_t interval)
{
    if (timer) {
        ((PurcTimer*)timer)->setInterval(interval);
    }
}

void
pcintr_timer_processed(pcintr_timer_t timer)
{
    PurcTimer *p = (PurcTimer*)timer;
    p->processed();
}

uint32_t
pcintr_timer_get_interval(pcintr_timer_t timer)
{
    if (timer) {
        return ((PurcTimer*)timer)->getInterval();
    }
    return 0;
}

void
pcintr_timer_start(pcintr_timer_t timer)
{
    if (timer) {
        PurcTimer* tm = (PurcTimer*)timer;
        tm->startRepeating(
                PurCWTF::Seconds::fromMilliseconds(tm->getInterval()));
    }
}

void
pcintr_timer_start_oneshot(pcintr_timer_t timer)
{
    if (timer) {
        PurcTimer* tm = (PurcTimer*)timer;
        tm->startOneShot(
                PurCWTF::Seconds::fromMilliseconds(tm->getInterval()));
    }
}

void
pcintr_timer_stop(pcintr_timer_t timer)
{
    if (timer) {
        ((PurcTimer*)timer)->stop();
    }
}

bool
pcintr_timer_is_active(pcintr_timer_t timer)
{
    return timer ? ((PurcTimer*)timer)->isActive() : false;
}

void
pcintr_timer_destroy(pcintr_timer_t timer)
{
    if (timer) {
        PurcTimer* tm = (PurcTimer*)timer;
        delete tm;
    }
}

//  $TIMERS begin

#define TIMERS_STR_ID               "id"
#define TIMERS_STR_INTERVAL         "interval"
#define TIMERS_STR_ACTIVE           "active"
#define TIMERS_STR_YES              "yes"
#define TIMERS_STR_TIMERS           "TIMERS"
#define TIMERS_STR_EXPIRED          "expired"

struct pcintr_timers {
    purc_variant_t timers_var;
    struct pcvar_listener* timer_listener;
    pcutils_map* timers_map; // id : pcintr_timer_t
};

static void* map_copy_val(const void* val)
{
    return (void*)val;
}

static void map_free_val(void* val)
{
    if (val) {
        pcintr_timer_destroy((pcintr_timer_t)val);
    }
}

static void timer_fire_func(pcintr_timer_t timer, const char* id)
{
    UNUSED_PARAM(timer);

    PC_ASSERT(pcintr_get_heap());

    pcintr_coroutine_t co = pcintr_get_coroutine();
    PC_ASSERT(co);
    PC_ASSERT(co->state == CO_STATE_RUN);

    pcintr_stack_t stack = &co->stack;

    purc_variant_t type = PURC_VARIANT_INVALID;
    purc_variant_t sub_type = PURC_VARIANT_INVALID;

    if (stack->exited)
        return;

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame == NULL);

    type = purc_variant_make_string(TIMERS_STR_EXPIRED, false);
    sub_type = purc_variant_make_string(id, false);

    if (type && sub_type) {
        pcintr_dispatch_message_ex(stack,
                stack->timers->timers_var,
                type, sub_type, PURC_VARIANT_INVALID);
    }

    PURC_VARIANT_SAFE_CLEAR(type);
    PURC_VARIANT_SAFE_CLEAR(sub_type);
}

bool
timer_listener_handler(purc_variant_t source, pcvar_op_t msg_type,
        void* ctxt, size_t nr_args, purc_variant_t* argv);

static bool
is_euqal(purc_variant_t var, const char* comp)
{
    if (var && comp) {
        return (strcmp(purc_variant_get_string_const(var), comp) == 0);
    }
    return false;
}

pcintr_timer_t
find_timer(struct pcintr_timers* timers, const char* id)
{
    pcutils_map_entry* entry = pcutils_map_find(timers->timers_map, id);
    return entry ? (pcintr_timer_t) entry->val : NULL;
}

bool
add_timer(struct pcintr_timers* timers, const char* id, pcintr_timer_t timer)
{
    int r;
    r = pcutils_map_find_replace_or_insert(timers->timers_map, id, timer, NULL);
    if (0 == r) {
        return true;
    }
    return false;
}

void
remove_timer(struct pcintr_timers* timers, const char* id)
{
    pcutils_map_erase(timers->timers_map, (void*)id);
}

static pcintr_timer_t
get_inner_timer(pcintr_stack_t stack, purc_variant_t timer_var)
{
    PC_ASSERT(pcintr_get_stack());
    purc_variant_t id;
    id = purc_variant_object_get_by_ckey(timer_var, TIMERS_STR_ID);
    if (!id) {
        purc_set_error(PURC_ERROR_INVALID_VALUE);
        return NULL;
    }

    const char* idstr = purc_variant_get_string_const(id);
    pcintr_timer_t timer = find_timer(stack->timers, idstr);
    if (timer) {
        return timer;
    }

    bool for_yielded = false;
    timer = pcintr_timer_create(NULL, for_yielded, idstr, timer_fire_func);
    if (timer == NULL) {
        return NULL;
    }

    if (!add_timer(stack->timers, idstr, timer)) {
        pcintr_timer_destroy(timer);
        return NULL;
    }
    return timer;
}

static void
destroy_inner_timer(pcintr_stack_t stack, purc_variant_t timer_var)
{
    purc_variant_t id;
    id = purc_variant_object_get_by_ckey(timer_var, TIMERS_STR_ID);
    if (!id) {
        return;
    }

    const char* idstr = purc_variant_get_string_const(id);
    pcintr_timer_t timer = find_timer(stack->timers, idstr);
    if (timer) {
        remove_timer(stack->timers, idstr);
    }
}

bool
timers_set_grow(purc_variant_t source, pcvar_op_t msg_type,
        void* ctxt, size_t nr_args, purc_variant_t* argv)
{
    UNUSED_PARAM(source);
    UNUSED_PARAM(msg_type);
    UNUSED_PARAM(nr_args);
    UNUSED_PARAM(ctxt);
    pcintr_stack_t stack = pcintr_get_stack();
    purc_variant_t interval = purc_variant_object_get_by_ckey(argv[0],
            TIMERS_STR_INTERVAL);
    purc_variant_t active = purc_variant_object_get_by_ckey(argv[0],
            TIMERS_STR_ACTIVE);
    pcintr_timer_t timer = get_inner_timer(stack, argv[0]);
    if (!timer) {
        return false;
    }
    uint64_t ret = 0;
    purc_variant_cast_to_ulongint(interval, &ret, false);
    pcintr_timer_set_interval(timer, ret);
    if (is_euqal(active, TIMERS_STR_YES)) {
        pcintr_timer_start(timer);
    }
    return true;
}

bool
timers_set_shrink(purc_variant_t source, pcvar_op_t msg_type,
        void* ctxt, size_t nr_args, purc_variant_t* argv)
{
    UNUSED_PARAM(source);
    UNUSED_PARAM(msg_type);
    UNUSED_PARAM(nr_args);
    UNUSED_PARAM(ctxt);
    pcintr_stack_t stack = pcintr_get_stack();
    destroy_inner_timer(stack, argv[0]);
    return true;
}

bool
timers_set_change(purc_variant_t source, pcvar_op_t msg_type,
        void* ctxt, size_t nr_args, purc_variant_t* argv)
{
    UNUSED_PARAM(source);
    UNUSED_PARAM(msg_type);
    UNUSED_PARAM(ctxt);
    UNUSED_PARAM(nr_args);
    pcintr_stack_t stack = pcintr_get_stack();
    purc_variant_t nv = argv[1];
    pcintr_timer_t timer = get_inner_timer(stack, nv);
    if (!timer) {
        return false;
    }
    purc_variant_t interval = purc_variant_object_get_by_ckey(nv,
            TIMERS_STR_INTERVAL);
    purc_variant_t active = purc_variant_object_get_by_ckey(nv,
            TIMERS_STR_ACTIVE);
    if (interval != PURC_VARIANT_INVALID) {
        uint64_t ret = 0;
        purc_variant_cast_to_ulongint(interval, &ret, false);
        uint32_t oval = pcintr_timer_get_interval(timer);
        if (oval != ret) {
            pcintr_timer_set_interval(timer, ret);
        }
    }
    else {
        purc_clr_error();
    }
    bool next_active = pcintr_timer_is_active(timer);
    if (active != PURC_VARIANT_INVALID) {
        if (is_euqal(active, TIMERS_STR_YES)) {
            next_active = true;
        }
        else {
            next_active = false;
        }
    }

    if (next_active) {
        pcintr_timer_start(timer);
    }
    else {
        pcintr_timer_stop(timer);
    }
    return true;
}

bool
timers_set_listener_handler(purc_variant_t source, pcvar_op_t msg_type,
        void* ctxt, size_t nr_args, purc_variant_t* argv)
{
    switch (msg_type) {
    case PCVAR_OPERATION_GROW:
        return timers_set_grow(source, msg_type, ctxt, nr_args, argv);

    case PCVAR_OPERATION_SHRINK:
        return timers_set_shrink(source, msg_type, ctxt, nr_args, argv);

    case PCVAR_OPERATION_CHANGE:
        return timers_set_change(source, msg_type, ctxt, nr_args, argv);

    default:
        return true;
    }
    return true;
}

struct pcintr_timers*
pcintr_timers_init(pcintr_stack_t stack)
{
    purc_variant_t ret = purc_variant_make_set_by_ckey(0, TIMERS_STR_ID, NULL);
    if (!ret) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    if (!pcintr_bind_document_variable(stack->vdom, TIMERS_STR_TIMERS, ret)) {
        purc_variant_unref(ret);
        return NULL;
    }

    int op = PCVAR_OPERATION_GROW | PCVAR_OPERATION_SHRINK |
        PCVAR_OPERATION_CHANGE;
    struct pcintr_timers* timers = (struct pcintr_timers*) calloc(1,
            sizeof(struct pcintr_timers));
    if (!timers) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failure;
    }

    timers->timers_var = ret;
    purc_variant_ref(ret);

    timers->timers_map = pcutils_map_create (copy_key_string, free_key_string,
                          map_copy_val, map_free_val, comp_key_string, false);
    if (!timers->timers_map) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failure;
    }

    timers->timer_listener = purc_variant_register_post_listener(ret,
            (pcvar_op_t)op, timers_set_listener_handler, NULL);
    if (!timers->timer_listener) {
        goto failure;
    }

    purc_variant_unref(ret);
    return timers;

failure:
    if (timers)
        pcintr_timers_destroy(timers);
    pcintr_unbind_document_variable(stack->vdom, TIMERS_STR_TIMERS);
    purc_variant_unref(ret);
    return NULL;
}

void
pcintr_timers_destroy(struct pcintr_timers* timers)
{
    if (timers) {
        if (timers->timer_listener) {
            PC_ASSERT(timers->timers_var);
            purc_variant_revoke_listener(timers->timers_var,
                    timers->timer_listener);
            timers->timer_listener = NULL;
        }

        // remove inner timer
        if (timers->timers_map) {
            pcutils_map_destroy(timers->timers_map);
            timers->timers_map = NULL;
        }

        PURC_VARIANT_SAFE_CLEAR(timers->timers_var);
        free(timers);
    }
}

bool
pcintr_is_timers(pcintr_stack_t stack, purc_variant_t v)
{
    if (!stack) {
        return false;
    }
    return (v == stack->timers->timers_var);
}
