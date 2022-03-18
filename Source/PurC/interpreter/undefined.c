/**
 * @file undefined.c
 * @author Xu Xiaohong
 * @date 2021/12/06
 * @brief
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
 *
 */

#include "purc.h"

#include "internal.h"

#include "private/debug.h"
#include "private/runloop.h"

#include "ops.h"

#include <pthread.h>
#include <unistd.h>

struct ctxt_for_undefined {
    struct pcvdom_node           *curr;
    purc_variant_t                href;
};

static void
ctxt_for_undefined_destroy(struct ctxt_for_undefined *ctxt)
{
    if (ctxt) {
        PURC_VARIANT_SAFE_CLEAR(ctxt->href);
        free(ctxt);
    }
}

static void
ctxt_destroy(void *ctxt)
{
    ctxt_for_undefined_destroy((struct ctxt_for_undefined*)ctxt);
}

static int
process_attr_href(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val)
{
    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)frame->ctxt;
    if (ctxt->href != PURC_VARIANT_INVALID) {
        purc_set_error_with_info(PURC_ERROR_DUPLICATED,
                "vdom attribute '%s' for element <%s>",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    if (val == PURC_VARIANT_INVALID) {
        purc_set_error_with_info(PURC_ERROR_INVALID_VALUE,
                "vdom attribute '%s' for element <%s> undefined",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    ctxt->href = val;
    purc_variant_ref(val);

    return 0;
}

static int
attr_found_val(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val,
        struct pcvdom_attr *attr,
        void *ud)
{
    UNUSED_PARAM(ud);

    PC_ASSERT(attr->op == PCHVML_ATTRIBUTE_OPERATOR);
    PC_ASSERT(attr->key);
    const char *sv = "";

    if (purc_variant_is_string(val)) {
        sv = purc_variant_get_string_const(val);
        PC_ASSERT(sv);
    }
    else if (purc_variant_is_undefined(val)) {
        /* no action to take */
    }
    else {
        PC_ASSERT(0);
    }

    int r = pcintr_util_set_attribute(frame->edom_element, attr->key, sv);
    PC_ASSERT(r == 0);

    if (name) {
        if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, HREF)) == name) {
            return process_attr_href(frame, element, name, val);
        }
        if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, TYPE)) == name) {
            return 0;
        }
        PC_DEBUGX("name: %s", purc_atom_to_string(name));
        PC_ASSERT(0);
        return -1;
    }

    return 0;
}

static int
attr_found(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name,
        struct pcvdom_attr *attr,
        void *ud)
{
    PC_ASSERT(attr->op == PCHVML_ATTRIBUTE_OPERATOR);

    purc_variant_t val = pcintr_eval_vdom_attr(pcintr_get_stack(), attr);
    if (val == PURC_VARIANT_INVALID)
        return -1;

    int r = attr_found_val(frame, element, name, val, attr, ud);
    purc_variant_unref(val);

    return r ? -1 : 0;
}

static void*
after_pushed(pcintr_stack_t stack, pcvdom_element_t pos)
{
    PC_ASSERT(stack && pos);
    PC_ASSERT(stack == pcintr_get_stack());
    switch (stack->mode) {
        case STACK_VDOM_BEFORE_HVML:
            PC_ASSERT(0);
            break;
        case STACK_VDOM_BEFORE_HEAD:
            stack->mode = STACK_VDOM_IN_BODY;
            break;
        case STACK_VDOM_IN_HEAD:
            break;
        case STACK_VDOM_AFTER_HEAD:
            stack->mode = STACK_VDOM_IN_BODY;
            break;
        case STACK_VDOM_IN_BODY:
            break;
        case STACK_VDOM_AFTER_BODY:
            PC_ASSERT(0);
            break;
        case STACK_VDOM_AFTER_HVML:
            PC_ASSERT(0);
            break;
        default:
            PC_ASSERT(0);
            break;
    }

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame);

    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)calloc(1, sizeof(*ctxt));
    if (!ctxt) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    frame->ctxt = ctxt;
    frame->ctxt_destroy = ctxt_destroy;

    frame->pos = pos; // ATTENTION!!

    struct pcvdom_element *element = frame->pos;
    PC_ASSERT(element);

    PC_ASSERT(frame->edom_element);
    pcdom_element_t *child;
    child = pcintr_util_append_element(frame->edom_element,
            frame->pos->tag_name);
    PC_ASSERT(child);
    frame->edom_element = child;
    int r;
    r = pcintr_refresh_at_var(frame);
    if (r)
        return NULL;

    r = pcintr_vdom_walk_attrs(frame, element, NULL, attr_found);
    if (r)
        return NULL;

#if 1
    // FIXME
    // base tag, set base uri
    if (strcmp(element->tag_name, "base") == 0) {
        purc_variant_t href;
        href = ctxt->href;
        if (href != PURC_VARIANT_INVALID && purc_variant_is_string(href)) {
            const char* base_url = purc_variant_get_string_const(href);
            fprintf(stderr, "base_url: [%s]\n", base_url);
            pcintr_set_base_uri(stack, base_url);
        }
    }
