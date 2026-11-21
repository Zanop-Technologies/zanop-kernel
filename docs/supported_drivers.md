# Suported Network Drivers

**Last Updated:** November 21, 2026

**Author:** John Adman

## Intel E1000

**What is it?**
> The Intel e1000 (part of the Intel PRO/1000 line) refers to a family of Gigabit Ethernet network controllers and their corresponding Linux/Unix drivers. It is also widely emulated by virtual machines (such as QEMU, VirtualBox, and VMware) as a standard, broadly compatible virtual network card.

**Planned Support:**
> Please correct me in issues since I am not experienced in Networking.

- Gigabit Ethernet & DMA &rightarrow; Transfers transmit and receive packets directly to and from system RAM using Direct Memory Access (DMA) with circular descriptor rings.

- VLAN & Offload Support &rightarrow; Supports native VLAN tagging and hardware-based checksum offload to reduce host CPU overhead.

- Interrupt Throttling & NAPI &rightarrow; Supports interrupt rate limiting (`InterruptThrottleRate`) and Linux NAPI polling to prevent CPU overload under heavy packet bursts.

- Link & Power Management &rightarrow; Supports link bonding/teaming, Wake-on-LAN (WoL), multicast handling, and power-saving options via ethtool and driver parameters.

- Jumbo Frames &rightarrow; Supports MTU adjustments for jumbo frames (up to 9,216 bytes on supported configurations).

- Broad OS & Virtualization Compatibility &rightarrow; Supported natively across Linux (e1000 for PCI/PCI-X and e1000e for PCIe) and BSD/Unix systems, and commonly emulated in virtual machines for out-of-the-box driver compatibility.

**Warning:**
> Intel E1000 is only for our current VM support, if you want to suggest more drivers, please create an issue.