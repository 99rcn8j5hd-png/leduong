#pragma once
#include "mach.h"
#define VM_REGION_BASIC_INFO_64 9
#define VM_REGION_BASIC_INFO_COUNT_64 9
typedef struct { int protection; int max_protection; int inheritance; unsigned shared:1; unsigned reserved:1; unsigned offset:1; unsigned is_submap:1; unsigned behavior:2; unsigned user_wired_count:1; unsigned reserved2:1; } vm_region_basic_info_data_64_t;
typedef int* vm_region_info_t;
kern_return_t mach_vm_region(task_t, mach_vm_address_t*, mach_vm_size_t*, int, vm_region_info_t, mach_msg_type_number_t*, mach_port_t*);
