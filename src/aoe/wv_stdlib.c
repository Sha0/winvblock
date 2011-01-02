/**
 * Copyright (C) 2010-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, originally derived from WinAoE.
 *
 * WinVBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinVBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinVBlock.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ntddk.h>
#include "wv_stdlib.h"

PVOID wv_malloc(wv_size_t size) {
    return ExAllocatePoolWithTag(NonPagedPool, size, 'EoAW');
  }

PVOID wv_palloc(wv_size_t size) {
    return ExAllocatePoolWithTag(PagedPool, size, 'EoAW');
  }

PVOID wv_mallocz(wv_size_t size) {
    PVOID ptr = ExAllocatePoolWithTag(NonPagedPool, size, 'EoAW');
    return ptr ? RtlZeroMemory(ptr, size), ptr : ptr;
  }

PVOID wv_pallocz(wv_size_t size) {
    PVOID ptr = ExAllocatePoolWithTag(PagedPool, size, 'EoAW');
    return ptr ? RtlZeroMemory(ptr, size), ptr : ptr;
  }

VOID wv_free(PVOID ptr) {
    ExFreePool(ptr);
    return;
  }
