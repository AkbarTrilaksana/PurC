/*
 * @file move-heap.c
 * @author Vincent Wei
 * @date 2022/03/08
 * @brief The implementation of internal interfaces to move variant.
 *
 * Copyright (C) 2022 FMSoft <https://www.fmsoft.cn>
 *
 * Authors:
 *  Vincent Wei (https://github.com/VincentWei), 2022
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

#include "config.h"

#include "private/instance.h"
#include "private/variant.h"

#include "variant-internals.h"

#include <stdlib.h>
#include <string.h>

static struct purc_mutex        mh_lock;
static struct pcvariant_heap    move_heap;

static void mvheap_cleanup_once(void)
{
    if (mh_lock.native_impl)
        purc_mutex_clear(&mh_lock);

    struct purc_variant_stat *stat = &move_heap.stat;

    // FIXME: multiple runner
#if 0
    PC_DEBUG("refc of v_undefined in move heap: %u\n", move_heap.v_undefined.refc);
    PC_DEBUG("refc of v_null in move heap: %u\n", move_heap.v_null.refc);
    PC_DEBUG("refc of v_true in move heap: %u\n", move_heap.v_true.refc);
    PC_DEBUG("refc of v_false in move heap: %u\n", move_heap.v_false.refc);
    PC_DEBUG("total values in move heap: %u\n", (unsigned int)stat->nr_total_values);
    PC_DEBUG("total memory used by move heap: %u\n", (unsigned int)stat->sz_total_mem);
#endif

    PC_ASSERT(move_heap.v_undefined.refc == 0);
    PC_ASSERT(move_heap.v_null.refc == 0);
    PC_ASSERT(move_heap.v_true.refc == 0);
    PC_ASSERT(move_heap.v_false.refc == 0);
    PC_ASSERT(stat->nr_total_values == 4);
    PC_ASSERT(stat->sz_total_mem == 4 * sizeof(purc_variant));
}

static int mvheap_init_once(void)
{
    move_heap.v_undefined.type = PURC_VARIANT_TYPE_UNDEFINED;
    move_heap.v_undefined.refc = 0;
    move_heap.v_undefined.flags = PCVARIANT_FLAG_NOFREE;
    INIT_LIST_HEAD(&move_heap.v_undefined.listeners);

    move_heap.v_null.type = PURC_VARIANT_TYPE_NULL;
    move_heap.v_null.refc = 0;
    move_heap.v_null.flags = PCVARIANT_FLAG_NOFREE;
    INIT_LIST_HEAD(&move_heap.v_null.listeners);

    move_heap.v_false.type = PURC_VARIANT_TYPE_BOOLEAN;
    move_heap.v_false.refc = 0;
    move_heap.v_false.flags = PCVARIANT_FLAG_NOFREE;
    move_heap.v_false.b = false;
    INIT_LIST_HEAD(&move_heap.v_false.listeners);

    move_heap.v_true.type = PURC_VARIANT_TYPE_BOOLEAN;
    move_heap.v_true.refc = 0;
    move_heap.v_true.flags = PCVARIANT_FLAG_NOFREE;
    move_heap.v_true.b = true;

    struct purc_variant_stat *stat = &move_heap.stat;
    stat->nr_values[PURC_VARIANT_TYPE_UNDEFINED] = 0;
    stat->sz_mem[PURC_VARIANT_TYPE_UNDEFINED] = sizeof(purc_variant);
    stat->nr_values[PURC_VARIANT_TYPE_NULL] = 0;
    stat->sz_mem[PURC_VARIANT_TYPE_NULL] = sizeof(purc_variant);
    stat->nr_values[PURC_VARIANT_TYPE_BOOLEAN] = 0;
    stat->sz_mem[PURC_VARIANT_TYPE_BOOLEAN] = sizeof(purc_variant) * 2;
    stat->nr_total_values = 4;
    stat->sz_total_mem = 4 * sizeof(purc_variant);

    stat->nr_reserved = 0;
    stat->nr_max_reserved = 0;  // no need to reserve variants for move heap.

    purc_mutex_init(&mh_lock);
    if (mh_lock.native_impl == NULL)
        return -1;

    int r;
    r = atexit(mvheap_cleanup_once);
    if (r)
        goto fail_atexit;

    return 0;

fail_atexit:
    purc_mutex_clear(&mh_lock);

    return -1;
}

struct pcmodule _module_mvheap = {
    .id              = PURC_HAVE_VARIANT,
    .module_inited   = 0,

    .init_once       = mvheap_init_once,
    .init_instance   = NULL,
};

static void
move_variant_in(struct pcinst *inst, purc_variant_t v)
{
    /* move directly and change the stat info */

    if (IS_CONTAINER(v->type) ||
            ((v->type == PURC_VARIANT_TYPE_STRING ||
                v->type == PURC_VARIANT_TYPE_BSEQUENCE) &&
            (v->flags & PCVARIANT_FLAG_EXTRA_SIZE))) {
        inst->org_vrt_heap->stat.sz_mem[v->type] -= v->sz_ptr[0];
        inst->org_vrt_heap->stat.sz_total_mem -= v->sz_ptr[0];

        move_heap.stat.sz_mem[v->type] += v->sz_ptr[0];
        move_heap.stat.sz_total_mem += v->sz_ptr[0];
    }

    inst->org_vrt_heap->stat.nr_values[v->type]--;
    inst->org_vrt_heap->stat.nr_total_values--;
    move_heap.stat.nr_values[v->type]++;
    move_heap.stat.nr_total_values++;

    inst->org_vrt_heap->stat.sz_mem[v->type] -= sizeof(purc_variant);
    inst->org_vrt_heap->stat.sz_total_mem -= sizeof(purc_variant);
    move_heap.stat.sz_mem[v->type] += sizeof(purc_variant);
    move_heap.stat.sz_total_mem += sizeof(purc_variant);
}

