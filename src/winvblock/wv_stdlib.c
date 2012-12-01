/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
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

/**
 * @file
 *
 * Some C Standard Library work-alikes
 */

#include <ntddk.h>
#include "wv_stdlib.h"

PVOID wv_malloc(wv_size_t size) {
    return ExAllocatePoolWithTag(NonPagedPool, size, 'klBV');
  }

PVOID wv_palloc(wv_size_t size) {
    return ExAllocatePoolWithTag(PagedPool, size, 'klBV');
  }

PVOID wv_mallocz(wv_size_t size) {
    PVOID ptr = ExAllocatePoolWithTag(NonPagedPool, size, 'klBV');
    return ptr ? RtlZeroMemory(ptr, size), ptr : ptr;
  }

PVOID wv_pallocz(wv_size_t size) {
    PVOID ptr = ExAllocatePoolWithTag(PagedPool, size, 'klBV');
    return ptr ? RtlZeroMemory(ptr, size), ptr : ptr;
  }

VOID wv_free(VOID * ptr) {
    if (ptr)
      ExFreePool(ptr);
    return;
  }
