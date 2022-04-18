/**
 * @file back.c
 * @author Xu Xiaohong
 * @date 2022/04/07
 * @brief The ops for <back>
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
#include "purc-runloop.h"

#include "ops.h"

#include "purc-executor.h"

#include <pthread.h>
#include <unistd.h>

struct ctxt_for_back {
    struct pcintr_stack_frame         *back_anchor;
    purc_variant_t      with;
};

static void
ctxt_for_back_destroy(struct ctxt_for_back *ctxt)
{
    if (ctxt) {
        PURC_VARIANT_SAFE_CLEAR(ctxt->with);

        free(ctxt);
    }
}

static void
ctxt_destroy(void *ctxt)
{
    ctxt_for_back_destroy((struct ctxt_for_back*)ctxt);
}

static int
post_process_data(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    UNUSED_PARAM(co);

    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)frame->ctxt;
    PC_ASSERT(ctxt);

    if (ctxt->back_anchor == NULL) {
        purc_set_error_with_info(PURC_EXCEPT_ENTITY_NOT_FOUND,
                "vdom attribute 'to' for element <back> undefined");
        return -1;
    }

    PC_ASSERT(co->stack->back_anchor == NULL);

    if (ctxt->with != PURC_VARIANT_INVALID) {
        if (pcintr_set_question_var(ctxt->back_anchor, ctxt->with))
            return -1;
    }

    co->stack->back_anchor = ctxt->back_anchor;

    return 0;
}

static int
post_process(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    UNUSED_PARAM(co);

    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)frame->ctxt;
    PC_ASSERT(ctxt);

    int r = post_process_data(co, frame);
    if (r)
        return r;

    return 0;
}

static int
process_back_level(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, uint64_t back_level)
{
    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)frame->ctxt;

    PC_ASSERT(back_level > 0);
    PC_ASSERT(ctxt->back_anchor == NULL);

    struct pcintr_stack_frame *p = frame;
    while (frame && back_level > 0) {
        p = pcintr_stack_frame_get_parent(p);
        back_level -= 1;
    }

    if (p == NULL) {
        purc_set_error_with_info(PURC_EXCEPT_ENTITY_NOT_FOUND,
                "vdom attribute '%s' for element <%s>",
                purc_atom_to_string(name), element->tag_name);
        return -1;
    }

    ctxt->back_anchor = p;
    return 0;
}

static int
process_attr_to(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val)
{
    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)frame->ctxt;
    if (ctxt->back_anchor) {
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

    if (purc_variant_is_string(val)) {
        const char *s_to = purc_variant_get_string_const(val);
        if (s_to[0] == '#') {
            purc_set_error_with_info(PURC_EXCEPT_NOT_IMPLEMENTED,
                    "<%s to = %s>", element->tag_name, s_to);
            return -1;
        }

        if (s_to[0] == '_') {
            if (strcmp(s_to+1, "parent") == 0) {
                return process_back_level(frame, element, name, 1);
            }
            else if (strcmp(s_to+1, "grandparent") == 0) {
                return process_back_level(frame, element, name, 2);
            }
            else if (strcmp(s_to+1, "ancestor") == 0) {
                return process_back_level(frame, element, name, 3);
            }
            else {
                purc_set_error_with_info(PURC_EXCEPT_NOT_IMPLEMENTED,
                        "<%s to = %s>", element->tag_name, s_to);
                return -1;
            }
        }
    }
    else if (purc_variant_is_ulongint(val)) {
        return process_back_level(frame, element, name, val->u64);
    }
    else if (purc_variant_is_longint(val)) {
        int64_t back_level = val->i64;
        if (back_level <= 0) {
            purc_set_error_with_info(PURC_EXCEPT_INVALID_VALUE,
                    "<%s to = %zd>", element->tag_name, back_level);
            return -1;
        }
        return process_back_level(frame, element, name, (uint64_t)back_level);
    }

    purc_set_error_with_info(PURC_EXCEPT_NOT_IMPLEMENTED,
            "<%s to = ...>", element->tag_name);
    return -1;
}

static int
process_attr_with(struct pcintr_stack_frame *frame,
        struct pcvdom_element *element,
        purc_atom_t name, purc_variant_t val)
{
    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)frame->ctxt;
    if (ctxt->with != PURC_VARIANT_INVALID) {
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

    ctxt->with = val;
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

    PC_ASSERT(name);
    PC_ASSERT(attr->op == PCHVML_ATTRIBUTE_OPERATOR);

    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, TO)) == name) {
        return process_attr_to(frame, element, name, val);
    }

    if (pchvml_keyword(PCHVML_KEYWORD_ENUM(HVML, WITH)) == name) {
        return process_attr_with(frame, element, name, val);
    }

    purc_set_error_with_info(PURC_EXCEPT_NOT_IMPLEMENTED,
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

    int r = pcintr_check_insertion_mode_for_normal_element(stack);
    PC_ASSERT(r == 0);

    struct pcintr_stack_frame *frame;
    frame = pcintr_stack_get_bottom_frame(stack);
    PC_ASSERT(frame);

    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)calloc(1, sizeof(*ctxt));
    if (!ctxt) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    frame->ctxt = ctxt;
    frame->ctxt_destroy = ctxt_destroy;

    frame->pos = pos; // ATTENTION!!

    struct pcvdom_element *element = frame->pos;
    PC_ASSERT(element);

    r = pcintr_vdom_walk_attrs(frame, element, NULL, attr_found);
    if (r)
        return NULL;

    r = post_process(&stack->co, frame);
    if (r)
        return NULL;

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

    if (frame->ctxt == NULL)
        return true;

    struct pcvdom_element *element = frame->pos;
    PC_ASSERT(element);

    struct ctxt_for_back *ctxt;
    ctxt = (struct ctxt_for_back*)frame->ctxt;
    if (ctxt) {
        ctxt_for_back_destroy(ctxt);
        frame->ctxt = NULL;
    }

    return true;
}

static struct pcintr_element_ops
ops = {
    .after_pushed       = after_pushed,
    .on_popping         = on_popping,
    .rerun              = NULL,
    .select_child       = NULL,
};

struct pcintr_element_ops* pcintr_get_back_ops(void)
{
    return &ops;
}