static purc_variant_t
move_or_clone_immutable(struct pcinst *inst, purc_variant_t v)
{
    purc_variant_t retv = PURC_VARIANT_INVALID;

    if (IS_CONTAINER(v->type))
        return retv;

    if (v == &inst->org_vrt_heap->v_undefined) {
        retv = &move_heap.v_undefined;
        v->refc--;
        retv->refc++;
    }
    else if (v == &inst->org_vrt_heap->v_null) {
        retv = &move_heap.v_null;
        v->refc--;
        retv->refc++;
    }
    else if (v == &inst->org_vrt_heap->v_false) {
        retv = &move_heap.v_false;
        v->refc--;
        retv->refc++;
    }
    else if (v == &inst->org_vrt_heap->v_true) {
        retv = &move_heap.v_true;
        v->refc--;
        retv->refc++;
    }
    else if (v->refc == 1) {
        retv = v;
        move_variant_in(inst, v);
    }
    else {
        // clone the immutable variant

        retv = pcvariant_alloc();
        memcpy(retv, v, sizeof(*retv));
        retv->refc = 1;

        /* copy the extra space */
        if ((v->type == PURC_VARIANT_TYPE_STRING ||
                v->type == PURC_VARIANT_TYPE_BSEQUENCE) &&
                (v->flags & PCVARIANT_FLAG_EXTRA_SIZE)) {

            retv->sz_ptr[1] = (uintptr_t)malloc(v->sz_ptr[0]);
            memcpy((void *)retv->sz_ptr[1], (void *)v->sz_ptr[1], v->sz_ptr[0]);

            move_heap.stat.sz_mem[v->type] += v->sz_ptr[0];
            move_heap.stat.sz_total_mem += v->sz_ptr[0];
        }

        move_heap.stat.nr_values[v->type]++;
        move_heap.stat.nr_total_values++;
        move_heap.stat.sz_mem[v->type] += sizeof(purc_variant);
        move_heap.stat.sz_total_mem += sizeof(purc_variant);
    }

    return retv;
}

struct travel_context {
    struct pcinst *inst;
    struct pcutils_arrlist *vrts_to_unref;
    unsigned int el; /* embedded levels */
};

static bool
move_or_clone_mutable_descendants_in_array(struct travel_context *ctxt,
        purc_variant_t v);
static bool
move_or_clone_mutable_descendants_in_object(struct travel_context *ctxt,
        purc_variant_t v);
