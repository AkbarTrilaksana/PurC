/**
 * @file iterate.c
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

#include "../internal.h"

#include "private/debug.h"
#include "private/executor.h"
#include "purc-runloop.h"

#include "../ops.h"

#include "purc-executor.h"

#include <pthread.h>
#include <unistd.h>

struct ctxt_for_iterate {
    struct pcvdom_node           *curr;

    purc_variant_t                on;

    struct pcvdom_attr           *onlyif_attr;
    struct pcvdom_attr           *while_attr;
    struct pcvdom_attr           *with_attr;

    struct pcvdom_attr           *rule_attr;
    purc_variant_t                with;

    struct purc_exec_ops          ops;
    purc_exec_inst_t              exec_inst;
    purc_exec_iter_t              it;

    unsigned int                  stop:1;
    unsigned int                  with_set:1;
    unsigned int                  by_set:1;
    unsigned int                  nosetotail:1;
};

static void
ctxt_for_iterate_destroy(struct ctxt_for_iterate *ctxt)
{
    if (ctxt) {
        if (ctxt->exec_inst) {
            bool ok = ctxt->ops.destroy(ctxt->exec_inst);
            PC_ASSERT(ok);
            ctxt->exec_inst = NULL;
        }
        PURC_VARIANT_SAFE_CLEAR(ctxt->on);
        PURC_VARIANT_SAFE_CLEAR(ctxt->with);

        free(ctxt);
    }
}

static void
ctxt_destroy(void *ctxt)
{
    ctxt_for_iterate_destroy((struct ctxt_for_iterate*)ctxt);
}

static bool
check_stop(purc_variant_t val)
{
    if (purc_variant_is_undefined(val))
        return true;

    if (purc_variant_is_null(val))
        return true;

    if (purc_variant_is_boolean(val)) {
        if (pcvariant_is_false(val)) {
            return true;
        }
    }

    return false;
}

static int
check_onlyif(struct pcvdom_attr *onlyif, bool *stop)
{
    purc_variant_t val;
    val = pcintr_eval_vdom_attr(pcintr_get_stack(), onlyif);
    PC_ASSERT(val != PURC_VARIANT_INVALID);
    if (val == PURC_VARIANT_INVALID)
        return -1;

    int64_t i64;
    bool force = true;
    bool ok;
    ok = purc_variant_cast_to_longint(val, &i64, force);

    PURC_VARIANT_SAFE_CLEAR(val);
    if (!ok)
        return -1;

    *stop = i64 ? false : true;

    return 0;
}

static int
check_while(struct pcvdom_attr *_while, bool *stop)
{
    purc_variant_t val;
    val = pcintr_eval_vdom_attr(pcintr_get_stack(), _while);
    if (val == PURC_VARIANT_INVALID)
        return -1;

    int64_t i64;
    bool force = false;
    bool ok;
    ok = purc_variant_cast_to_longint(val, &i64, force);
    PURC_VARIANT_SAFE_CLEAR(val);
    if (!ok)
        return -1;

    *stop = i64 ? false : true;

    return 0;
}

static int
re_eval_with(struct pcintr_stack_frame *frame,
        struct pcvdom_attr *with, bool *stop)
{
    purc_variant_t val;
    val = pcintr_eval_vdom_attr(pcintr_get_stack(), with);
    PC_ASSERT(val != PURC_VARIANT_INVALID);
    if (val == PURC_VARIANT_INVALID)
        return -1;

    if (check_stop(val)) {
        *stop = true;
        PC_ASSERT(0);
        return 0;
    }

    *stop = false;

    int r;
    r = pcintr_set_question_var(frame, val);
    purc_variant_unref(val);

    return r ? -1 : 0;
}

static int
post_process(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    UNUSED_PARAM(co);
    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    PC_ASSERT(ctxt);

    PC_ASSERT(ctxt->by_set == 0);

    purc_variant_t on;
    on = ctxt->on;
    if (on == PURC_VARIANT_INVALID) {
        purc_set_error_with_info(PURC_ERROR_ARGUMENT_MISSED,
                "lack of vdom attribute 'on' for element <iterate>");
        return -1;
    }

    if (ctxt->onlyif_attr) {
        bool stop;
        int r = check_onlyif(ctxt->onlyif_attr, &stop);
        PC_ASSERT(r == 0);

        if (stop) {
            ctxt->stop = 1;
            return 0;
        }
    }

    if (ctxt->with_attr == NULL) {
        ctxt->stop = 1;
        PC_ASSERT(0);
        return 0;
    }

    bool stop;

    int r;
    r = re_eval_with(frame, ctxt->with_attr, &stop);
    PC_ASSERT(r == 0);

    if (stop) {
        ctxt->stop = 1;
        PC_ASSERT(0);
        return 0;
    }

    return 0;
}

static int
post_process_by_rule(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    UNUSED_PARAM(co);
    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    PC_ASSERT(ctxt);

    purc_variant_t on;
    on = ctxt->on;
    if (on == PURC_VARIANT_INVALID) {
        purc_set_error_with_info(PURC_ERROR_ARGUMENT_MISSED,
                "lack of vdom attribute 'on' for element <iterate>");
        return -1;
    }

    purc_variant_t with;
    if (ctxt->with_attr) {
        with = pcintr_eval_vdom_attr(pcintr_get_stack(), ctxt->with_attr);
    }
    else {
        with = purc_variant_make_undefined();
    }

    if (with == PURC_VARIANT_INVALID)
        return -1;

    PURC_VARIANT_SAFE_CLEAR(ctxt->with);
    ctxt->with = with;

    purc_variant_t val = PURC_VARIANT_INVALID;

    const char *rule = "RANGE: FROM 0";
    if (ctxt->rule_attr) {
        val = pcintr_eval_vdom_attr(pcintr_get_stack(), ctxt->rule_attr);
        if (val == PURC_VARIANT_INVALID)
            return -1;
        rule = purc_variant_get_string_const(val);
        if (!rule) {
            purc_variant_unref(val);
            return -1;
        }
    }

    bool ok = purc_get_executor(rule, &ctxt->ops);

    if (!ok) {
        PURC_VARIANT_SAFE_CLEAR(val);
        return -1;
    }

    PC_ASSERT(ctxt->ops.create);
    PC_ASSERT(ctxt->ops.it_begin);
    PC_ASSERT(ctxt->ops.it_next);
    PC_ASSERT(ctxt->ops.it_value);
    PC_ASSERT(ctxt->ops.destroy);

    purc_exec_inst_t exec_inst;
    exec_inst = ctxt->ops.create(PURC_EXEC_TYPE_ITERATE, on, false);
    if (!exec_inst) {
        PURC_VARIANT_SAFE_CLEAR(val);
        return -1;
    }

    exec_inst->with = with;

    ctxt->exec_inst = exec_inst;

    purc_exec_iter_t it;
    it = ctxt->ops.it_begin(exec_inst, rule);
    PURC_VARIANT_SAFE_CLEAR(val);
    if (!it)
        return -1;

    ctxt->it = it;

    purc_variant_t value;
    value = ctxt->ops.it_value(exec_inst, it);
    if (value == PURC_VARIANT_INVALID)
        return -1;

    int r;
    r = pcintr_set_question_var(frame, value);

    return r ? -1 : 0;
}

static int
process_attr_on(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val)
{
    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    if (ctxt->on != PURC_VARIANT_INVALID) {
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
    ctxt->on = val;
    purc_variant_ref(val);

    pcintr_set_input_var(pcintr_get_stack(), val);

    return 0;
}

static int
process_attr_by(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val, struct pcvdom_attr *attr)
{
    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    if (ctxt->with_set) {
        purc_set_error_with_info(PURC_ERROR_NOT_SUPPORTED,
                "vdom attribute '%s' for element <%s> conflicts with"
                "vdom attribute 'onlyif/while/with'",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    if (ctxt->rule_attr) {
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
    ctxt->rule_attr = attr;
    ctxt->by_set = 1;

    return 0;
}

static int
process_attr_onlyif(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val, struct pcvdom_attr *attr)
{
    UNUSED_PARAM(val);

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    if (ctxt->by_set) {
        purc_set_error_with_info(PURC_ERROR_NOT_SUPPORTED,
                "vdom attribute '%s' for element <%s> conflicts with"
                "vdom attribute 'by'",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    if (ctxt->onlyif_attr) {
        purc_set_error_with_info(PURC_ERROR_DUPLICATED,
                "vdom attribute '%s' for element <%s>",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    ctxt->onlyif_attr = attr;
    ctxt->with_set = 1;

    return 0;
}

static int
process_attr_while(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val, struct pcvdom_attr *attr)
{
    UNUSED_PARAM(val);

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    if (ctxt->by_set) {
        purc_set_error_with_info(PURC_ERROR_NOT_SUPPORTED,
                "vdom attribute '%s' for element <%s> conflicts with"
                "vdom attribute 'by'",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    if (ctxt->while_attr) {
        purc_set_error_with_info(PURC_ERROR_DUPLICATED,
                "vdom attribute '%s' for element <%s>",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }

    ctxt->while_attr = attr;
    ctxt->with_set = 1;

    return 0;
}

static int
process_attr_with(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val, struct pcvdom_attr *attr)
{
    UNUSED_PARAM(val);

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;
    if (ctxt->with_attr) {
        purc_set_error_with_info(PURC_ERROR_DUPLICATED,
                "vdom attribute '%s' for element <%s>",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }
    ctxt->with_attr = attr;
    ctxt->with_set = 1;

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

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;

    PC_ASSERT(name);
    PC_ASSERT(attr->op == PCHVML_ATTRIBUTE_OPERATOR);

    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, ON)) == name) {
        return process_attr_on(frame, element, name, val);
    }
    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, BY)) == name) {
        return process_attr_by(frame, element, name, val, attr);
    }
    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, ONLYIF)) == name) {
        return process_attr_onlyif(frame, element, name, val, attr);
    }
    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, WHILE)) == name) {
        return process_attr_while(frame, element, name, val, attr);
    }
    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, WITH)) == name) {
        return process_attr_with(frame, element, name, val, attr);
    }
    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, NOSETOTAIL)) == name) {
        ctxt->nosetotail = 1;
        return 0;
    }

    purc_set_error_with_info(PURC_ERROR_NOT_IMPLEMENTED,
            "vdom attribute '%s' for element <%s>",
            purc_atom_to_string(name), element->tag_name);

    return -1;
}

static int
attr_found(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name,
        struct pcvdom_attr *attr,
        void *ud)
{
    PC_ASSERT(name);
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

    if (stack->except)
        return NULL;

    pcintr_check_insertion_mode_for_normal_element(stack);

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)calloc(1, sizeof(*ctxt));
    if (!ctxt) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    frame->ctxt = ctxt;
    frame->ctxt_destroy = ctxt_destroy;

    frame->pos = pos; // ATTENTION!!

    frame->attr_vars = purc_variant_make_object(0,
            PURC_VARIANT_INVALID, PURC_VARIANT_INVALID);
    if (frame->attr_vars == PURC_VARIANT_INVALID)
        return ctxt;

    struct pcvdom_element *element = frame->pos;
    PC_ASSERT(element);

    int r;
    r = pcintr_vdom_walk_attrs(frame, element, NULL, attr_found);
    if (r)
        return ctxt;

    purc_clr_error();

    if (ctxt->by_set == 0 && ctxt->with_set == 0) {
        ctxt->by_set = 1;
    }

    if (ctxt->by_set) {
        r = post_process_by_rule(stack->co, frame);
        if (r)
            return ctxt;
    }
    else {
        r = post_process(stack->co, frame);
    }

    return ctxt;
}

static bool
on_popping_with(pcintr_stack_t stack)
{
    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame);

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;

    if (ctxt->stop)
        return true;

    if (ctxt->while_attr) {
        bool stop;
        int r = check_while(ctxt->while_attr, &stop);
        PC_ASSERT(r == 0);

        if (stop) {
            ctxt->stop = 1;
            return true;
        }
    }

    PC_ASSERT(ctxt->with_attr);

    int r = pcintr_inc_percent_var(frame);
    PC_ASSERT(r == 0);

    return false;
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

    if (frame->ctxt == NULL)
        return true;

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;

    if (!ctxt->by_set) {
        return on_popping_with(stack);
    }

    purc_exec_inst_t exec_inst;
    exec_inst = ctxt->exec_inst;
    if (!exec_inst)
        return true;

    purc_exec_iter_t it = ctxt->it;
    if (!it)
        return true;

    purc_variant_t val = PURC_VARIANT_INVALID;
    const char *rule = NULL;
    if (ctxt->rule_attr) {
        val = pcintr_eval_vdom_attr(pcintr_get_stack(), ctxt->rule_attr);
        if (val == PURC_VARIANT_INVALID)
            return -1;
        rule = purc_variant_get_string_const(val);
        if (!rule) {
            purc_variant_unref(val);
            return -1;
        }
    }

    it = ctxt->ops.it_next(exec_inst, it, rule);
    PURC_VARIANT_SAFE_CLEAR(val);

    ctxt->it = it;
    if (!it) {
        int err = purc_get_last_error();
        if (err == PURC_ERROR_NOT_EXISTS) {
            purc_clr_error();
        }
        return true;
    }

    return false;
}

static bool
rerun_with(pcintr_stack_t stack)
{
    PC_ASSERT(stack);
    PC_ASSERT(stack == pcintr_get_stack());

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame);

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;

    if (ctxt->with_attr == NULL) {
        ctxt->stop = 1;
        PC_ASSERT(0);
        return false;
    }

    bool stop;

    int r;
    if (ctxt->nosetotail) {
        purc_variant_t v = pcintr_get_question_var(frame);
        pcintr_set_input_var(stack, v);
    }

    if (ctxt->onlyif_attr) {
        bool stop;
        int r = check_onlyif(ctxt->onlyif_attr, &stop);
        PC_ASSERT(r == 0);

        if (stop) {
            ctxt->stop = 1;
            return true;
        }
    }

    r = re_eval_with(frame, ctxt->with_attr, &stop);
    PC_ASSERT(r == 0);

    if (stop) {
        ctxt->stop = 1;
        PC_ASSERT(0);
        return false;
    }

    return true;
}

static bool
rerun(pcintr_stack_t stack, void* ud)
{
    PC_ASSERT(stack);
    PC_ASSERT(stack == pcintr_get_stack());

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame);
    PC_ASSERT(ud == frame->ctxt);

    if (frame->ctxt == NULL)
        return false;

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;

    if (ctxt->with_set) {
        return rerun_with(stack);
    }

    purc_exec_inst_t exec_inst;
    exec_inst = ctxt->exec_inst;
    PC_ASSERT(exec_inst);

    int r;
    r = pcintr_inc_percent_var(frame);
    if (r)
        return false;

    purc_exec_iter_t it = ctxt->it;
    PC_ASSERT(it);

    purc_variant_t value;
    value = ctxt->ops.it_value(exec_inst, it);
    if (value == PURC_VARIANT_INVALID)
        return false;

    r = pcintr_set_question_var(frame, value);
    if (r == 0) {
        pcintr_set_input_var(pcintr_get_stack(), value);
    }

    return r ? false : true;
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
    PC_ASSERT(stack);
    PC_ASSERT(stack == pcintr_get_stack());

    pcintr_coroutine_t co = stack->co;
    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(ud == frame->ctxt);

    if (stack->back_anchor == frame)
        stack->back_anchor = NULL;

    if (frame->ctxt == NULL)
        return NULL;

    if (stack->back_anchor)
        return NULL;

    struct ctxt_for_iterate *ctxt;
    ctxt = (struct ctxt_for_iterate*)frame->ctxt;

    if (ctxt->stop)
        return NULL;

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
                return element;
            }
        case PCVDOM_NODE_CONTENT:
            on_content(co, frame, PCVDOM_CONTENT_FROM_NODE(curr));
            goto again;
        case PCVDOM_NODE_COMMENT:
            on_comment(co, frame, PCVDOM_COMMENT_FROM_NODE(curr));
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
    .rerun              = rerun,
    .select_child       = select_child,
};

struct pcintr_element_ops* pcintr_get_iterate_ops(void)
{
    return &ops;
}

