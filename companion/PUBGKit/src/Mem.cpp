#include "Mem.hpp"

bool memRead(uintptr_t addr, void* out, size_t len) {
    if (!addr || !out || !len || len > UINTPTR_MAX-addr) return false;
    vm_size_t got = 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(),
                                              (vm_address_t)addr,
                                              (vm_size_t)len,
                                              (vm_address_t)(uintptr_t)out,
                                              &got);
    return kr == KERN_SUCCESS && got == (vm_size_t)len;
}

bool memWrite(uintptr_t addr, const void* in, size_t len) {
    if (!addr || !in || !len || len > UINTPTR_MAX-addr || len > UINT32_MAX) return false;
    uintptr_t pageStart = addr & ~(uintptr_t)(vm_page_size - 1);
    uintptr_t pageEnd   = (addr + len + vm_page_size - 1) & ~(uintptr_t)(vm_page_size - 1);
    if (pageEnd < addr || pageEnd <= pageStart) return false;
    vm_size_t span = (vm_size_t)(pageEnd - pageStart);

    vm_address_t regionStart = (vm_address_t)pageStart;
    vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info = {};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t query = vm_region_64(mach_task_self(), &regionStart, &regionSize,
        VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    if (query != KERN_SUCCESS || regionStart > pageStart ||
        pageEnd - regionStart > regionSize) return false;

    kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)pageStart, span, FALSE,
                                  VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS) return false;

    kr = vm_write(mach_task_self(), (vm_address_t)addr, (vm_offset_t)in,
                  (mach_msg_type_number_t)len);
    kern_return_t restored = vm_protect(mach_task_self(), (vm_address_t)pageStart,
        span, FALSE, info.protection);
    return kr == KERN_SUCCESS && restored == KERN_SUCCESS;
}

bool memValid(uintptr_t addr, size_t len) {
    if (!addr || !len || len > UINTPTR_MAX-addr) return false;
    uint8_t buf[8];
    size_t n = len > sizeof(buf) ? sizeof(buf) : len;
    if (!memRead(addr, buf, n)) return false;
    // đọc thử thêm 1 byte ở cuối vùng để chắc chắn range còn sống
    if (len > sizeof(buf)) {
        uint8_t tail;
        if (!memRead(addr + len - 1, &tail, 1)) return false;
    }
    return true;
}
