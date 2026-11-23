# Suported Network Drivers

**Last Updated:** November 22, 2026

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

## Intel E1000e

**What is it?**
> The e1000e driver is a Linux kernel network driver for Intel PCI Express (PCIe) Gigabit Ethernet adapters, including the PRO/1000 (82563–82583) and I217–I219 series. It replaces the older e1000 driver for PCIe hardware.

**Planned Features:**

- Hardware Offloading —> Supports TCP Segmentation Offload (TSO), Generic Receive Offload (GRO), and hardware checksum offloading (RX/TX) to reduce CPU load during high-throughput transfers.

- Jumbo Frames —> Supports larger Maximum Transmission Units (MTU)—typically up to 8996 or 9216 bytes depending on the specific Intel controller—to improve bulk data transfer efficiency.

- VLANs & Channel Bonding —> Integrates natively with Linux 802.1Q VLAN tagging and network interface bonding/teaming for failover or link aggregation.

- NAPI Polling —> Uses the Linux NAPI (New API) framework to switch from interrupt-driven packet processing to polling under heavy traffic, preventing interrupt storms. (Note: This is from a google search.)

- Precision Time Protocol (PTP) —> Provides hardware timestamping (ptp module dependency) on supported chipsets for sub-microsecond clock synchronization.

**Warning:**
> Please be aware that this information is based off a google search. The driver for this is only for VMs and ethernet, not WIFI.