#endif

    purc_variant_t with = frame->ctnt_var;
    if (with != PURC_VARIANT_INVALID) {
        // FIXME: unify
        PC_ASSERT(purc_variant_is_type(with, PURC_VARIANT_TYPE_ULONGINT));
        bool ok;
        uint64_t u64;
        ok = purc_variant_cast_to_ulongint(with, &u64, false);
        PC_ASSERT(ok);
        struct pcvcm_node *vcm_content;
        vcm_content = (struct pcvcm_node*)u64;
        PC_ASSERT(vcm_content);

        purc_variant_t v = pcvcm_eval(vcm_content, stack, frame->silently);
        PC_ASSERT(v != PURC_VARIANT_INVALID);
        if (purc_variant_is_string(v)) {
            const char *sv = purc_variant_get_string_const(v);
            int r = pcintr_util_set_child_chunk(frame->edom_element, sv);
            PC_ASSERT(r == 0);
        }
        else {
            char *sv;
            int r;
            r = purc_variant_stringify_alloc(&sv, v);
            PC_ASSERT(r >= 0 && sv);
            r = pcintr_util_set_child_chunk(frame->edom_element, sv);
            PC_ASSERT(r == 0);
            free(sv);
        }
        purc_variant_unref(v);
    }

    purc_clr_error();

    return ctxt;
}

static bool
on_popping(pcintr_stack_t stack, void* ud)
{
    PC_ASSERT(stack);
    PC_ASSERT(stack == pcintr_get_stack());

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame);
    PC_ASSERT(ud == frame->ctxt);

    struct pcvdom_element *element = frame->pos;
    PC_ASSERT(element);

    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)frame->ctxt;
    if (ctxt) {
        ctxt_for_undefined_destroy(ctxt);
        frame->ctxt = NULL;
    }

    return true;
}

static void
on_element(pcintr_coroutine_t co, struct pcintr_stack_frame *frame,
        struct pcvdom_element *element)
{
    UNUSED_PARAM(co);
    UNUSED_PARAM(frame);
    UNUSED_PARAM(element);
}

static void
on_content(pcintr_coroutine_t co, struct pcintr_stack_frame *frame,
        struct pcvdom_content *content)
{
    UNUSED_PARAM(co);
    UNUSED_PARAM(frame);
    PC_ASSERT(content);
    // int r;
    struct pcvcm_node *vcm = content->vcm;
    if (!vcm)
        return;

    pcintr_stack_t stack = pcintr_get_stack();
    purc_variant_t v = pcvcm_eval(vcm, stack, frame->silently);
    PC_ASSERT(v != PURC_VARIANT_INVALID);
    purc_clr_error();

    if (purc_variant_is_string(v)) {
        const char *text = purc_variant_get_string_const(v);
        pcdom_text_t *content;
        content = pcintr_util_append_content(frame->edom_element, text);
        PC_ASSERT(content);
        purc_variant_unref(v);
    }
    else {
        char *sv;
        int r;
        r = purc_variant_stringify_alloc(&sv, v);
        PC_ASSERT(r >= 0 && sv);
        r = pcintr_util_add_child_chunk(frame->edom_element, sv);
        PC_ASSERT(r == 0);
        free(sv);
        purc_variant_unref(v);
    }
}

static void
on_comment(pcintr_coroutine_t co, struct pcintr_stack_frame *frame,
        struct pcvdom_comment *comment)
{
    UNUSED_PARAM(co);
    UNUSED_PARAM(frame);
    PC_ASSERT(comment);
}

static pcvdom_element_t
select_child(pcintr_stack_t stack, void* ud)
{
                PC_ASSERT(stack->except == 0);
    PC_ASSERT(stack);
    PC_ASSERT(stack == pcintr_get_stack());

    pcintr_coroutine_t co = &stack->co;
    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(ud == frame->ctxt);

    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)frame->ctxt;

    struct pcvdom_node *curr;

again:
    curr = ctxt->curr;

    if (curr == NULL) {
        struct pcvdom_element *element = frame->pos;
        struct pcvdom_node *node = &element->node;
        node = pcvdom_node_first_child(node);
        curr = node;
    }
    else {
        curr = pcvdom_node_next_sibling(curr);
        purc_clr_error();
    }

    ctxt->curr = curr;

    if (curr == NULL) {
        purc_clr_error();
        return NULL;
    }

    switch (curr->type) {
        case PCVDOM_NODE_DOCUMENT:
            PC_ASSERT(0); // Not implemented yet
            break;
        case PCVDOM_NODE_ELEMENT:
            {
                pcvdom_element_t element = PCVDOM_ELEMENT_FROM_NODE(curr);
                on_element(co, frame, element);
                PC_ASSERT(stack->except == 0);
                return element;
            }
        case PCVDOM_NODE_CONTENT:
            on_content(co, frame, PCVDOM_CONTENT_FROM_NODE(curr));
                PC_ASSERT(stack->except == 0);
            goto again;
        case PCVDOM_NODE_COMMENT:
            on_comment(co, frame, PCVDOM_COMMENT_FROM_NODE(curr));
                PC_ASSERT(stack->except == 0);
            goto again;
        default:
            PC_ASSERT(0); // Not implemented yet
    }

    PC_ASSERT(0);
    return NULL; // NOTE: never reached here!!!
}

static struct pcintr_element_ops
ops = {
    .after_pushed       = after_pushed,
    .on_popping         = on_popping,
    .rerun              = NULL,
    .select_child       = select_child,
};

struct pcintr_element_ops* pcintr_get_undefined_ops(void)
{
    return &ops;
}

