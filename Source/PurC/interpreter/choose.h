/**
 * @file choose.h
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

#ifndef PURC_PRIVATE_INTERPRETER_ITERATE_H
#define PURC_PRIVATE_INTERPRETER_ITERATE_H

#include "purc.h"

#include "purc-macros.h"

#include "private/interpreter.h"

PCA_EXTERN_C_BEGIN

struct pcintr_element_ops* pcintr_choose_get_ops(void);

PCA_EXTERN_C_END

#endif  /* PURC_PRIVATE_INTERPRETER_ITERATE_H */

