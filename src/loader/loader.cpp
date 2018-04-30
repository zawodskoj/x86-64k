#include <stdint.h>
#include <sys/std.hpp>
#include <mem/pm.hpp>
#include <sys/debug.hpp>
#include <stddef.h>
#include <boot/multiboot.h>
#include <sys/elf.hpp>
#include <sys/panic.hpp>
#include <loader.hpp>
#include <sys/paging/ptse_arc_pae.hpp>

extern "C" struct multiboot_info *mb_info_ptr;
extern "C" [[noreturn]] void long_enter(uint64_t);

LoaderData loader_data;

class pm_arc_allocator : public arc_allocator_t {
public:
    void *allocate() override { return reinterpret_cast<void*>(pm::alloc());}
    void deallocate(void *ptr) override {}
};

static constexpr uintptr_t m_loadAddr = 0x400000; // Real address at which kernel will be loaded
pdpt_arc_t<uint32_t> *arcp;

void load_elf(uintptr_t loadAddr, uintptr_t mod_start, size_t mod_size) {
    debug::printf("kernel located at %a\n", mod_start);
    elf::Elf64 *elf = reinterpret_cast<elf::Elf64 *>(mod_start);
    assert(elf->isValid());
    assert(elf->target() == ELFCLASS64);

    size_t ns = elf->programCount();
    debug::printf("Elf: %a\n", elf);
    debug::printf("Section count: %d\n", ns);

    const Elf64_Phdr *phdrs = elf->programHeaders();

    // TODO: make sure entry is 2MB-page-aligned
    uint64_t entry64 = elf->entry();
    uint64_t vma64 = 0x4000000000;
    uint64_t vma32 = 0x4000000;
    uint64_t vmadifference = vma64 - vma32;

    arcp->map(vma32,            0x1000000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arcp->map(vma32 + 0x200000, 0x1200000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arcp->map(vma32 + 0x400000, 0x1400000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arcp->map(vma32 + 0x600000, 0x1600000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arcp->map(vma32 + 0x800000, 0x1800000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arcp->apply();

    for (size_t i = 0; i < ns; ++i) {
        const Elf64_Phdr *programHeader = elf->programHeader(i);

        if (programHeader->p_type == PT_LOAD) {
            uint64_t paddr64 = programHeader->p_paddr;
            uint64_t vaddr64 = programHeader->p_vaddr;
            uint32_t paddr32 = static_cast<uint32_t>(paddr64);
            uint32_t vaddr32 = static_cast<uint32_t>(vaddr64);
            uint64_t size64 = programHeader->p_filesz;
            uint32_t size32 = static_cast<uint32_t>(size64);

            uint64_t off64 = programHeader->p_offset;
            uint32_t off32 = static_cast<uint32_t>(off64);

            uint64_t memsz64 = programHeader->p_memsz;
            uint32_t memsz32 = programHeader->p_memsz;

            const void *l = reinterpret_cast<const void *>(mod_start + off32);

            memset(reinterpret_cast<void*>(vaddr64 - vmadifference), 0, size32);
            memcpy(reinterpret_cast<void *>(vaddr64 - vmadifference), l, size32);

            // pdp->unmap(0x400000);
            //pdp->apply();
        }
    }
    // Convert pdp to pml4
    pm_disable();

    // WORLD IS NOT PREPARED TO THE FUTURE
    pm_arc_allocator alloca;
    pml4_arc_t *arc = new pml4_arc_t(&alloca);

    arc->map(0x0, 0x0, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3));
    arc->map(0x200000, 0x200000, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3));
    arc->map(vma64,            0x1000000, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arc->map(vma64 + 0x200000, 0x1200000, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arc->map(vma64 + 0x400000, 0x1400000, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arc->map(vma64 + 0x600000, 0x1600000, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arc->map(vma64 + 0x800000, 0x1800000, pml4_arc_t::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3)); // TODO: map more pages if needed
    arc->apply();
    
    /*pm::pm64::Pml4 *pml = new (0x100000) pm::pm64::Pml4;
    pml->map(0x0, 0x0, 0x86);
    pml->map(0x200000, 0x200000, 0x86);
    pml->map(vma64,            0x1000000, 0x86); // TODO: map more pages if needed
    pml->map(vma64 + 0x200000, 0x1200000, 0x86); // TODO: map more pages if needed
    pml->map(vma64 + 0x400000, 0x1400000, 0x86); // TODO: map more pages if needed
    pml->map(vma64 + 0x600000, 0x1600000, 0x86); // TODO: map more pages if needed
    pml->map(vma64 + 0x800000, 0x1800000, 0x86); // TODO: map more pages if needed
    pml->apply();*/

    debug::printf("Entry: %A\n", entry64);
    // Prepare loader info struct for kernel
    loader_data.loaderMagic = LOADER_MAGIC;
    loader_data.loaderPagingBase = pm::paging_region.start;
    loader_data.loaderPagingSize = pm::paging_region.length();
    loader_data.loaderPagingTracking = reinterpret_cast<uint32_t>(pm::trackingPtr());
    loader_data.multibootInfo = reinterpret_cast<uint32_t>(mb_info_ptr);
    loader_data.checksum = 0;
    uint32_t sum = 0;
    for (size_t off = 0; off < sizeof(LoaderData); ++off) {
        sum += reinterpret_cast<const uint8_t *>(&loader_data)[off];
    }
    sum &= 0xFF;
    loader_data.checksum = -sum;
    long_enter(entry64);
}

uint32_t calculateLoadAddr(struct multiboot_mod_list *mods, uint32_t modCount) {
    uint32_t addr = 0x400000;
    struct multiboot_mod_list *mod;
    for (uint32_t i = 0; i < modCount; ++i) {
        mod = &mods[i];

        debug::printf(" * module %a - %a\n", mod->mod_start, mod->mod_end);        
        if (mod->cmdline && strlen(reinterpret_cast<const char *>(mod->cmdline))) {
            debug::printf(" params = %s\n", mod->cmdline);
        }

        if (mod->mod_end > addr) {
            addr = alignUp(mod->mod_end, 0x200000u); // Align up to 2MiB boundary
        }
    }
    return addr;
}

extern "C" void __cxa_pure_virtual() {
    panic_msg("__cxa_pure_virtual: pure virtual call encountered.");
}

void *operator new(size_t size, std::align_val_t alignment) {
    debug::printf("new with size: %a and alignment: %a\n", size, alignment);
    return reinterpret_cast<void*>(pm::alloc());
}

void operator delete(void *ptr, size_t size, std::align_val_t alignment) { }
void operator delete(void *ptr, size_t size) { }
void operator delete(void *ptr) { }

extern "C" void loader_main(void) {
    debug::printf("at loader_main\n");
    // Map loader here
    
    pm::setAlloc(0x100000); // Because pdpt

    pm_arc_allocator alloca;
    pdpt_arc_t<uint32_t> arc(static_cast<arc_allocator_t*>(&alloca));
    arcp = &arc;
    debug::printf("mapping 0 -> 0\n");
    auto res = arc.map(0x00000000, 0x00000000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3));
    if (res != ptse_arc_status_t::OK) panic_msg("arc.map result: %d", res);
    debug::printf("mapping 0x00200000 -> 0x00200000\n");
    res = arc.map(0x00200000, 0x00200000, pdpt_arc_t<uint32_t>::LEVEL_2M, page_struct_flags_t(PTSE_FLAG_RW | PTSE_FLAG_RING3));
    if (res != ptse_arc_status_t::OK) panic_msg("arc.map result: %d", res);
    arc.apply();

    pae_enable();
    pm_enable();

    assert(mb_info_ptr->mods_count != 0);

    // Find physical address where the kernel can be loaded
    struct multiboot_mod_list *mb_mod = reinterpret_cast<struct multiboot_mod_list *>(mb_info_ptr->mods_addr);
    uint32_t availablePhysAddr = calculateLoadAddr(mb_mod, mb_info_ptr->mods_count);
    debug::printf("Physical address to load kernel: %a\n", availablePhysAddr);

    // Find kernel
    load_elf(availablePhysAddr, mb_mod->mod_start, mb_mod->mod_end - mb_mod->mod_start);

    while (1) {
        __asm__ __volatile__ ("cli; hlt");
    }
}
