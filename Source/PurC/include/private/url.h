/*
 * @file url.h
 * @author gengyue
 * @date 2021/12/26
 * @brief The header file of URL implementation.
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

#ifndef PURC_PRIVATE_URL_H
#define PURC_PRIVATE_URL_H

#include "private/vdom.h"

#ifdef __cplusplus
extern "C" {
#endif


char * pcdvobjs_get_url (const struct purc_broken_down_url *);
bool pcdvobjs_set_url (struct purc_broken_down_url *, const char *);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* not defined PURC_PRIVATE_URL_H*/
