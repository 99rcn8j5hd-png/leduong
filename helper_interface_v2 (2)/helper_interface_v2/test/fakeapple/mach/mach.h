#pragma once
#include <cstdint>
#include <cstddef>
typedef int kern_return_t;
typedef unsigned int mach_port_t;
typedef uint64_t mach_vm_address_t;
typedef uint64_t mach_vm_size_t;
typedef unsigned int mach_msg_type_number_t;
typedef mach_port_t task_t;
#define KERN_SUCCESS 0
#define MACH_PORT_NULL 0
#define VM_PROT_READ 1
#define VM_PROT_NONE 0
const char* mach_error_string(kern_return_t);
mach_port_t mach_task_self(void);
kern_return_t mach_port_deallocate(task_t, mach_port_t);
kern_return_t task_for_pid(task_t, int, mach_port_t*);
kern_return_t mach_vm_read_overwrite(task_t, mach_vm_address_t, mach_vm_size_t, mach_vm_address_t, mach_vm_size_t*);
