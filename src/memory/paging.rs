const ENTRY_COUNT: usize = 512;
const PAGE_SIZE: usize = 4096;

bitflags::bitflags! {
    pub struct PageFlags: u64 {
        const PRESENT    = 1 << 0;
        const WRITABLE   = 1 << 1;
        const USER       = 1 << 2;
        const HUGE_PAGE  = 1 << 7;
        const NO_EXECUTE = 1 << 63;
    }
}

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct PageTableEntry(u64);

impl PageTableEntry {
    pub const fn empty() -> Self {
        PageTableEntry(0)
    }

    pub fn set(&mut self, addr: u64, flags: PageFlags) {
        self.0 = (addr & 0x000f_ffff_ffff_f000) | flags.bits();
    }

    pub fn is_present(&self) -> bool {
        self.0 & PageFlags::PRESENT.bits() != 0
    }

    pub fn addr(&self) -> u64 {
        self.0 & 0x000f_ffff_ffff_f000
    }
}

#[repr(align(4096))]
pub struct PageTable {
    entries: [PageTableEntry; ENTRY_COUNT],
}

impl PageTable {
    pub const fn new() -> Self {
        PageTable {
            entries: [PageTableEntry::empty(); ENTRY_COUNT],
        }
    }
}

pub fn init() {
    // TODO: read CR3, walk/construct PML4 -> PDPT -> PD -> PT
    // TODO: identity-map or higher-half map kernel sections per linker.ld
}

pub fn current_page_table_addr() -> u64 {
    let addr: u64;
    unsafe {
        core::arch::asm!("mov {}, cr3", out(reg) addr);
    }
    addr
}