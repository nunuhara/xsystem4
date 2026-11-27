/* AIN v12 support. Licensed under GPL-2.0-or-later, like xsystem4. */
#ifndef XSYSTEM4_VM_V12_H
#define XSYSTEM4_VM_V12_H
#include "vm.h"
#include "system4/ain.h"
struct page;
struct page *v12_array_new(const struct ain_type *element, int count, bool construct);
int v12_array_length(struct page *page);
bool v12_builtin_library(const char *name);
bool v12_hll_call(int library, int function, int type_parameter);
void vm_system_call(int code);
#endif
