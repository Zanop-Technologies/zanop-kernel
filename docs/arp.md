# Address Resolution Protocol

## WARNING
> **ARP (Address Resolution Protocol) has zero authentication.** Meaning any device can claim they are the router which is called **ARP spoofing.**

## Packet Format
> *An ARP packet is 28 bytes and it sits directly inside an Ethernet frame, which has no IP header.*

**Ethernet Type:** 0x0806

**Hardware Type:** 1 (for ethernet, ofcourse)

**Protocol Type:** 0x0800 (for IPv4)

**Hardware Length:** 6 (MAC address is 6 bytes)

**Protocol Length:** 4 (IP address is 4 bytes)

**Operation:** `1` for a Request, `2` for a Reply.

**Sender MAC & IP:** The sender's addresses.

**Target MAC:** Set all to `0s` in a request (unknown), filled in for a reply.

**Target IP:** The IP you are trying to resolve.

## How it Works

**The Request:** When Zanop OS needs to send an IP packet but doesn't know the destination MAC, it sends an ARP request. The ethernet is set to the **Broadcast MAC** (`FF:FF:FF:FF:FF:FF`). Every device on the local network recives it.

**The Reply:** Only the device with the matching Target IP responds. It sends a **Unicast** ARP reply directly back to the sender's MAC address.

## ARP Cache

> [!TIP]
> *Do not send an ARP request for every single packet.*

**The Table:** Store resolved `IP —> MAC` mappings in a table (e.g., a hash map or array).

**Timeouts:** Dynamic entries should expire. Linux uses a base reachable time (typically ~30 secs) and a garbage collection threshold. For Zanop OS, a **10-minute** hard timeout is a safe starting point.

**Queueing:** If a packet needs to be sent while waiting for an ARP reply, you must **queue the packet** (or drop it). If you drop it, TCP will handle the retransmission.

## Gratuitous ARP

> *When Zanop OS boots up, or if its IP changes, it should send a Gratiutous ARP.*