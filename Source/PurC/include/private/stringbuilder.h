/**
 * @file stringbuilder.h
 * @author Xu Xiaohong
 * @date 2021/11/06
 * @brief The internal interfaces for stringbuilder.
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

#ifndef PURC_PRIVATE_STRINGBUILDER_H
#define PURC_PRIVATE_STRINGBUILDER_H

#include "config.h"

#include "purc-macros.h"
#include "private/list.h"

#include <stdarg.h>
#include <string.h>

struct pcutils_stringbuilder
{
    struct list_head           list;

    struct pcutils_buf        *curr;

    size_t                     total;

    size_t                     chunk;
    unsigned int               oom:1;
};

PCA_EXTERN_C_BEGIN

static inline void
pcutils_stringbuilder_init(struct pcutils_stringbuilder *sb, size_t chunk)
{
    memset(sb, 0, sizeof(*sb));
    if (chunk == (size_t)-1) {
        chunk = 64;
    }
    sb->chunk = chunk;
    INIT_LIST_HEAD(&sb->list);
}

void
pcutils_stringbuilder_reset(struct pcutils_stringbuilder *sb);

int
pcutils_stringbuilder_keep(struct pcutils_stringbuilder *sb, size_t sz);

# if COMPILER(GCC)
__attribute__ ((format (gnu_printf, 2, 0)))
# endif
int pcutils_stringbuilder_vsnprintf(struct pcutils_stringbuilder *sb,
        const char *fmt, va_list ap);

# if COMPILER(GCC)
__attribute__ ((format (gnu_printf, 2, 3)))
# endif
static inline int
pcutils_stringbuilder_snprintf(struct pcutils_stringbuilder *sb,
    const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = pcutils_stringbuilder_vsnprintf(sb, fmt, ap);
    va_end(ap);

    return n;
}

char*
pcutils_stringbuilder_build(struct pcutils_stringbuilder *sb);

PCA_EXTERN_C_END

#endif  /* PURC_PRIVATE_STRINGBUILDER_H */

