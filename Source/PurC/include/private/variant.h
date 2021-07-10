/**
 * @file variant.h
 * @author 
 * @date 2021/07/02
 * @brief The internal interfaces for variant.
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

#ifndef PURC_PRIVATE_VARIANT_H
#define PURC_PRIVATE_VARIANT_H

#include "config.h"
#include "purc-variant.h"

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define PCVARIANT_FLAG_CONSTANT     (0x01 << 0)     // for null, true, ...
#define PCVARIANT_FLAG_NOFREE       PCVARIANT_FLAG_CONSTANT
#define PCVARIANT_FLAG_EXTRA_SIZE   (0x01 << 1)     // when use extra space

/* VWNOTE: use value->size or PCVARIANT_FLAG_EXTRA_SIZE flag */
#define PCVARIANT_FLAG_LONG         (0x01 << 15)    // for long string or sequence
/* VWNOTE: no need */
#define PCVARIANT_FLAG_SIGNED       (0x01 << 15)    // for signed int
/* VWNOTE: no need. */
#define PCVARIANT_FLAG_ATOM_STATIC  (0x01 << 15)    // for static atom string

#define PVT(t) (PURC_VARIANT_TYPE##t)

#define MAX_RESERVED_VARIANTS  32

// structure for variant
struct purc_variant {

    /* variant type */
    unsigned int type:8;

    /* real length for short string and byte sequence.
       use the extra space (long string and byte sequence)
       if the value of this field is 0. */
    unsigned int size:8;

    /* flags */
    unsigned int flags:16;

    /* reference count */
    unsigned int refc;

    /* value */
    union {
        /* for boolean */
        bool        b;

        /* for number */
        double      d;

        /* for long integer */
        int64_t     i64;

        /* for unsigned long integer */
        uint64_t    u64;

        /* for long double */
        long double ld;

        /* for dynamic and native variant (two pointers) */
        void*       ptr2[2];

        /* for long string, long byte sequence, array, object,
           and set (sz_ptr[0] for size, sz_ptr[1] for pointer). */
        uintptr_t   sz_ptr[2];

        /* for short string and byte sequence; the real space size of `bytes`
           is `max(sizeof(long double), sizeof(void*) * 2)` */
        uint8_t     bytes[0];
    };
};

#define MAX_RESERVED_VARIANTS   32
#define SZ_COMMON_BUFFER        1024

struct pcvariant_heap {
    // the constant values.
    struct purc_variant v_undefined;
    struct purc_variant v_null;
    struct purc_variant v_false;
    struct purc_variant v_true;

    // the statistics of memory usage of variant values
    struct purc_variant_stat stat;

    // the loop buffer for reserved values.
    purc_variant_t nr_reserved [MAX_RESERVED_VARIANTS];
    int headpos;
    int tailpos;

    // the fixed-size buffer for serializing the values
    char buff[SZ_COMMON_BUFFER];
};

// initialize variant module
void pcvariant_init(void) WTF_INTERNAL;

struct pcinst;
void pcvariant_init_instance(struct pcinst* inst) WTF_INTERNAL;
void pcvariant_cleanup_instance(struct pcinst* inst) WTF_INTERNAL;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* PURC_PRIVATE_VARIANT_H */