static bool
move_or_clone_mutable_descendants_in_set(struct travel_context *ctxt,
        purc_variant_t v);

static bool
move_or_clone_mutable_descendants_in_array(struct travel_context *ctxt,
        purc_variant_t arr)
{
#if 0
    ctxt->el++;
    if (ctxt->el >= ctxt->inst->max_embedded_levels) {
        purc_set_error(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
        return false;
    }
#endif

    size_t idx;
    purc_variant_t v;
    foreach_value_in_variant_array(arr, v, idx) {
        purc_variant_t retv;

        UNUSED_PARAM(idx);
        switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_array(ctxt, v);
            }
            break;

        case PURC_VARIANT_TYPE_OBJECT:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_object(ctxt, v);
            }
            break;

        case PURC_VARIANT_TYPE_SET:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_set(ctxt, v);
            }
            break;

        default:
            // immutable element
            return true;
        }

        retv = purc_variant_container_clone_recursively(v);
        if (retv == PURC_VARIANT_INVALID) {
            purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
            return false;
        }

        _p->val = retv;
        pcutils_arrlist_append(ctxt->vrts_to_unref, v);

    } end_foreach;

    return true;
}

static bool
move_or_clone_mutable_descendants_in_object(struct travel_context *ctxt,
        purc_variant_t obj)
{
#if 0
    ctxt->el++;
    if (ctxt->el >= ctxt->inst->max_embedded_levels) {
        purc_set_error(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
        return false;
    }
#endif

    purc_variant_t k,v;
    foreach_key_value_in_variant_object(obj, k, v) {
        purc_variant_t retv;

        UNUSED_PARAM(k);

        switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_array(ctxt, v);
            }
            break;

        case PURC_VARIANT_TYPE_OBJECT:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_object(ctxt, v);
            }
            break;

        case PURC_VARIANT_TYPE_SET:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_set(ctxt, v);
            }
            break;

        default:
            // immutable element
            return true;
        }

        retv = purc_variant_container_clone_recursively(v);
        if (retv == PURC_VARIANT_INVALID) {
            purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
            return false;
        }

        _node->val = retv;
        pcutils_arrlist_append(ctxt->vrts_to_unref, v);
    } end_foreach;

    return true;
}

static bool
move_or_clone_mutable_descendants_in_set(struct travel_context *ctxt,
        purc_variant_t set)
{
#if 0
    ctxt->el++;
    if (ctxt->el >= ctxt->inst->max_embedded_levels) {
        purc_set_error(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
        return false;
    }
#endif

    purc_variant_t v;
    foreach_value_in_variant_set(set, v) {
        purc_variant_t retv;

        switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_array(ctxt, v);
            }
            break;

        case PURC_VARIANT_TYPE_OBJECT:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_object(ctxt, v);
            }
            break;

        case PURC_VARIANT_TYPE_SET:
            if (v->refc == 1) {
                move_variant_in(ctxt->inst, v);
                return move_or_clone_mutable_descendants_in_set(ctxt, v);
            }
            break;

        default:
            // immutable element
            return true;
        }

        retv = purc_variant_container_clone_recursively(v);
        if (retv == PURC_VARIANT_INVALID) {
            purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
            return false;
        }

        _sn->val = retv;
        pcutils_arrlist_append(ctxt->vrts_to_unref, v);

    } end_foreach;

    return true;
}

static bool
move_or_clone_mutable_descendants(struct travel_context *ctxt,
        purc_variant_t v)
{
    switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            return move_or_clone_mutable_descendants_in_array(ctxt, v);
        case PURC_VARIANT_TYPE_OBJECT:
            return move_or_clone_mutable_descendants_in_object(ctxt, v);
        case PURC_VARIANT_TYPE_SET:
            return move_or_clone_mutable_descendants_in_set(ctxt, v);
        default:
            break;
    }

    return true;
}

static bool
move_or_clone_immutable_descendants_in_array(struct travel_context *ctxt,
        purc_variant_t v);
static bool
move_or_clone_immutable_descendants_in_object(struct travel_context *ctxt,
        purc_variant_t v);
static bool
move_or_clone_immutable_descendants_in_set(struct travel_context *ctxt,
        purc_variant_t v);

