# DHCP (Dynamic Host Configuration Protocol)
> [!NOTE]
> This guide is based of RFC 2131.

## RFC 2131

The protocol must use UDP:
 - It uses Port 68 (Client)
 - For the server, it uses Port 67

Transporting it connectionless:
 - No TCP handshake.
 - Fire-and-forget UDP packets.

IP address must start with no IP (`0.0.0.0`):
 - The client starts with **No IP at all**, then it sends packets from `0.0.0.0` to 255.255.255.255 (Broadcast).

Required MAC address:
 - The server identifies you by your **MAC address**, the NIC driver must exposse this.

Transaction ID:
 - Has to be Random 32-bit
 - Used to match requests with replies and has to be unique per session.

Magic Cookie:
 - `99.130.83.99`
 - A fixed 4-byte value inside the DHCP packet that says "Hey, I`m a DHCP message, not garbage."

## DORA (Discover. Offer, Request, Ack)
1. DISCOVER:
 - Discover says: "Is there a DHCP server out there? I need an IP!"
   - Source IP: `0.0.0.0`
   - Dest IP: `255.255.255.0`
 - Payload: Your MAC address + Random Transaction ID

2. OFFER:
 - Offer says: "Yes! I have an IP for you: `192.168.1.50`. Here are the details."
   - Server offers: IP, Subnet Mask, Gateway (Router), DNS Server, Lease Time.

3. REQUEST:
 - Request says: "Cool, I'll take `192.168.1.50`. Please confirm."
   > [!NOTE]
   > This confirms the specific IP that is offered.

4. ACK:
 - "Confirmed. `192.168.1.50` is yours for 24 hours. Go ahead."
   - The kernel can now configure its network interface with this IP.

## Packet Structure
> [!IMPORTANT]
> You need to define a `struct` in C++ that matches the BOOTP/DHCP header (it is usually 300 bytes):
> ```cpp
> struct DhcpPacket {
>     uint8_t op;
>     uint8_t htype;
>     uint8_t hlen;
>     uint8_t hops;
>     uint32_t xid;
>     uint16_t secs;
>     uint16_t flags;
>     uint32_t ciaddr;
>     uint32_t siaddr;
>     uint32_t giaddr;
>     uint8_t chaddr[16];
>     uint8_t sname[64];
>     uint8_t file[128];
>     uint32_t magic_cookie;
>     uint8_t options[312]; 
> };
> ```

## CRITICAL OPTIONS
> [!IMPORTANT]
> You must parse these specific tags to get the internet working.
> - **Option 1**: Subnet Mask (e.g., `255.255.255.0`)
> - **Option 3**
> - **Option 6**
> - **Option 51**