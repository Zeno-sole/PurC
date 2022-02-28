/*
 * @file parser.c
 * @author Xue Shuming
 * @date 2022/02/24
 * @brief The implementation of ejson parser.
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

#include "config.h"

#include "private/instance.h"
#include "private/errors.h"
#include "private/debug.h"
#include "private/utils.h"
#include "private/stack.h"

#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#if HAVE(GLIB)
#include <gmodule.h>
#else
#include <stdlib.h>
#endif

#define ERROR_BUF_SIZE  100
#define NR_CONSUMED_LIST_LIMIT   10

#define INVALID_CHARACTER    0xFFFFFFFF

#if HAVE(GLIB)
#define    pc_alloc(sz)   g_slice_alloc0(sz)
#define    pc_free(p)     g_slice_free1(sizeof(*p), (gpointer)p)
#else
#define    pc_alloc(sz)   calloc(1, sz)
#define    pc_free(p)     free(p)
#endif

#define PRINT_STATE(state_name)                                             \
    if (parser->enable_print_log) {                                         \
        fprintf(stderr,                                                     \
            "in %s|uc=%c|hex=0x%X|stack_is_empty=%d"                        \
            "|stack_top=%c|stack_size=%ld|vcm_node->type=%d\n",             \
            curr_state_name, character, character,                          \
            ejson_stack_is_empty(), (char)ejson_stack_top(),                \
            ejson_stack_size(),                                             \
            (parser->vcm_node != NULL ? (int)parser->vcm_node->type : -1)); \
    }

#define SET_ERR(err)    do {                                                \
    purc_variant_t exinfo = PURC_VARIANT_INVALID;                           \
    if (parser->curr_uc) {                                                  \
        char buf[ERROR_BUF_SIZE+1];                                         \
        snprintf(buf, ERROR_BUF_SIZE,                                       \
                "line=%d, column=%d, character=%c",                         \
                parser->curr_uc->line,                                      \
                parser->curr_uc->column,                                    \
                parser->curr_uc->character);                                \
        exinfo = purc_variant_make_string(buf, false);                      \
        if (parser->enable_print_log) {                                     \
            fprintf(stderr, "%s:%d|%s|%s\n", __FILE__, __LINE__, #err, buf);\
        }                                                                   \
    }                                                                       \
    purc_set_error_exinfo(err, exinfo);                                     \
} while (0)

#define ejson_stack_is_empty()  pcutils_stack_is_empty(parser->ejson_stack)
#define ejson_stack_top()  pcutils_stack_top(parser->ejson_stack)
#define ejson_stack_pop()  pcutils_stack_pop(parser->ejson_stack)
#define ejson_stack_push(c) pcutils_stack_push(parser->ejson_stack, c)
#define ejson_stack_size() pcutils_stack_size(parser->ejson_stack)
#define ejson_stack_reset() pcutils_stack_clear(parser->ejson_stack)

#define vcm_stack_is_empty() pcvcm_stack_is_empty(parser->vcm_stack)
#define vcm_stack_push(c) pcvcm_stack_push(parser->vcm_stack, c)
#define vcm_stack_pop() pcvcm_stack_pop(parser->vcm_stack)

#define BEGIN_STATE(state_name)                                             \
    case state_name:                                                        \
    {                                                                       \
        const char *curr_state_name = ""#state_name;                        \
        int curr_state = state_name;                                        \
        UNUSED_PARAM(curr_state_name);                                      \
        UNUSED_PARAM(curr_state);                                           \
        PRINT_STATE(curr_state);

#define END_STATE()                                                         \
        break;                                                              \
    }

#define ADVANCE_TO(new_state)                                               \
    do {                                                                    \
        parser->state = new_state;                                          \
        goto next_input;                                                    \
    } while (false)

#define RECONSUME_IN(new_state)                                             \
    do {                                                                    \
        parser->state = new_state;                                          \
        goto next_state;                                                    \
    } while (false)

#define SET_RETURN_STATE(new_state)                                         \
    do {                                                                    \
        parser->return_state = new_state;                                   \
    } while (false)

#define RETURN_AND_STOP_PARSE()                                             \
    do {                                                                    \
        return -1;                                                          \
    } while (false)

#define RESET_TEMP_BUFFER()                                                 \
    do {                                                                    \
        uc_buffer_reset(parser->temp_buffer);                           \
    } while (false)

#define APPEND_TO_TEMP_BUFFER(c)                                            \
    do {                                                                    \
        uc_buffer_append(parser->temp_buffer, c);                       \
    } while (false)

#define APPEND_BYTES_TO_TEMP_BUFFER(bytes, nr_bytes)                        \
    do {                                                                    \
        uc_buffer_append_bytes(parser->temp_buffer, bytes, nr_bytes);   \
    } while (false)

#define APPEND_BUFFER_TO_TEMP_BUFFER(buffer)                                \
    do {                                                                    \
        uc_buffer_append_another(parser->temp_buffer, buffer);          \
    } while (false)

#define IS_TEMP_BUFFER_EMPTY()                                              \
        uc_buffer_is_empty(parser->temp_buffer)

#define RESET_STRING_BUFFER()                                               \
    do {                                                                    \
        uc_buffer_reset(parser->string_buffer);                         \
    } while (false)

#define APPEND_TO_STRING_BUFFER(uc)                                         \
    do {                                                                    \
        uc_buffer_append(parser->string_buffer, uc);                    \
    } while (false)

#define RESET_QUOTED_COUNTER()                                              \
    do {                                                                    \
        parser->nr_quoted = 0;                                              \
    } while (false)

#define UPDATE_VCM_NODE(node)                                                  \
    do {                                                                    \
        if (node) {                                                         \
            parser->vcm_node = node;                                        \
        }                                                                   \
    } while (false)

#define RESET_VCM_NODE()                                                    \
    do {                                                                    \
        parser->vcm_node = NULL;                                            \
    } while (false)

#define RESTORE_VCM_NODE()                                                  \
    do {                                                                    \
        if (!parser->vcm_node) {                                            \
            parser->vcm_node = pcvcm_stack_pop(parser->vcm_stack);          \
        }                                                                   \
    } while (false)

#define APPEND_CHILD(parent, child)                                         \
    do {                                                                    \
        if (parent && child) {                                              \
            pctree_node_append_child((struct pctree_node*)parent,           \
                (struct pctree_node*)child);                                \
        }                                                                   \
    } while (false)

#define APPEND_AS_VCM_CHILD(node)                                           \
    do {                                                                    \
        if (parser->vcm_node) {                                             \
            pctree_node_append_child((struct pctree_node*)parser->vcm_node, \
                (struct pctree_node*)node);                                 \
        }                                                                   \
        else {                                                              \
            parser->vcm_node = node;                                        \
        }                                                                   \
    } while (false)

#define POP_AS_VCM_PARENT_AND_UPDATE_VCM()                                  \
    do {                                                                    \
        struct pcvcm_node *parent = pcvcm_stack_pop(parser->vcm_stack);     \
        struct pcvcm_node *child = parser->vcm_node;                        \
        APPEND_CHILD(parent, child);                                        \
        UPDATE_VCM_NODE(parent);                                            \
    } while (false)

enum tokenizer_state {
    FIRST_STATE = 0,

    EJSON_DATA_STATE = FIRST_STATE,
    EJSON_FINISHED_STATE,
    EJSON_CONTROL_STATE,
    EJSON_LEFT_BRACE_STATE,
    EJSON_RIGHT_BRACE_STATE,
    EJSON_LEFT_BRACKET_STATE,
    EJSON_RIGHT_BRACKET_STATE,
    EJSON_LEFT_PARENTHESIS_STATE,
    EJSON_RIGHT_PARENTHESIS_STATE,
    EJSON_DOLLAR_STATE,
    EJSON_AFTER_VALUE_STATE,
    EJSON_BEFORE_NAME_STATE,
    EJSON_AFTER_NAME_STATE,
    EJSON_NAME_UNQUOTED_STATE,
    EJSON_NAME_SINGLE_QUOTED_STATE,
    EJSON_NAME_DOUBLE_QUOTED_STATE,
    EJSON_VALUE_SINGLE_QUOTED_STATE,
    EJSON_VALUE_DOUBLE_QUOTED_STATE,
    EJSON_AFTER_VALUE_DOUBLE_QUOTED_STATE,
    EJSON_VALUE_TWO_DOUBLE_QUOTED_STATE,
    EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE,
    EJSON_KEYWORD_STATE,
    EJSON_AFTER_KEYWORD_STATE,
    EJSON_BYTE_SEQUENCE_STATE,
    EJSON_AFTER_BYTE_SEQUENCE_STATE,
    EJSON_HEX_BYTE_SEQUENCE_STATE,
    EJSON_BINARY_BYTE_SEQUENCE_STATE,
    EJSON_BASE64_BYTE_SEQUENCE_STATE,
    EJSON_VALUE_NUMBER_STATE,
    EJSON_AFTER_VALUE_NUMBER_STATE,
    EJSON_VALUE_NUMBER_INTEGER_STATE,
    EJSON_VALUE_NUMBER_FRACTION_STATE,
    EJSON_VALUE_NUMBER_EXPONENT_STATE,
    EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE,
    EJSON_VALUE_NUMBER_SUFFIX_INTEGER_STATE,
    EJSON_VALUE_NUMBER_INFINITY_STATE,
    EJSON_VALUE_NAN_STATE,
    EJSON_STRING_ESCAPE_STATE,
    EJSON_STRING_ESCAPE_FOUR_HEXADECIMAL_DIGITS_STATE,
    EJSON_JSONEE_VARIABLE_STATE,
    EJSON_JSONEE_FULL_STOP_SIGN_STATE,
    EJSON_JSONEE_KEYWORD_STATE,
    EJSON_JSONEE_STRING_STATE,
    EJSON_AFTER_JSONEE_STRING_STATE,

    LAST_STATE = EJSON_AFTER_JSONEE_STRING_STATE,
};

struct ucwrap {
    struct list_head list;
    uint32_t character;
    int line;
    int column;
    int position;
};

struct rwswrap {
    purc_rwstream_t rws;
    struct list_head reconsume_list;
    struct list_head consumed_list;
    size_t nr_consumed_list;

    struct ucwrap curr_uc;
    int line;
    int column;
    int consumed;
};

struct uc_buffer {
    uint8_t *base;
    uint8_t *here;
    uint8_t *stop;
    size_t nr_chars;
};

static inline UNUSED_FUNCTION
struct ucwrap *ucwrap_new(void)
{
    return pc_alloc(sizeof(struct ucwrap));
}

static inline UNUSED_FUNCTION
void ucwrap_destroy(struct ucwrap *uc)
{
    if (uc) {
        pc_free(uc);
    }
}

static inline UNUSED_FUNCTION
struct rwswrap *rwswrap_new(void)
{
    struct rwswrap *wrap = pc_alloc(sizeof(struct rwswrap));
    if (!wrap) {
        return NULL;
    }
    INIT_LIST_HEAD(&wrap->reconsume_list);
    INIT_LIST_HEAD(&wrap->consumed_list);
    wrap->line = 1;
    wrap->column = 0;
    wrap->consumed = 0;
    return wrap;
}

static inline UNUSED_FUNCTION
void rwswrap_set_rwstream(struct rwswrap *wrap, purc_rwstream_t rws)
{
    wrap->rws = rws;
}

static inline UNUSED_FUNCTION
struct ucwrap *rwswrap_read_from_rwstream(struct rwswrap *wrap)
{
    char c[8] = {0};
    uint32_t uc = 0;
    int nr_c = purc_rwstream_read_utf8_char(wrap->rws, c, &uc);
    if (nr_c < 0) {
        uc = INVALID_CHARACTER;
    }
    wrap->column++;
    wrap->consumed++;

    wrap->curr_uc.character = uc;
    wrap->curr_uc.line = wrap->line;
    wrap->curr_uc.column = wrap->column;
    wrap->curr_uc.position = wrap->consumed;
    if (uc == '\n') {
        wrap->line++;
        wrap->column = 0;
    }
    return &wrap->curr_uc;
}

static inline UNUSED_FUNCTION
struct ucwrap *rwswrap_read_from_reconsume_list(struct rwswrap *wrap)
{
    struct ucwrap *puc = list_entry(wrap->reconsume_list.next,
            struct ucwrap, list);
    wrap->curr_uc = *puc;
    list_del_init(&puc->list);
    ucwrap_destroy(puc);
    return &wrap->curr_uc;
}

#define print_uc_list(uc_list, tag)                                         \
    do {                                                                    \
        fprintf(stderr, "begin print %s list\n|", tag);                     \
        struct list_head *p, *n;                                            \
        list_for_each_safe(p, n, uc_list) {                                 \
            struct ucwrap *puc = list_entry(p, struct ucwrap, list);        \
            fprintf(stderr, "%c", puc->character);                          \
        }                                                                   \
        fprintf(stderr, "|\nend print %s list\n", tag);                     \
    } while(0)

#define PRINT_CONSUMED_LIST(wrap)    \
        print_uc_list(&wrap->consumed_list, "consumed")

#define PRINT_RECONSUM_LIST(wrap)    \
        print_uc_list(&wrap->reconsume_list, "reconsume")

static inline UNUSED_FUNCTION
bool rwswrap_add_consumed(struct rwswrap *wrap, struct ucwrap *uc)
{
    struct ucwrap *p = ucwrap_new();
    if (!p) {
        pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return false;
    }

    *p = *uc;
    list_add_tail(&p->list, &wrap->consumed_list);
    wrap->nr_consumed_list++;

    if (wrap->nr_consumed_list > NR_CONSUMED_LIST_LIMIT) {
        struct ucwrap *first = list_first_entry(
                &wrap->consumed_list, struct ucwrap, list);
        list_del_init(&first->list);
        ucwrap_destroy(first);
        wrap->nr_consumed_list--;
    }
    return true;
}

static inline UNUSED_FUNCTION
bool rwswrap_reconsume_last_char(struct rwswrap *wrap)
{
    if (!wrap->nr_consumed_list) {
        return true;
    }

    struct ucwrap *last = list_last_entry(
            &wrap->consumed_list, struct ucwrap, list);
    list_del_init(&last->list);
    wrap->nr_consumed_list--;

    list_add(&last->list, &wrap->reconsume_list);
    return true;
}

static inline UNUSED_FUNCTION
struct ucwrap *rwswrap_next_char(struct rwswrap *wrap)
{
    struct ucwrap *ret = NULL;
    if (list_empty(&wrap->reconsume_list)) {
        ret = rwswrap_read_from_rwstream(wrap);
    }
    else {
        ret = rwswrap_read_from_reconsume_list(wrap);
    }

    if (rwswrap_add_consumed(wrap, ret)) {
        return ret;
    }
    return NULL;
}

static inline UNUSED_FUNCTION
void rwswrap_destroy(struct rwswrap *wrap)
{
    if (wrap) {
        struct list_head *p, *n;
        list_for_each_safe(p, n, &wrap->reconsume_list) {
            struct ucwrap *puc = list_entry(p, struct ucwrap, list);
            list_del_init(&puc->list);
            ucwrap_destroy(puc);
        }
        list_for_each_safe(p, n, &wrap->consumed_list) {
            struct ucwrap *puc = list_entry(p, struct ucwrap, list);
            list_del_init(&puc->list);
            ucwrap_destroy(puc);
        }
        pc_free(wrap);
    }
}

// buffer
#define MIN_BUFFER_CAPACITY 32

static inline UNUSED_FUNCTION
size_t get_buffer_size(size_t sz)
{
    size_t sz_buf = pcutils_get_next_fibonacci_number(sz);
    return sz_buf < MIN_BUFFER_CAPACITY ? MIN_BUFFER_CAPACITY : sz_buf;
}

static inline UNUSED_FUNCTION
struct uc_buffer *uc_buffer_new(void)
{
    struct uc_buffer *buffer = (struct uc_buffer*) calloc(
            1, sizeof(struct uc_buffer));
    size_t sz_init = get_buffer_size(MIN_BUFFER_CAPACITY);
    buffer->base = (uint8_t*) calloc(1, sz_init + 1);
    buffer->here = buffer->base;
    buffer->stop = buffer->base + sz_init;
    buffer->nr_chars = 0;
    return buffer;
}

static inline UNUSED_FUNCTION
void uc_buffer_reset(struct uc_buffer *buffer)
{
    memset(buffer->base, 0, buffer->stop - buffer->base);
    buffer->here = buffer->base;
    buffer->nr_chars = 0;
}

static inline UNUSED_FUNCTION
void uc_buffer_destroy(struct uc_buffer *buffer)
{
    if (buffer) {
        free(buffer->base);
        free(buffer);
    }
}

static inline UNUSED_FUNCTION
bool uc_buffer_is_empty(struct uc_buffer *buffer)
{
    return buffer->here == buffer->base;
}

static inline UNUSED_FUNCTION
size_t uc_buffer_get_size_in_bytes(struct uc_buffer *buffer)
{
    return buffer->here - buffer->base;
}

static inline UNUSED_FUNCTION
size_t uc_buffer_get_size_in_chars(struct uc_buffer *buffer)
{
    return buffer->nr_chars;
}

static inline UNUSED_FUNCTION
const char *uc_buffer_get_bytes(struct uc_buffer *buffer)
{
    return (const char *)buffer->base;
}

static inline UNUSED_FUNCTION
bool is_utf8_leading_byte(char c)
{
    return (c & 0xC0) != 0x80;
}

static inline UNUSED_FUNCTION
uint32_t utf8_to_uint32_t(const unsigned char *utf8_char, int utf8_char_len)
{
    uint32_t wc = *((unsigned char *)(utf8_char++));
    int n = utf8_char_len;
    int t = 0;

    if (wc & 0x80) {
        wc &= (1 <<(8-n)) - 1;
        while (--n > 0) {
            t = *((unsigned char *)(utf8_char++));
            wc = (wc << 6) | (t & 0x3F);
        }
    }

    return wc;
}

static inline UNUSED_FUNCTION
void uc_buffer_append_inner(struct uc_buffer *buffer,
        const char *bytes, size_t nr_bytes)
{
    uint8_t *newpos = buffer->here + nr_bytes;
    if ( newpos > buffer->stop ) {
        size_t new_size = get_buffer_size(newpos - buffer->base);
        off_t here_offset = buffer->here - buffer->base;

        uint8_t *newbuf = (uint8_t*) realloc(buffer->base, new_size + 1);
        if (newbuf == NULL) {
            pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
            return;
        }

        buffer->base = newbuf;
        buffer->here = buffer->base + here_offset;
        buffer->stop = buffer->base + new_size;
    }

    memcpy(buffer->here, bytes, nr_bytes);
    buffer->here += nr_bytes;
    *buffer->here = 0;
}

static inline UNUSED_FUNCTION
void uc_buffer_append_bytes(struct uc_buffer *buffer,
        const char *bytes, size_t nr_bytes)
{
    uc_buffer_append_inner(buffer, bytes, nr_bytes);
    const uint8_t *p = (const uint8_t*)bytes;
    const uint8_t *end = p + nr_bytes;
    while (p != end) {
        if (is_utf8_leading_byte(*p)) {
            buffer->nr_chars++;
        }
        p++;
    }
}

static inline UNUSED_FUNCTION
size_t uc_to_utf8(uint32_t c, char *outbuf)
{
    size_t len = 0;
    int first;
    int i;

    if (c < 0x80) {
        first = 0;
        len = 1;
    }
    else if (c < 0x800) {
        first = 0xc0;
        len = 2;
    }
    else if (c < 0x10000) {
        first = 0xe0;
        len = 3;
    }
    else if (c < 0x200000) {
        first = 0xf0;
        len = 4;
    }
    else if (c < 0x4000000) {
        first = 0xf8;
        len = 5;
    }
    else {
        first = 0xfc;
        len = 6;
    }

    if (outbuf) {
        for (i = len - 1; i > 0; --i) {
            outbuf[i] = (c & 0x3f) | 0x80;
            c >>= 6;
        }
        outbuf[0] = c | first;
    }

    return len;
}

static inline UNUSED_FUNCTION
void uc_buffer_append(struct uc_buffer *buffer, uint32_t uc)
{
    char buf[8] = {0};
    size_t len = uc_to_utf8(uc, buf);
    uc_buffer_append_bytes(buffer, buf, len);
}

static inline UNUSED_FUNCTION
void uc_buffer_append_chars(struct uc_buffer *buffer,
        const uint32_t *ucs, size_t nr_ucs)
{
    for (size_t i = 0; i < nr_ucs; i++) {
        uc_buffer_append(buffer, ucs[i]);
    }
}

static inline UNUSED_FUNCTION
void uc_buffer_append_another(struct uc_buffer *buffer,
        struct uc_buffer *another)
{
    uc_buffer_append_bytes(buffer,
        uc_buffer_get_bytes(another),
        uc_buffer_get_size_in_bytes(another));
}

static inline UNUSED_FUNCTION
void uc_buffer_delete_head_chars(
        struct uc_buffer *buffer, size_t sz)
{
    uint8_t *p = buffer->base;
    size_t nr = 0;
    while (p < buffer->here && nr <= sz) {
        if (is_utf8_leading_byte(*p)) {
            nr++;
        }
        p = p + 1;
    }
    p = p - 1;
    size_t n = buffer->here - p;
    memmove(buffer->base, p, n);
    buffer->here = buffer->base + n;
    memset(buffer->here, 0, buffer->stop - buffer->here);
}

static inline UNUSED_FUNCTION
void uc_buffer_delete_tail_chars(struct uc_buffer *buffer, size_t sz)
{
    uint8_t *p = buffer->here - 1;
    while (p >= buffer->base && sz > 0) {
        if (is_utf8_leading_byte(*p)) {
            sz--;
        }
        p = p - 1;
    }
    buffer->here = p + 1;
    memset(buffer->here, 0, buffer->stop - buffer->here);
}

static inline UNUSED_FUNCTION
bool uc_buffer_end_with(struct uc_buffer *buffer,
        const char *bytes, size_t nr_bytes)
{
    size_t sz = uc_buffer_get_size_in_bytes(buffer);
    return (sz >= nr_bytes
            && memcmp(buffer->here - nr_bytes, bytes, nr_bytes) == 0);
}

static inline UNUSED_FUNCTION
bool uc_buffer_equal_to(struct uc_buffer *buffer,
        const char *bytes, size_t nr_bytes)
{
    size_t sz = uc_buffer_get_size_in_bytes(buffer);
    return (sz == nr_bytes && memcmp(buffer->base, bytes, sz) == 0);
}

static inline UNUSED_FUNCTION
uint32_t uc_buffer_get_last_char(struct uc_buffer *buffer)
{
    if (uc_buffer_is_empty(buffer)) {
        return 0;
    }

    uint8_t *p = buffer->here - 1;
    while (p >= buffer->base) {
        if (is_utf8_leading_byte(*p)) {
            break;
        }
        p = p - 1;
    }
    return utf8_to_uint32_t(p, buffer->here - p);
}

static inline UNUSED_FUNCTION
bool uc_buffer_is_int(struct uc_buffer *buffer)
{
    char *p = NULL;
    strtol((const char*)buffer->base, &p, 10);
    return (p == (char*)buffer->here);
}

static inline UNUSED_FUNCTION
bool uc_buffer_is_number(struct uc_buffer *buffer)
{
    char *p = NULL;
    strtold((const char*)buffer->base, &p);
    return (p == (const char*)buffer->here);
}

static inline UNUSED_FUNCTION
bool uc_buffer_is_whitespace(struct uc_buffer *buffer)
{
    uint8_t *p = buffer->base;
    while (p !=  buffer->here) {
        if (*p == ' ' || *p == '\x0A' || *p == '\x09' || *p == '\x0C') {
            p++;
            continue;
        }
        return false;
    }
    return true;
}

// character
#define END_OF_FILE       0
static inline UNUSED_FUNCTION
bool is_eof(uint32_t uc)
{
    return uc == END_OF_FILE;
}

static inline UNUSED_FUNCTION
bool is_whitespace(uint32_t uc)
{
    return uc == ' ' || uc == '\x0A' || uc == '\x09' || uc == '\x0C';
}

static inline UNUSED_FUNCTION
uint32_t to_ascii_lower_unchecked(uint32_t uc)
{
    return uc | 0x20;
}

static inline UNUSED_FUNCTION
bool is_ascii(uint32_t uc)
{
    return !(uc & ~0x7F);
}

static inline UNUSED_FUNCTION
bool is_ascii_lower(uint32_t uc)
{
    return uc >= 'a' && uc <= 'z';
}

static inline UNUSED_FUNCTION
bool is_ascii_upper(uint32_t uc)
{
     return uc >= 'A' && uc <= 'Z';
}

static inline UNUSED_FUNCTION
bool is_ascii_space(uint32_t uc)
{
    return uc <= ' ' && (uc == ' ' || (uc <= 0xD && uc >= 0x9));
}

static inline UNUSED_FUNCTION
bool is_ascii_digit(uint32_t uc)
{
    return uc >= '0' && uc <= '9';
}

static inline UNUSED_FUNCTION
bool is_ascii_binary_digit(uint32_t uc)
{
     return uc == '0' || uc == '1';
}

static inline UNUSED_FUNCTION
bool is_ascii_hex_digit(uint32_t uc)
{
     return is_ascii_digit(uc) || (
             to_ascii_lower_unchecked(uc) >= 'a' &&
             to_ascii_lower_unchecked(uc) <= 'f'
             );
}

static inline UNUSED_FUNCTION
bool is_ascii_upper_hex_digit(uint32_t uc)
{
     return is_ascii_digit(uc) || (uc >= 'A' && uc <= 'F');
}

static inline UNUSED_FUNCTION
bool is_ascii_lower_hex_digit(uint32_t uc)
{
     return is_ascii_digit(uc) || (uc >= 'a' && uc <= 'f');
}

static inline UNUSED_FUNCTION
bool is_ascii_octal_digit(uint32_t uc)
{
     return uc >= '0' && uc <= '7';
}

static inline UNUSED_FUNCTION
bool is_ascii_alpha(uint32_t uc)
{
    return is_ascii_lower(to_ascii_lower_unchecked(uc));
}

static inline UNUSED_FUNCTION
bool is_ascii_alpha_numeric(uint32_t uc)
{
    return is_ascii_digit(uc) || is_ascii_alpha(uc);
}

static inline UNUSED_FUNCTION
bool is_separator(uint32_t c)
{
    switch (c) {
        case '{':
        case '}':
        case '[':
        case ']':
        case '(':
        case ')':
        case ',':
        case ':':
            return true;
    }
    return false;
}

static inline UNUSED_FUNCTION
bool is_context_variable(uint32_t c)
{
    switch (c) {
        case '?':
        case '<':
        case '@':
        case '!':
        case ':':
        case '=':
        case '%':
            return true;
    }
    return false;
}

struct pcejson {
    int state;
    int return_state;
    uint32_t depth;
    uint32_t max_depth;
    uint32_t flags;

    struct ucwrap* curr_uc;
    struct rwswrap* rwswrap;
    struct uc_buffer* temp_buffer;
    struct uc_buffer* string_buffer;
    struct pcvcm_node* vcm_node;
    struct pcvcm_stack* vcm_stack;
    struct pcutils_stack* ejson_stack;
    uint32_t prev_separator;
    uint32_t nr_quoted;
    bool enable_print_log;
};

#define EJSON_MAX_DEPTH         32
#define EJSON_MIN_BUFFER_SIZE   128
#define EJSON_MAX_BUFFER_SIZE   1024 * 1024 * 1024
#define EJSON_END_OF_FILE       0
#define PRINT_LOG_SWITCH_FILE "/tmp/purc_print_ejson_parser"

struct pcejson *pcejson_create(uint32_t depth, uint32_t flags)
{
    struct pcejson* parser = (struct pcejson*) pc_alloc(
            sizeof(struct pcejson));
    if (!parser) {
        pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    parser->state = 0;
    parser->max_depth = depth;
    parser->depth = 0;
    parser->flags = flags;

    parser->curr_uc = NULL;
    parser->rwswrap = rwswrap_new();
    parser->temp_buffer = uc_buffer_new();
    parser->string_buffer = uc_buffer_new();
    parser->vcm_stack = pcvcm_stack_new();
    parser->ejson_stack = pcutils_stack_new(0);
    parser->prev_separator = 0;
    parser->nr_quoted = 0;
    struct stat st;
    parser->enable_print_log = (stat(PRINT_LOG_SWITCH_FILE, &st) == 0);

    return parser;
}

void pcejson_destroy(struct pcejson *parser)
{
    if (parser) {
        rwswrap_destroy(parser->rwswrap);
        uc_buffer_destroy(parser->temp_buffer);
        uc_buffer_destroy(parser->string_buffer);
        struct pcvcm_node* n = parser->vcm_node;
        parser->vcm_node = NULL;
        while (!pcvcm_stack_is_empty(parser->vcm_stack)) {
            struct pcvcm_node* node = pcvcm_stack_pop(parser->vcm_stack);
            pctree_node_append_child(
                    (struct pctree_node*)node, (struct pctree_node*)n);
            n = node;
        }
        pcvcm_node_destroy(n);
        pcvcm_stack_destroy(parser->vcm_stack);
        pcutils_stack_destroy(parser->ejson_stack);
        pc_free(parser);
    }
}

void pcejson_reset(struct pcejson *parser, uint32_t depth, uint32_t flags)
{
    parser->state = 0;
    parser->max_depth = depth;
    parser->depth = 0;
    parser->flags = flags;

    rwswrap_destroy(parser->rwswrap);
    parser->rwswrap = rwswrap_new();

    uc_buffer_reset(parser->temp_buffer);
    uc_buffer_reset(parser->string_buffer);

    struct pcvcm_node* n = parser->vcm_node;
    parser->vcm_node = NULL;
    while (!pcvcm_stack_is_empty(parser->vcm_stack)) {
        struct pcvcm_node *node = pcvcm_stack_pop(parser->vcm_stack);
        pctree_node_append_child(
                (struct pctree_node *)node, (struct pctree_node *)n);
        n = node;
    }
    pcvcm_node_destroy(n);
    pcvcm_stack_destroy(parser->vcm_stack);
    parser->vcm_stack = pcvcm_stack_new();
    pcutils_stack_destroy(parser->ejson_stack);
    parser->ejson_stack = pcutils_stack_new(0);
    parser->prev_separator = 0;
    parser->nr_quoted = 0;
}

static inline UNUSED_FUNCTION
bool pcejson_inc_depth (struct pcejson* parser)
{
    parser->depth++;
    return parser->depth <= parser->max_depth;
}

static inline UNUSED_FUNCTION
void pcejson_dec_depth (struct pcejson* parser)
{
    parser->depth--;
}

static UNUSED_FUNCTION
struct pcvcm_node *create_byte_sequenct(struct uc_buffer *buffer)
{
    UNUSED_PARAM(buffer);
    size_t nr_bytes = uc_buffer_get_size_in_bytes(buffer);
    const char *bytes = uc_buffer_get_bytes(buffer);
    if (bytes[1] == 'x') {
        return pcvcm_node_new_byte_sequence_from_bx(bytes + 2, nr_bytes - 2);
    }
    else if (bytes[1] == 'b') {
        return pcvcm_node_new_byte_sequence_from_bb(bytes + 2, nr_bytes - 2);
    }
    else if (bytes[1] == '6') {
        return pcvcm_node_new_byte_sequence_from_b64(bytes + 3, nr_bytes - 3);
    }
    return NULL;
}

#define PCEJSON_PARSER_BEGIN                                                \
int pcejson_parse(struct pcvcm_node **vcm_tree,                             \
        struct pcejson **parser_param,                                      \
        purc_rwstream_t rws,                                                \
        uint32_t depth)                                                     \
{                                                                           \
    if (*parser_param == NULL) {                                            \
        *parser_param = pcejson_create(                                     \
                depth > 0 ? depth : EJSON_MAX_DEPTH, 1);                    \
        if (*parser_param == NULL) {                                        \
            return -1;                                                      \
        }                                                                   \
    }                                                                       \
                                                                            \
    uint32_t character = 0;                                                 \
    struct pcejson* parser = *parser_param;                                 \
    rwswrap_set_rwstream (parser->rwswrap, rws);                            \
                                                                            \
next_input:                                                                 \
    parser->curr_uc = rwswrap_next_char (parser->rwswrap);                  \
    if (!parser->curr_uc) {                                                 \
        return -1;                                                          \
    }                                                                       \
                                                                            \
    character = parser->curr_uc->character;                                 \
    if (character == INVALID_CHARACTER) {                                   \
        SET_ERR(PURC_ERROR_BAD_ENCODING);                                   \
        return -1;                                                          \
    }                                                                       \
                                                                            \
    if (is_separator(character)) {                                          \
        if (parser->prev_separator == ',' && character == ',') {            \
            SET_ERR(PCEJSON_ERROR_UNEXPECTED_COMMA);                        \
            return -1;                                                      \
        }                                                                   \
        parser->prev_separator = character;                                 \
    }                                                                       \
    else if (!is_whitespace(character)) {                                   \
        parser->prev_separator = 0;                                         \
    }                                                                       \
                                                                            \
next_state:                                                                 \
    switch (parser->state) {

#define PCEJSON_PARSER_END                                                  \
    default:                                                                \
        break;                                                              \
    }                                                                       \
    return -1;                                                              \
}

#if 1
PCEJSON_PARSER_BEGIN

BEGIN_STATE(EJSON_DATA_STATE)
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (is_whitespace (character) || character == 0xFEFF) {
        ADVANCE_TO(EJSON_DATA_STATE);
    }
    RECONSUME_IN(EJSON_CONTROL_STATE);
END_STATE()

BEGIN_STATE(EJSON_FINISHED_STATE)
    if (!is_eof(character) && !is_whitespace(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    while (!vcm_stack_is_empty()) {
        ejson_stack_pop();
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
    }
    if (is_eof(character)  && !ejson_stack_is_empty()) {
        uint32_t uc = ejson_stack_top();
        if (uc == '{' || uc == '[' || uc == '(' || uc == ':') {
            SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
            return -1;
        }
    }
    ejson_stack_reset();
    *vcm_tree = parser->vcm_node;
    parser->vcm_node = NULL;
    return 0;
END_STATE()

BEGIN_STATE(EJSON_CONTROL_STATE)
    uint32_t uc = ejson_stack_top();
    if (is_whitespace(character)) {
        if (ejson_stack_is_empty()) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        if (uc == '"' || uc == '\'' || uc == 'U') {
            RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
        }
        ADVANCE_TO(EJSON_CONTROL_STATE);
    }
    if (character == '{') {
        RECONSUME_IN(EJSON_LEFT_BRACE_STATE);
    }
    if (character == '}') {
        if ((parser->vcm_node->type == PCVCM_NODE_TYPE_FUNC_CONCAT_STRING)
                && (uc == '"' || uc == '\'' || uc == 'U')) {
            RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
        }
        RECONSUME_IN(EJSON_RIGHT_BRACE_STATE);
    }
    if (character == '[') {
        RECONSUME_IN(EJSON_LEFT_BRACKET_STATE);
    }
    if (character == ']') {
        if (parser->vcm_node != NULL && parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_CONCAT_STRING
                && (uc == '"' || uc == '\'' || uc == 'U')) {
            RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
        }
        RECONSUME_IN(EJSON_RIGHT_BRACKET_STATE);
    }
    if (character == '<' || character == '>') {
        RECONSUME_IN(EJSON_FINISHED_STATE);
    }
    if (character == '/') {
        if (ejson_stack_is_empty() && parser->vcm_node) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
    }
    if (character == '(') {
        ADVANCE_TO(EJSON_LEFT_PARENTHESIS_STATE);
    }
    if (character == ')') {
        if (ejson_stack_is_empty() && parser->vcm_node) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        if (uc == '"' || uc == '\'' || uc == 'U') {
            RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
        }
        pcejson_dec_depth(parser);
        ADVANCE_TO(EJSON_RIGHT_PARENTHESIS_STATE);
    }
    if (character == '$') {
        RECONSUME_IN(EJSON_DOLLAR_STATE);
    }
    if (character == '"') {
        if (ejson_stack_is_empty() && parser->vcm_node) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        if (uc == '"') {
            RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
        }
        else {
            RESET_TEMP_BUFFER();
            RESET_QUOTED_COUNTER();
            RECONSUME_IN(EJSON_VALUE_DOUBLE_QUOTED_STATE);
        }
    }
    if (character == '\'') {
        RESET_TEMP_BUFFER();
        RESET_QUOTED_COUNTER();
        RECONSUME_IN(EJSON_VALUE_SINGLE_QUOTED_STATE);
    }
    if (character == 'b') {
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_BYTE_SEQUENCE_STATE);
    }
    if (character == 't' || character == 'f' || character == 'n') {
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_KEYWORD_STATE);
    }
    if (character == 'I') {
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_VALUE_NUMBER_INFINITY_STATE);
    }
    if (character == 'N') {
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_VALUE_NAN_STATE);
    }
    if (is_ascii_digit(character) || character == '-') {
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_VALUE_NUMBER_STATE);
    }
    if (is_eof(character)) {
        if (parser->vcm_node) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (character == ',') {
        if (uc == '{') {
            ejson_stack_pop();
            ADVANCE_TO(EJSON_BEFORE_NAME_STATE);
        }
        if (uc == '[' || uc == '(' || uc == '<') {
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        if (uc == ':') {
            ejson_stack_pop();
            if (!uc_buffer_is_empty(parser->temp_buffer)) {
                struct pcvcm_node *node = pcvcm_node_new_string(
                uc_buffer_get_bytes(parser->temp_buffer));
                APPEND_AS_VCM_CHILD(node);
                RESET_TEMP_BUFFER();
            }
            if (parser->vcm_node &&
                    parser->vcm_node->type != PCVCM_NODE_TYPE_OBJECT) {
                POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            }
            ADVANCE_TO(EJSON_BEFORE_NAME_STATE);
        }
        if (uc == '"') {
            RECONSUME_IN(EJSON_JSONEE_STRING_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '.') {
        RECONSUME_IN(EJSON_JSONEE_FULL_STOP_SIGN_STATE);
    }
    if (uc == '[') {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (parser->vcm_node != NULL && (parser->vcm_node->type ==
            PCVCM_NODE_TYPE_FUNC_GET_VARIABLE ||
            parser->vcm_node->type ==
            PCVCM_NODE_TYPE_FUNC_GET_ELEMENT)) {
        size_t n = pctree_node_children_number(
                (struct pctree_node*)parser->vcm_node);
        if (n < 2) {
            RECONSUME_IN(EJSON_JSONEE_VARIABLE_STATE);
        }
        else {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
    }
    if (ejson_stack_is_empty() && parser->vcm_node) {
        RECONSUME_IN(EJSON_FINISHED_STATE);
    }
    RECONSUME_IN(EJSON_JSONEE_STRING_STATE);
END_STATE()

BEGIN_STATE(EJSON_DOLLAR_STATE)
    if (is_whitespace(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        ejson_stack_push('$');
        struct pcvcm_node *snode = pcvcm_node_new_get_variable(NULL);
        UPDATE_VCM_NODE(snode);
        ADVANCE_TO(EJSON_DOLLAR_STATE);
    }
    if (character == '{') {
        ejson_stack_push('P');
        RESET_TEMP_BUFFER();
        ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
    }
    RESET_TEMP_BUFFER();
    RECONSUME_IN(EJSON_JSONEE_VARIABLE_STATE);
END_STATE()

BEGIN_STATE(EJSON_JSONEE_FULL_STOP_SIGN_STATE)
    if (character == '.' &&
        (parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_GET_VARIABLE ||
                parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_GET_ELEMENT ||
                parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_CALL_GETTER ||
                parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_CALL_SETTER
                )) {
        ejson_stack_push('.');
        struct pcvcm_node *node = pcvcm_node_new_get_element(NULL,
                NULL);
        APPEND_CHILD(node, parser->vcm_node);
        UPDATE_VCM_NODE(node);
        ADVANCE_TO(EJSON_JSONEE_KEYWORD_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_LEFT_BRACE_STATE)
    if (character == '{') {
        ejson_stack_push('P');
        ADVANCE_TO(EJSON_LEFT_BRACE_STATE);
    }
    if (character == '$') {
        RECONSUME_IN(EJSON_DOLLAR_STATE);
    }
    uint32_t uc = ejson_stack_top();
    if (uc == 'P') {
        ejson_stack_pop();
        ejson_stack_push('{');
        if (!pcejson_inc_depth(parser)) {
            SET_ERR(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
            return -1;
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *node = pcvcm_node_new_object(0, NULL);
        UPDATE_VCM_NODE(node);
        RECONSUME_IN(EJSON_BEFORE_NAME_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_RIGHT_BRACE_STATE)
    if (is_eof(character)) {
        if (parser->vcm_node) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    uint32_t uc = ejson_stack_top();
    if (character == '}') {
        if (uc == ':') {
            ejson_stack_pop();
            uc = ejson_stack_top();
        }
        if (uc == '{') {
            ejson_stack_pop();
            pcejson_dec_depth(parser);
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            if (ejson_stack_is_empty()) {
                ADVANCE_TO(EJSON_FINISHED_STATE);
            }
            ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
        }
        else if (uc == 'P') {
            ejson_stack_pop();
            if (parser->vcm_node->extra & EXTRA_PROTECT_FLAG) {
                parser->vcm_node->extra &= EXTRA_SUGAR_FLAG;
            }
            else {
                parser->vcm_node->extra &= EXTRA_PROTECT_FLAG;
            }
            // FIXME : <update from="assets/{$SYSTEM.locale}.json" />
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            if (ejson_stack_is_empty()) {
                ADVANCE_TO(EJSON_FINISHED_STATE);
            }
            ADVANCE_TO(EJSON_RIGHT_BRACE_STATE);
        }
        else if (uc == '(' || uc == '<' || uc == '"') {
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_RIGHT_BRACE);
        RETURN_AND_STOP_PARSE();
    }
    if (uc == '"') {
        RECONSUME_IN(EJSON_JSONEE_STRING_STATE);
    }
    if (is_whitespace(character)) {
        ADVANCE_TO(EJSON_RIGHT_BRACE_STATE);
    }
    if (character == ':') {
        if (uc == '{') {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            vcm_stack_push(parser->vcm_node);
            RESET_VCM_NODE();
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        if (uc == 'P') {
            ejson_stack_pop();
            ejson_stack_push('{');
            struct pcvcm_node *node = pcvcm_node_new_object(0, NULL);
            APPEND_CHILD(node, parser->vcm_node);
            vcm_stack_push(node);
            RESET_VCM_NODE();
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '.' && uc == '$') {
        ejson_stack_pop();
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
    }
    RECONSUME_IN(EJSON_CONTROL_STATE);
END_STATE()

BEGIN_STATE(EJSON_LEFT_BRACKET_STATE)
    if (character == '[') {
        if (parser->vcm_node && ejson_stack_is_empty()) {
            ejson_stack_push('[');
            struct pcvcm_node *node = pcvcm_node_new_get_element(NULL,
                    NULL);
            APPEND_CHILD(node, parser->vcm_node);
            UPDATE_VCM_NODE(node);
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        if (parser->vcm_node && (parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_GET_VARIABLE ||
                parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_GET_ELEMENT)) {
            ejson_stack_push('.');
            struct pcvcm_node *node = pcvcm_node_new_get_element(NULL,
                    NULL);
            APPEND_CHILD(node, parser->vcm_node);
            UPDATE_VCM_NODE(node);
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        uint32_t uc = ejson_stack_top();
        if (uc == '(' || uc == '<' || uc == '[' || uc == ':' || uc == 0
                || uc == '"') {
            ejson_stack_push('[');
            if (!pcejson_inc_depth(parser)) {
                SET_ERR(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
                return -1;
            }
            if (parser->vcm_node) {
                vcm_stack_push(parser->vcm_node);
            }
            struct pcvcm_node *node = pcvcm_node_new_array(0, NULL);
            UPDATE_VCM_NODE(node);
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_RIGHT_BRACKET_STATE)
    if (is_whitespace(character)) {
        ADVANCE_TO(EJSON_RIGHT_BRACKET_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    uint32_t uc = ejson_stack_top();
    if (character == ']') {
        if (uc == '.') {
            ejson_stack_pop();
            uc = ejson_stack_top();
            if (uc == '"' || uc == 'U') {
                ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
            }
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
        }
        if (uc == '[') {
            ejson_stack_pop();
            pcejson_dec_depth(parser);
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            struct pcvcm_node *parent = (struct pcvcm_node*)
                pctree_node_parent((struct pctree_node*)parser->vcm_node);
            if (parent) {
                UPDATE_VCM_NODE(parent);
            }
            if (ejson_stack_is_empty()) {
                ADVANCE_TO(EJSON_FINISHED_STATE);
            }
            ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
        }
        if (uc == '"') {
            RECONSUME_IN(EJSON_JSONEE_STRING_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_RIGHT_BRACKET);
        RETURN_AND_STOP_PARSE();
    }
    if (ejson_stack_is_empty()
            || uc == '(' || uc == '<') {
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    ADVANCE_TO(EJSON_CONTROL_STATE);
END_STATE()

BEGIN_STATE(EJSON_LEFT_PARENTHESIS_STATE)
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '!') {
        if (parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_GET_VARIABLE ||
                parser->vcm_node->type ==
                PCVCM_NODE_TYPE_FUNC_GET_ELEMENT) {
            struct pcvcm_node *node = pcvcm_node_new_call_setter(NULL,
                    0, NULL);
            APPEND_CHILD(node, parser->vcm_node);
            UPDATE_VCM_NODE(node);
            ejson_stack_push('<');
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (parser->vcm_node->type ==
            PCVCM_NODE_TYPE_FUNC_GET_VARIABLE ||
            parser->vcm_node->type ==
            PCVCM_NODE_TYPE_FUNC_GET_ELEMENT) {
        if (!pcejson_inc_depth(parser)) {
            SET_ERR(PCEJSON_ERROR_MAX_DEPTH_EXCEEDED);
            return -1;
        }
        struct pcvcm_node *node = pcvcm_node_new_call_getter(NULL,
                0, NULL);
        APPEND_CHILD(node, parser->vcm_node);
        UPDATE_VCM_NODE(node);
        ejson_stack_push('(');
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (ejson_stack_is_empty()) {
        RECONSUME_IN(EJSON_FINISHED_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_RIGHT_PARENTHESIS_STATE)
    uint32_t uc = ejson_stack_top();
    if (character == '.') {
        if (uc == '(' || uc == '<') {
            ejson_stack_pop();
            RECONSUME_IN(EJSON_CONTROL_STATE);
        }
        if (ejson_stack_is_empty()) {
            SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
            RETURN_AND_STOP_PARSE();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    else {
        if (uc == '(' || uc == '<') {
            ejson_stack_pop();
            if (!vcm_stack_is_empty()) {
                POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            }
            RECONSUME_IN(EJSON_CONTROL_STATE);
        }
        if (ejson_stack_is_empty()) {
            SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
            RETURN_AND_STOP_PARSE();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
END_STATE()

BEGIN_STATE(EJSON_AFTER_VALUE_STATE)
    uint32_t uc = ejson_stack_top();
    if (is_whitespace(character)) {
        if (ejson_stack_is_empty() || uc  == 'U' || uc == '"' || uc == 'T') {
            RECONSUME_IN(EJSON_CONTROL_STATE);
        }
        ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
    }
    if (is_eof(character) && ejson_stack_is_empty()) {
        RECONSUME_IN(EJSON_FINISHED_STATE);
    }
    if (character == '"' || character == '\'') {
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer));
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
        }
        if (uc == '"' || uc == '\'') {
            ejson_stack_pop();
            if (ejson_stack_is_empty()) {
                ADVANCE_TO(EJSON_FINISHED_STATE);
            }
        }
        ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
    }
    if (character == '}') {
        RECONSUME_IN(EJSON_RIGHT_BRACE_STATE);
    }
    if (character == ']') {
        RECONSUME_IN(EJSON_RIGHT_BRACKET_STATE);
    }
    if (character == ')') {
        pcejson_dec_depth(parser);
        ADVANCE_TO(EJSON_RIGHT_PARENTHESIS_STATE);
    }
    if (character == ',') {
        if (uc == '{') {
            ejson_stack_pop();
            ADVANCE_TO(EJSON_BEFORE_NAME_STATE);
        }
        if (uc == '[') {
            if (!uc_buffer_is_empty(parser->temp_buffer)) {
                struct pcvcm_node *node = pcvcm_node_new_string(
                uc_buffer_get_bytes(parser->temp_buffer));
                APPEND_AS_VCM_CHILD(node);
                RESET_TEMP_BUFFER();
            }
            if (parser->vcm_node &&
                    parser->vcm_node->type != PCVCM_NODE_TYPE_ARRAY) {
                POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            }
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        if (uc == '(' || uc == '<') {
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        if (uc == ':') {
            ejson_stack_pop();
            if (!uc_buffer_is_empty(parser->temp_buffer)) {
                struct pcvcm_node *node = pcvcm_node_new_string(
                uc_buffer_get_bytes(parser->temp_buffer));
                APPEND_AS_VCM_CHILD(node);
                RESET_TEMP_BUFFER();
            }
            if (parser->vcm_node &&
                    parser->vcm_node->type != PCVCM_NODE_TYPE_OBJECT) {
                POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            }
            ADVANCE_TO(EJSON_BEFORE_NAME_STATE);
        }
        // FIXME
        if (ejson_stack_is_empty() && parser->vcm_node) {
            parser->prev_separator = 0;
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '<' || character == '.') {
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (uc == '"' || uc  == 'U') {
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_BEFORE_NAME_STATE)
    if (is_whitespace(character)) {
        ADVANCE_TO(EJSON_BEFORE_NAME_STATE);
    }
    uint32_t uc = ejson_stack_top();
    if (character == '"') {
        RESET_TEMP_BUFFER();
        RESET_STRING_BUFFER();
        if (uc == '{') {
            ejson_stack_push(':');
        }
        RECONSUME_IN(EJSON_NAME_DOUBLE_QUOTED_STATE);
    }
    if (character == '\'') {
        RESET_TEMP_BUFFER();
        if (uc == '{') {
            ejson_stack_push(':');
        }
        RECONSUME_IN(EJSON_NAME_SINGLE_QUOTED_STATE);
    }
    if (character == '}') {
        RECONSUME_IN(EJSON_RIGHT_BRACE_STATE);
    }
    if (character == '$') {
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (is_ascii_alpha(character)) {
        RESET_TEMP_BUFFER();
        if (uc == '{') {
            ejson_stack_push(':');
        }
        RECONSUME_IN(EJSON_NAME_UNQUOTED_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_AFTER_NAME_STATE)
    if (is_whitespace(character)) {
        ADVANCE_TO(EJSON_AFTER_NAME_STATE);
    }
    if (character == ':') {
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                uc_buffer_get_bytes(parser->temp_buffer));
            APPEND_AS_VCM_CHILD(node);
        }
        ADVANCE_TO(EJSON_CONTROL_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_NAME_UNQUOTED_STATE)
    if (is_whitespace(character) || character == ':') {
        RECONSUME_IN(EJSON_AFTER_NAME_STATE);
    }
    if (is_ascii_alpha(character) || is_ascii_digit(character)
            || character == '-' || character == '_') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_NAME_UNQUOTED_STATE);
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                NULL);
        UPDATE_VCM_NODE(snode);
        ejson_stack_push('U');
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer)
                    );
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_NAME_SINGLE_QUOTED_STATE)
    if (character == '\'') {
        size_t nr_buf_chars = uc_buffer_get_size_in_chars(
                parser->temp_buffer);
        if (nr_buf_chars >= 1) {
            ADVANCE_TO(EJSON_AFTER_NAME_STATE);
        }
        else {
            ADVANCE_TO(EJSON_NAME_SINGLE_QUOTED_STATE);
        }
    }
    if (character == '\\') {
        SET_RETURN_STATE(EJSON_NAME_SINGLE_QUOTED_STATE);
        ADVANCE_TO(EJSON_STRING_ESCAPE_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    APPEND_TO_TEMP_BUFFER(character);
    ADVANCE_TO(EJSON_NAME_SINGLE_QUOTED_STATE);
END_STATE()

BEGIN_STATE(EJSON_NAME_DOUBLE_QUOTED_STATE)
    if (character == '"') {
        size_t nr_buf_chars = uc_buffer_get_size_in_chars(
                parser->temp_buffer);
        if (nr_buf_chars > 1) {
            uc_buffer_delete_head_chars (parser->temp_buffer, 1);
            ADVANCE_TO(EJSON_AFTER_NAME_STATE);
        }
        else if (nr_buf_chars == 1) {
            RESET_TEMP_BUFFER();
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_string ("");
            APPEND_AS_VCM_CHILD(node);
            ADVANCE_TO(EJSON_AFTER_NAME_STATE);
        }
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_NAME_DOUBLE_QUOTED_STATE);
    }
    if (character == '\\') {
        SET_RETURN_STATE(curr_state);
        ADVANCE_TO(EJSON_STRING_ESCAPE_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                NULL);
        UPDATE_VCM_NODE(snode);
        ejson_stack_push('"');
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer)
                    );
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    APPEND_TO_TEMP_BUFFER(character);
    ADVANCE_TO(EJSON_NAME_DOUBLE_QUOTED_STATE);
END_STATE()

BEGIN_STATE(EJSON_VALUE_SINGLE_QUOTED_STATE)
    if (character == '\'') {
        parser->nr_quoted++;
        size_t nr_buf_chars = uc_buffer_get_size_in_chars(
                parser->temp_buffer);
        if (parser->nr_quoted > 1 || nr_buf_chars >= 1) {
            RESET_QUOTED_COUNTER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        else {
            ADVANCE_TO(EJSON_VALUE_SINGLE_QUOTED_STATE);
        }
    }
    if (character == '\\') {
        SET_RETURN_STATE(curr_state);
        ADVANCE_TO(EJSON_STRING_ESCAPE_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    APPEND_TO_TEMP_BUFFER(character);
    ADVANCE_TO(EJSON_VALUE_SINGLE_QUOTED_STATE);
END_STATE()

BEGIN_STATE(EJSON_VALUE_DOUBLE_QUOTED_STATE)
    if (character == '"') {
        if (parser->nr_quoted == 0) {
            parser->nr_quoted++;
            ADVANCE_TO(EJSON_VALUE_DOUBLE_QUOTED_STATE);
        }
        else if (parser->nr_quoted == 1) {
            RECONSUME_IN(EJSON_VALUE_TWO_DOUBLE_QUOTED_STATE);
        }
        RECONSUME_IN(EJSON_AFTER_VALUE_DOUBLE_QUOTED_STATE);
    }
    if (character == '\\') {
        SET_RETURN_STATE(curr_state);
        ADVANCE_TO(EJSON_STRING_ESCAPE_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                NULL);
        UPDATE_VCM_NODE(snode);
        ejson_stack_push('"');
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            if (uc_buffer_end_with(parser->temp_buffer, "{", 1)) {
                rwswrap_reconsume_last_char(parser->rwswrap);
                rwswrap_reconsume_last_char(parser->rwswrap);
                uc_buffer_delete_tail_chars(parser->temp_buffer, 1);
                if (!uc_buffer_is_empty(parser->temp_buffer)) {
                    struct pcvcm_node *node = pcvcm_node_new_string(
                            uc_buffer_get_bytes(parser->temp_buffer)
                            );
                    APPEND_AS_VCM_CHILD(node);
                }
            }
            else if (uc_buffer_end_with(parser->temp_buffer, "{{", 2)) {
                rwswrap_reconsume_last_char(parser->rwswrap);
                rwswrap_reconsume_last_char(parser->rwswrap);
                rwswrap_reconsume_last_char(parser->rwswrap);
                uc_buffer_delete_tail_chars(parser->temp_buffer, 2);
                if (!uc_buffer_is_empty(parser->temp_buffer)) {
                    struct pcvcm_node *node = pcvcm_node_new_string(
                            uc_buffer_get_bytes(parser->temp_buffer)
                            );
                    APPEND_AS_VCM_CHILD(node);
                }
            }
            else {
                rwswrap_reconsume_last_char(parser->rwswrap);
                struct pcvcm_node *node = pcvcm_node_new_string(
                        uc_buffer_get_bytes(parser->temp_buffer)
                        );
                APPEND_AS_VCM_CHILD(node);
            }
            RESET_TEMP_BUFFER();
            ADVANCE_TO(EJSON_CONTROL_STATE);
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    APPEND_TO_TEMP_BUFFER(character);
    ADVANCE_TO(EJSON_VALUE_DOUBLE_QUOTED_STATE);
END_STATE()

BEGIN_STATE(EJSON_AFTER_VALUE_DOUBLE_QUOTED_STATE)
    if (character == '\"') {
        RESET_QUOTED_COUNTER();
        RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_TWO_DOUBLE_QUOTED_STATE)
    if (character == '"') {
        if (parser->nr_quoted == 1) {
            parser->nr_quoted++;
            ADVANCE_TO(EJSON_VALUE_TWO_DOUBLE_QUOTED_STATE);
        }
        else if (parser->nr_quoted == 2) {
            RECONSUME_IN(EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE);
        }
    }
    RESTORE_VCM_NODE();
    struct pcvcm_node *node = pcvcm_node_new_string(
            uc_buffer_get_bytes(parser->temp_buffer)
            );
    APPEND_AS_VCM_CHILD(node);
    RESET_TEMP_BUFFER();
    RESET_QUOTED_COUNTER();
    RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
END_STATE()

BEGIN_STATE(EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE)
    if (character == '\"') {
        parser->nr_quoted++;
        if (parser->nr_quoted > 3) {
            APPEND_TO_TEMP_BUFFER(character);
        }
        if (parser->nr_quoted >= 6
                && uc_buffer_end_with(parser->temp_buffer,
                    "\"\"\"", 3)) {
            RESTORE_VCM_NODE();
            uc_buffer_delete_tail_chars(parser->temp_buffer, 3);
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer)
                    );
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RESET_QUOTED_COUNTER();
            ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
        }
        ADVANCE_TO(EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    APPEND_TO_TEMP_BUFFER(character);
    ADVANCE_TO(EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE);
END_STATE()

BEGIN_STATE(EJSON_KEYWORD_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ','
            || character == ')') {
        RECONSUME_IN(EJSON_AFTER_KEYWORD_STATE);
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                NULL);
        UPDATE_VCM_NODE(snode);
        ejson_stack_push('U');
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer)
                    );
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == 't' || character == 'f' || character == 'n') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'r') {
        if (uc_buffer_equal_to(parser->temp_buffer, "t", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'u') {
        if (uc_buffer_equal_to(parser->temp_buffer, "tr", 2)
           || uc_buffer_equal_to(parser->temp_buffer, "n", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'e') {
        if (uc_buffer_equal_to(parser->temp_buffer, "tru", 3)
           || uc_buffer_equal_to(parser->temp_buffer, "fals", 4)
           ) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'a') {
        if (uc_buffer_equal_to(parser->temp_buffer, "f", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'l') {
        if (uc_buffer_equal_to(parser->temp_buffer, "nu", 2)
         || uc_buffer_equal_to(parser->temp_buffer, "nul", 3)
         || uc_buffer_equal_to(parser->temp_buffer, "fa", 2)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 's') {
        if (uc_buffer_equal_to(parser->temp_buffer, "fal", 3)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_KEYWORD_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_KEYWORD);
        RETURN_AND_STOP_PARSE();
    }
    if (is_eof(character)) {
        RECONSUME_IN(EJSON_AFTER_KEYWORD_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_AFTER_KEYWORD_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ','
            || character == ')'
            || is_eof(character)) {
        if (uc_buffer_equal_to(parser->temp_buffer, "true", 4)) {
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_boolean(true);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        if (uc_buffer_equal_to(parser->temp_buffer, "false",
                    5)) {
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_boolean(false);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        if (uc_buffer_equal_to(parser->temp_buffer, "null", 4)) {
            struct pcvcm_node *node = pcvcm_node_new_null();
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        RESET_TEMP_BUFFER();
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RETURN_AND_STOP_PARSE();
    }
    RESET_TEMP_BUFFER();
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_BYTE_SEQUENCE_STATE)
    if (character == 'b') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_BYTE_SEQUENCE_STATE);
        }
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_BINARY_BYTE_SEQUENCE_STATE);
    }
    if (character == 'x') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_HEX_BYTE_SEQUENCE_STATE);
    }
    if (character == '6') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_BASE64_BYTE_SEQUENCE_STATE);
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                NULL);
        UPDATE_VCM_NODE(snode);
        ejson_stack_push('U');
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer)
                    );
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_AFTER_BYTE_SEQUENCE_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        struct pcvcm_node *node = create_byte_sequenct(parser->temp_buffer);
        if (node == NULL) {
            SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
            RETURN_AND_STOP_PARSE();
        }
        RESTORE_VCM_NODE();
        APPEND_AS_VCM_CHILD(node);
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_HEX_BYTE_SEQUENCE_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_BYTE_SEQUENCE_STATE);
    }
    else if (is_ascii_digit(character)
            || is_ascii_hex_digit(character)) {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_HEX_BYTE_SEQUENCE_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_BINARY_BYTE_SEQUENCE_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_BYTE_SEQUENCE_STATE);
    }
    else if (is_ascii_binary_digit(character)) {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_BINARY_BYTE_SEQUENCE_STATE);
    }
    if (character == '.') {
        ADVANCE_TO(EJSON_BINARY_BYTE_SEQUENCE_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_BASE64_BYTE_SEQUENCE_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_BYTE_SEQUENCE_STATE);
    }
    if (character == '=') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_BASE64_BYTE_SEQUENCE_STATE);
    }
    if (is_ascii_digit(character) || is_ascii_alpha(character)
            || character == '+' || character == '-') {
        if (!uc_buffer_end_with(parser->temp_buffer, "=", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_BASE64_BYTE_SEQUENCE_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_BASE64);
        RETURN_AND_STOP_PARSE();
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_VALUE_NUMBER_STATE);
    }
    if (is_ascii_digit(character)) {
        RECONSUME_IN(EJSON_VALUE_NUMBER_INTEGER_STATE);
    }
    if (character == '-') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_INTEGER_STATE);
    }
    if (character == '$') {
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                NULL);
        UPDATE_VCM_NODE(snode);
        ejson_stack_push('U');
        if (!uc_buffer_is_empty(parser->temp_buffer)) {
            struct pcvcm_node *node = pcvcm_node_new_string(
                    uc_buffer_get_bytes(parser->temp_buffer)
                    );
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    SET_ERR(PCEJSON_ERROR_BAD_JSON_NUMBER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_AFTER_VALUE_NUMBER_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')'
            || is_eof(character)) {
        if (uc_buffer_end_with(parser->temp_buffer, "-", 1)
            || uc_buffer_end_with(parser->temp_buffer, "E", 1)
            || uc_buffer_end_with(parser->temp_buffer, "e", 1)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSON_NUMBER);
            RETURN_AND_STOP_PARSE();
        }
        double d = strtod(
                uc_buffer_get_bytes(parser->temp_buffer), NULL);
        RESTORE_VCM_NODE();
        struct pcvcm_node *node = pcvcm_node_new_number(d);
        APPEND_AS_VCM_CHILD(node);
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_INTEGER_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_VALUE_NUMBER_STATE);
    }
    if (is_ascii_digit(character)) {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_INTEGER_STATE);
    }
    if (character == 'E' || character == 'e') {
        APPEND_TO_TEMP_BUFFER('e');
        ADVANCE_TO(EJSON_VALUE_NUMBER_EXPONENT_STATE);
    }
    if (character == '.' || character == 'F') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_FRACTION_STATE);
    }
    if (character == 'U' || character == 'L') {
        RECONSUME_IN(EJSON_VALUE_NUMBER_SUFFIX_INTEGER_STATE);
    }
    if (character == 'I' && (
                uc_buffer_is_empty(parser->temp_buffer) ||
                uc_buffer_equal_to(parser->temp_buffer, "-", 1)
                )) {
        RECONSUME_IN(EJSON_VALUE_NUMBER_INFINITY_STATE);
    }
    if (is_eof(character)) {
        ADVANCE_TO(EJSON_AFTER_VALUE_NUMBER_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER_INTEGER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_FRACTION_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')'
            || is_eof(character)) {
        RECONSUME_IN(EJSON_AFTER_VALUE_NUMBER_STATE);
    }

    if (is_ascii_digit(character)) {
        if (uc_buffer_end_with(parser->temp_buffer, "F", 1)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSON_NUMBER);
            RETURN_AND_STOP_PARSE();
        }
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_FRACTION_STATE);
    }
    if (character == 'F') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_FRACTION_STATE);
    }
    if (character == 'L') {
        if (uc_buffer_end_with(parser->temp_buffer, "F", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            long double ld = strtold (
                    uc_buffer_get_bytes(parser->temp_buffer), NULL);
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_longdouble(ld);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
        }
    }
    if (character == 'E' || character == 'e') {
        if (uc_buffer_end_with(parser->temp_buffer, ".", 1)) {
            SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER_FRACTION);
            RETURN_AND_STOP_PARSE();
        }
        APPEND_TO_TEMP_BUFFER('e');
        ADVANCE_TO(EJSON_VALUE_NUMBER_EXPONENT_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER_FRACTION);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_EXPONENT_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_VALUE_NUMBER_STATE);
    }
    if (is_ascii_digit(character)) {
        RECONSUME_IN(EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE);
    }
    if (character == '+' || character == '-') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE);
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER_EXPONENT);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_VALUE_NUMBER_STATE);
    }
    if (is_ascii_digit(character)) {
        if (uc_buffer_end_with(parser->temp_buffer, "F", 1)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSON_NUMBER);
            RETURN_AND_STOP_PARSE();
        }
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE);
    }
    if (character == 'F') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE);
    }
    if (character == 'L') {
        if (uc_buffer_end_with(parser->temp_buffer, "F", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            long double ld = strtold (
                    uc_buffer_get_bytes(parser->temp_buffer), NULL);
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_longdouble(ld);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            ADVANCE_TO(EJSON_AFTER_VALUE_NUMBER_STATE);
        }
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER_EXPONENT);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_SUFFIX_INTEGER_STATE)
    uint32_t last_c = uc_buffer_get_last_char(
            parser->temp_buffer);
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        RECONSUME_IN(EJSON_AFTER_VALUE_NUMBER_STATE);
    }
    if (character == 'U') {
        if (is_ascii_digit(last_c)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_SUFFIX_INTEGER_STATE);
        }
    }
    if (character == 'L') {
        if (is_ascii_digit(last_c) || last_c == 'U') {
            APPEND_TO_TEMP_BUFFER(character);
            if (uc_buffer_end_with(parser->temp_buffer, "UL", 2)
                    ) {
                uint64_t u64 = strtoull (
                    uc_buffer_get_bytes(parser->temp_buffer),
                    NULL, 10);
                RESTORE_VCM_NODE();
                struct pcvcm_node *node = pcvcm_node_new_ulongint(u64);
                APPEND_AS_VCM_CHILD(node);
                RESET_TEMP_BUFFER();
                ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
            }
            else if (uc_buffer_end_with(parser->temp_buffer,
                        "L", 1)) {
                int64_t i64 = strtoll (
                    uc_buffer_get_bytes(parser->temp_buffer),
                    NULL, 10);
                RESTORE_VCM_NODE();
                struct pcvcm_node *node = pcvcm_node_new_longint(i64);
                APPEND_AS_VCM_CHILD(node);
                RESET_TEMP_BUFFER();
                ADVANCE_TO(EJSON_AFTER_VALUE_STATE);
            }
        }
    }
    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER_INTEGER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NUMBER_INFINITY_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        if (uc_buffer_equal_to(parser->temp_buffer,
                    "-Infinity", 9)) {
            double d = -INFINITY;
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_number(d);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        if (uc_buffer_equal_to(parser->temp_buffer,
                "Infinity", 8)) {
            double d = INFINITY;
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_number(d);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'I') {
        if (uc_buffer_is_empty(parser->temp_buffer)
            || uc_buffer_equal_to(parser->temp_buffer, "-", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_INFINITY_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    if (character == 'n') {
        if (uc_buffer_equal_to(parser->temp_buffer, "I", 1)
          || uc_buffer_equal_to(parser->temp_buffer, "-I", 2)
          || uc_buffer_equal_to(parser->temp_buffer, "Infi", 4)
          || uc_buffer_equal_to(parser->temp_buffer, "-Infi", 5)
            ) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_INFINITY_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    if (character == 'f') {
        if (uc_buffer_equal_to(parser->temp_buffer, "In", 2)
            || uc_buffer_equal_to (parser->temp_buffer, "-In", 3)
                ) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_INFINITY_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    if (character == 'i') {
        if (uc_buffer_equal_to(parser->temp_buffer, "Inf", 3)
         || uc_buffer_equal_to(parser->temp_buffer, "-Inf", 4)
         || uc_buffer_equal_to(parser->temp_buffer, "Infin", 5)
         || uc_buffer_equal_to(parser->temp_buffer, "-Infin", 6)
         ) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_INFINITY_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    if (character == 't') {
        if (uc_buffer_equal_to(parser->temp_buffer, "Infini", 6)
            || uc_buffer_equal_to (parser->temp_buffer,
                "-Infini", 7)
                ) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_INFINITY_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    if (character == 'y') {
        if (uc_buffer_equal_to(parser->temp_buffer, "Infinit", 7)
           || uc_buffer_equal_to (parser->temp_buffer,
               "-Infinit", 8)
                ) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NUMBER_INFINITY_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_VALUE_NAN_STATE)
    if (is_whitespace(character) || character == '}'
            || character == ']' || character == ',' || character == ')') {
        if (uc_buffer_equal_to(parser->temp_buffer, "NaN", 3)) {
            double d = NAN;
            RESTORE_VCM_NODE();
            struct pcvcm_node *node = pcvcm_node_new_number(d);
            APPEND_AS_VCM_CHILD(node);
            RESET_TEMP_BUFFER();
            RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }
    if (character == 'N') {
        if (uc_buffer_is_empty(parser->temp_buffer)
          || uc_buffer_equal_to(parser->temp_buffer, "Na", 2)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NAN_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    if (character == 'a') {
        if (uc_buffer_equal_to(parser->temp_buffer, "N", 1)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_VALUE_NAN_STATE);
        }
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
        RETURN_AND_STOP_PARSE();
    }

    SET_ERR(PCEJSON_ERROR_UNEXPECTED_JSON_NUMBER);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_STRING_ESCAPE_STATE)
    switch (character)
    {
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            APPEND_TO_TEMP_BUFFER('\\');
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(parser->return_state);
            break;
        case '$':
        case '{':
        case '}':
        case '<':
        case '>':
        case '/':
        case '\\':
        case '"':
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(parser->return_state);
            break;
        case 'u':
            RESET_STRING_BUFFER();
            ADVANCE_TO(
              EJSON_STRING_ESCAPE_FOUR_HEXADECIMAL_DIGITS_STATE);
            break;
        default:
            SET_ERR(PCEJSON_ERROR_BAD_JSON_STRING_ESCAPE_ENTITY);
            RETURN_AND_STOP_PARSE();
    }
END_STATE()

BEGIN_STATE(EJSON_STRING_ESCAPE_FOUR_HEXADECIMAL_DIGITS_STATE)
    if (is_ascii_hex_digit(character)) {
        APPEND_TO_STRING_BUFFER(character);
        size_t nr_chars = uc_buffer_get_size_in_chars(
                parser->string_buffer);
        if (nr_chars == 4) {
            APPEND_BYTES_TO_TEMP_BUFFER("\\u", 2);
            APPEND_BUFFER_TO_TEMP_BUFFER(parser->string_buffer);
            RESET_STRING_BUFFER();
            ADVANCE_TO(parser->return_state);
        }
        ADVANCE_TO(
            EJSON_STRING_ESCAPE_FOUR_HEXADECIMAL_DIGITS_STATE);
    }
    SET_ERR(PCEJSON_ERROR_BAD_JSON_STRING_ESCAPE_ENTITY);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_JSONEE_VARIABLE_STATE)
    if (character == '"') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            RECONSUME_IN(EJSON_VALUE_DOUBLE_QUOTED_STATE);
        }
    }
    if (character == '\'') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            RESET_QUOTED_COUNTER();
            RECONSUME_IN(EJSON_VALUE_SINGLE_QUOTED_STATE);
        }
    }
    if (character == '$') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            RECONSUME_IN(EJSON_CONTROL_STATE);
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        while (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            uc = ejson_stack_top();
        }
        if (uc == '(' || uc == '<' || uc == '.' || uc == '"') {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == '_' || is_ascii_digit(character)) {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
    }
    if (is_ascii_alpha(character) || character == '-') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
    }
    if (is_whitespace(character) || character == '}'
            || character == '"' || character == ']' || character == ')') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_VARIABLE_NAME);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        while (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            uc = ejson_stack_top();
        }
        if (uc == '(' || uc == '<' || uc == '.' || uc == '"' || uc == 'T') {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == ',') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_VARIABLE_NAME);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        while (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            uc = ejson_stack_top();
        }
        if (uc == '(' || uc == '<') {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
    }
    if (character == ':') {
        if (uc_buffer_is_empty(parser->temp_buffer)
            || uc_buffer_is_int(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
        }
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        while (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            uc = ejson_stack_top();
        }
        if (uc == '(' || uc == '<' || uc == '{') {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        if (uc == 'P') {
            ejson_stack_pop();
            ejson_stack_push('{');
            ejson_stack_push(':');
            struct pcvcm_node *node = pcvcm_node_new_object(0, NULL);
            APPEND_CHILD(node, parser->vcm_node);
            UPDATE_VCM_NODE(node);
        }
        if (ejson_stack_is_empty()) {
            RECONSUME_IN(EJSON_FINISHED_STATE);
        }
        ADVANCE_TO(EJSON_CONTROL_STATE);
    }
    if (is_context_variable(character)) {
        if (uc_buffer_is_empty(parser->temp_buffer)
            || uc_buffer_is_int(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
        }
    }
    if (character == '[' || character == '(') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_VARIABLE_NAME);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        if (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == '<' || character == '>') {
        // FIXME
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        if (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == '.') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_VARIABLE_NAME);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        uint32_t uc = ejson_stack_top();
        if (uc == '$') {
            ejson_stack_pop();
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_JSONEE_FULL_STOP_SIGN_STATE);
    }
    if (character == '=') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            APPEND_TO_TEMP_BUFFER(character);
            ADVANCE_TO(EJSON_JSONEE_VARIABLE_STATE);
        }
    }
    SET_ERR(PCEJSON_ERROR_BAD_JSONEE_VARIABLE_NAME);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_JSONEE_KEYWORD_STATE)
    if (is_ascii_digit(character)) {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_KEYWORD);
            RETURN_AND_STOP_PARSE();
        }
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_JSONEE_KEYWORD_STATE);
    }
    if (is_ascii_alpha(character) || character == '_' ||
            character == '-') {
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_JSONEE_KEYWORD_STATE);
    }
    if (is_whitespace(character) || character == '[' ||
            character == '(' || character == '<' || character == '}' ||
            character == '$' || character == '>' || character == ']'
            || character == ')' || character == ':') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_KEYWORD);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        ejson_stack_pop();
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == '"') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_KEYWORD);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        ejson_stack_pop();
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        //ADVANCE_TO(EJSON_CONTROL_STATE);
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == ',') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_KEYWORD);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        ejson_stack_pop();
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        uint32_t uc = ejson_stack_top();
        if (uc == '(' || uc == '<') {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        RECONSUME_IN(EJSON_AFTER_VALUE_STATE);
    }
    if (character == '.') {
        if (uc_buffer_is_empty(parser->temp_buffer)) {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_KEYWORD);
            RETURN_AND_STOP_PARSE();
        }
        if (parser->vcm_node) {
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        ejson_stack_pop();
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        RECONSUME_IN(EJSON_JSONEE_FULL_STOP_SIGN_STATE);
    }
    SET_ERR(PCEJSON_ERROR_BAD_JSONEE_KEYWORD);
    RETURN_AND_STOP_PARSE();
END_STATE()

BEGIN_STATE(EJSON_JSONEE_STRING_STATE)
    uint32_t uc = ejson_stack_top();
    if (is_whitespace(character)) {
        if (uc == 'U') {
            RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
        }
        APPEND_TO_TEMP_BUFFER(character);
        ADVANCE_TO(EJSON_JSONEE_STRING_STATE);
    }
    if (character == '$') {
        if (uc != 'U' && uc != '"') {
            if (parser->vcm_node) {
                vcm_stack_push(parser->vcm_node);
            }
            struct pcvcm_node *snode = pcvcm_node_new_concat_string(0,
                    NULL);
            UPDATE_VCM_NODE(snode);
            ejson_stack_push('"');
            if (!uc_buffer_is_empty(parser->temp_buffer)) {
                struct pcvcm_node *node = pcvcm_node_new_string(
                   uc_buffer_get_bytes(parser->temp_buffer));
                APPEND_AS_VCM_CHILD(node);
                RESET_TEMP_BUFFER();
                ADVANCE_TO(EJSON_JSONEE_STRING_STATE);
            }
        }
        RECONSUME_IN(EJSON_CONTROL_STATE);
    }
    if (character == '\\') {
        SET_RETURN_STATE(curr_state);
        ADVANCE_TO(EJSON_STRING_ESCAPE_STATE);
    }
    if (character == '"') {
        if (parser->vcm_node) {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            vcm_stack_push(parser->vcm_node);
        }
        parser->vcm_node = pcvcm_node_new_string(
                uc_buffer_get_bytes(parser->temp_buffer));
        RESET_TEMP_BUFFER();
        RECONSUME_IN(EJSON_AFTER_JSONEE_STRING_STATE);
    }
    if (is_eof(character)) {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_EOF);
        RETURN_AND_STOP_PARSE();
    }
    if (character == ':' && uc == ':') {
        SET_ERR(PCEJSON_ERROR_UNEXPECTED_CHARACTER);
        RESET_TEMP_BUFFER();
        RETURN_AND_STOP_PARSE();
    }
    APPEND_TO_TEMP_BUFFER(character);
    ADVANCE_TO(EJSON_JSONEE_STRING_STATE);
END_STATE()

BEGIN_STATE(EJSON_AFTER_JSONEE_STRING_STATE)
    uint32_t uc = ejson_stack_top();
    if (is_whitespace(character)) {
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        if (uc == 'U') {
            ejson_stack_pop();
            if (!ejson_stack_is_empty()) {
                POP_AS_VCM_PARENT_AND_UPDATE_VCM();
            }
            RECONSUME_IN(EJSON_CONTROL_STATE);
        }
        RECONSUME_IN(EJSON_JSONEE_STRING_STATE);
    }
    if (character == '"') {
        if (uc == 'U') {
            SET_ERR(PCEJSON_ERROR_BAD_JSONEE_NAME);
            RETURN_AND_STOP_PARSE();
        }
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        ejson_stack_pop();
        if (!ejson_stack_is_empty()) {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        ADVANCE_TO(EJSON_CONTROL_STATE);
    }
    if (character == '}' || character == ']' || character == ')') {
        POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        ejson_stack_pop();
        if (!ejson_stack_is_empty()) {
            POP_AS_VCM_PARENT_AND_UPDATE_VCM();
        }
        ADVANCE_TO(EJSON_CONTROL_STATE);
    }
    SET_ERR(PCEJSON_ERROR_BAD_JSONEE_NAME);
    RETURN_AND_STOP_PARSE();
END_STATE()

PCEJSON_PARSER_END
#endif