static bool
move_or_clone_immutable_descendants_in_array(struct travel_context *ctxt,
        purc_variant_t arr)
{
#if 0
    ctxt->el++;
    if (ctxt->el >= ctxt->inst->max_embedded_levels) {
        purc_set_error(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
        return false;
    }
#endif

    size_t idx;
    purc_variant_t v;
    foreach_value_in_variant_array(arr, v, idx) {
        purc_variant_t retv;

        UNUSED_PARAM(idx);
        switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            return move_or_clone_immutable_descendants_in_array(ctxt, v);

        case PURC_VARIANT_TYPE_OBJECT:
            return move_or_clone_immutable_descendants_in_object(ctxt, v);

        case PURC_VARIANT_TYPE_SET:
            return move_or_clone_immutable_descendants_in_set(ctxt, v);

        default:
            retv = move_or_clone_immutable(ctxt->inst, v);
            if (retv == PURC_VARIANT_INVALID) {
                purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
                return false;
            }

            if (retv != v) {
                _p->val = retv;
                pcutils_arrlist_append(ctxt->vrts_to_unref, v);
            }
            break;
        }
    } end_foreach;

    return true;
}

static bool
move_or_clone_immutable_descendants_in_object(struct travel_context *ctxt,
        purc_variant_t obj)
{
#if 0
    ctxt->el++;
    if (ctxt->el >= ctxt->inst->max_embedded_levels) {
        purc_set_error(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
        return false;
    }
#endif

    purc_variant_t k,v;
    foreach_key_value_in_variant_object(obj, k, v) {
        purc_variant_t retk, retv;

        switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            return move_or_clone_immutable_descendants_in_array(ctxt, v);

        case PURC_VARIANT_TYPE_OBJECT:
            return move_or_clone_immutable_descendants_in_object(ctxt, v);

        case PURC_VARIANT_TYPE_SET:
            return move_or_clone_immutable_descendants_in_set(ctxt, v);

        default:
            retk = move_or_clone_immutable(ctxt->inst, k);
            retv = move_or_clone_immutable(ctxt->inst, v);
            if (retk == PURC_VARIANT_INVALID || retv == PURC_VARIANT_INVALID) {
                purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
                return false;
            }

            if (retk != k) {
                _node->key = retk;
                pcutils_arrlist_append(ctxt->vrts_to_unref, k);
            }

            if (retv != v) {
                _node->val = retv;
                pcutils_arrlist_append(ctxt->vrts_to_unref, v);
            }
            break;
        }
    } end_foreach;

    return true;
}

static bool
move_or_clone_immutable_descendants_in_set(struct travel_context *ctxt,
        purc_variant_t set)
{
#if 0
    ctxt->el++;
    if (ctxt->el >= ctxt->inst->max_embedded_levels) {
        purc_set_error(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
        return false;
    }
#endif

    purc_variant_t v;
    foreach_value_in_variant_set(set, v) {
        purc_variant_t retv;

        switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            return move_or_clone_immutable_descendants_in_array(ctxt, v);

        case PURC_VARIANT_TYPE_OBJECT:
            return move_or_clone_immutable_descendants_in_object(ctxt, v);

        case PURC_VARIANT_TYPE_SET:
            return move_or_clone_immutable_descendants_in_set(ctxt, v);

        default:
            retv = move_or_clone_immutable(ctxt->inst, v);
            if (retv == PURC_VARIANT_INVALID) {
                purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
                return false;
            }

            if (retv != v) {
                _sn->val = retv;
                pcutils_arrlist_append(ctxt->vrts_to_unref, v);
            }
            break;
        }
    } end_foreach;

    return true;
}

static bool
move_or_clone_immutable_descendants(struct travel_context *ctxt,
        purc_variant_t v)
{
    switch (v->type) {
        case PURC_VARIANT_TYPE_ARRAY:
            return move_or_clone_immutable_descendants_in_array(ctxt, v);
        case PURC_VARIANT_TYPE_OBJECT:
            return move_or_clone_immutable_descendants_in_object(ctxt, v);
        case PURC_VARIANT_TYPE_SET:
            return move_or_clone_immutable_descendants_in_set(ctxt, v);
        default:
            break;
    }

    return true;
}

static void cb_free_element(void *data)
{
    purc_variant_unref(data);
}

// move the variant from the current instance to the move heap.
purc_variant_t pcvariant_move_heap_in(purc_variant_t v)
{
    purc_variant_t retv = PURC_VARIANT_INVALID;
    struct pcinst *inst = pcinst_current();
    struct travel_context ctxt;

    ctxt.inst = pcinst_current();
    ctxt.el = 0;
    ctxt.vrts_to_unref = pcutils_arrlist_new(cb_free_element);
    if (ctxt.vrts_to_unref == NULL) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return retv;
    }

    pcvariant_use_move_heap();

    if (IS_CONTAINER(v->type)) {
        if (v->refc == 1) {
            retv = v;
            move_variant_in(inst, v);
            ctxt.el = 0;
            move_or_clone_mutable_descendants(&ctxt, v);
        }
        else {
            retv = purc_variant_container_clone_recursively(v);
        }

        ctxt.el = 0;
        move_or_clone_immutable_descendants(&ctxt, v);
    }
    else {
        retv = move_or_clone_immutable(inst, v);
    }

    pcvariant_use_norm_heap();

    if (retv != PURC_VARIANT_INVALID && retv != v) {
        purc_variant_unref(v);
    }

    // the cloned immutable descendants will be unreferenced in cb_free_element
    pcutils_arrlist_free(ctxt.vrts_to_unref);

    return retv;
}

// move the variant from the move heap to the current instance.
// we only need to update the stat information.
purc_variant_t pcvariant_move_heap_out(purc_variant_t v)
{
    purc_variant_t retv = PURC_VARIANT_INVALID;
    struct pcinst *inst = pcinst_current();

    pcvariant_use_move_heap();
    if (v == &move_heap.v_undefined) {
        retv = &inst->org_vrt_heap->v_undefined;
        v->refc--;
        retv->refc++;
    }
    else if (v == &move_heap.v_null) {
        retv = &inst->org_vrt_heap->v_null;
        v->refc--;
        retv->refc++;
    }
    else if (v == &move_heap.v_false) {
        retv = &inst->org_vrt_heap->v_false;
        v->refc--;
        retv->refc++;
    }
    else if (v == &move_heap.v_true) {
        retv = &inst->org_vrt_heap->v_true;
        v->refc--;
        retv->refc++;
    }
    else {
        retv = v;

        /* change the stat info */
        if (IS_CONTAINER(v->type) ||
                ((v->type == PURC_VARIANT_TYPE_STRING ||
                  v->type == PURC_VARIANT_TYPE_BSEQUENCE) &&
                 (v->flags & PCVARIANT_FLAG_EXTRA_SIZE))) {
            inst->org_vrt_heap->stat.sz_mem[v->type] += v->sz_ptr[0];
            inst->org_vrt_heap->stat.sz_total_mem += v->sz_ptr[0];

            move_heap.stat.sz_mem[v->type] -= v->sz_ptr[0];
            move_heap.stat.sz_total_mem -= v->sz_ptr[0];
        }

        inst->org_vrt_heap->stat.nr_values[v->type]++;
        inst->org_vrt_heap->stat.nr_total_values++;
        move_heap.stat.nr_values[v->type]--;
        move_heap.stat.nr_total_values--;

        inst->org_vrt_heap->stat.sz_mem[v->type] += sizeof(purc_variant);
        inst->org_vrt_heap->stat.sz_total_mem += sizeof(purc_variant);
        move_heap.stat.sz_mem[v->type] -= sizeof(purc_variant);
        move_heap.stat.sz_total_mem -= sizeof(purc_variant);
    }
    pcvariant_use_norm_heap();

    return retv;
}

void pcvariant_use_move_heap(void)
{
    struct pcinst *inst = pcinst_current();
    purc_mutex_lock(&mh_lock);
    inst->variant_heap = &move_heap;
}

void pcvariant_use_norm_heap(void)
{
    struct pcinst *inst = pcinst_current();
    inst->variant_heap = inst->org_vrt_heap;
    purc_mutex_unlock(&mh_lock);
}

