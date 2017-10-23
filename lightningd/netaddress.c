#include <assert.h>
#include <lightningd/lightningd.h>
#include <lightningd/netaddress.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <wire/wire.h>

/* Based on bitcoin's src/netaddress.cpp, hence different naming and styling!
   version 7f31762cb6261806542cc6d1188ca07db98a6950:

   Copyright (c) 2009-2010 Satoshi Nakamoto
   Copyright (c) 2009-2016 The Bitcoin Core developers
   Distributed under the MIT software license, see the accompanying
   file COPYING or http://www.opensource.org/licenses/mit-license.php.
*/

/* The common IPv4-in-IPv6 prefix */
static const unsigned char pchIPv4[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

static bool IsRFC6145(const struct ipaddr *addr)
{
    static const unsigned char pchRFC6145[] = {0,0,0,0,0,0,0,0,0xFF,0xFF,0,0};
    return addr->type == ADDR_TYPE_IPV6
        && memcmp(addr->addr, pchRFC6145, sizeof(pchRFC6145)) == 0;
}

static bool IsRFC6052(const struct ipaddr *addr)
{
    static const unsigned char pchRFC6052[] = {0,0x64,0xFF,0x9B,0,0,0,0,0,0,0,0};
    return addr->type == ADDR_TYPE_IPV6
        && memcmp(addr->addr, pchRFC6052, sizeof(pchRFC6052)) == 0;
}

static bool IsRFC3964(const struct ipaddr *addr)
{
    return addr->type == ADDR_TYPE_IPV6
        && addr->addr[0] == 0x20 && addr->addr[1] == 0x02;
}

/* Return offset of IPv4 address, or 0 == not an IPv4 */
static size_t IPv4In6(const struct ipaddr *addr)
{
    if (addr->type != ADDR_TYPE_IPV6)
        return 0;
    if (memcmp(addr->addr, pchIPv4, sizeof(pchIPv4)) == 0)
        return sizeof(pchIPv4);
    if (IsRFC6052(addr))
        return 12;
    if (IsRFC6145(addr))
        return 12;
    if (IsRFC3964(addr))
        return 2;
    return 0;
}

/* Is this an IPv4 address, or an IPv6-wrapped IPv4 */
static bool IsIPv4(const struct ipaddr *addr)
{
    return addr->type == ADDR_TYPE_IPV4 || IPv4In6(addr) != 0;
}

static bool IsIPv6(const struct ipaddr *addr)
{
    return addr->type == ADDR_TYPE_IPV6 && IPv4In6(addr) == 0;
}

static bool RawEq(const struct ipaddr *addr, const void *cmp, size_t len)
{
    size_t off = IPv4In6(addr);

    assert(off + len <= addr->addrlen);
    return memcmp(addr->addr + off, cmp, len) == 0;
}

/* The bitcoin code packs addresses backwards, so we map it here. */
static unsigned int GetByte(const struct ipaddr *addr, int n)
{
    size_t off = IPv4In6(addr);
    assert(off + n < addr->addrlen);
    return addr->addr[addr->addrlen - 1 - off - n];
}

static bool IsRFC1918(const struct ipaddr *addr)
{
    return IsIPv4(addr) && (
        GetByte(addr, 3) == 10 ||
        (GetByte(addr, 3) == 192 && GetByte(addr, 2) == 168) ||
        (GetByte(addr, 3) == 172 && (GetByte(addr, 2) >= 16 && GetByte(addr, 2) <= 31)));
}

static bool IsRFC2544(const struct ipaddr *addr)
{
    return IsIPv4(addr) && GetByte(addr, 3) == 198 && (GetByte(addr, 2) == 18 || GetByte(addr, 2) == 19);
}

static bool IsRFC3927(const struct ipaddr *addr)
{
    return IsIPv4(addr) && (GetByte(addr, 3) == 169 && GetByte(addr, 2) == 254);
}

static bool IsRFC6598(const struct ipaddr *addr)
{
    return IsIPv4(addr) && GetByte(addr, 3) == 100 && GetByte(addr, 2) >= 64 && GetByte(addr, 2) <= 127;
}

static bool IsRFC5737(const struct ipaddr *addr)
{
    return IsIPv4(addr) && ((GetByte(addr, 3) == 192 && GetByte(addr, 2) == 0 && GetByte(addr, 1) == 2) ||
        (GetByte(addr, 3) == 198 && GetByte(addr, 2) == 51 && GetByte(addr, 1) == 100) ||
        (GetByte(addr, 3) == 203 && GetByte(addr, 2) == 0 && GetByte(addr, 1) == 113));
}

static bool IsRFC3849(const struct ipaddr *addr)
{
    return IsIPv6(addr) && GetByte(addr, 15) == 0x20 && GetByte(addr, 14) == 0x01 && GetByte(addr, 13) == 0x0D && GetByte(addr, 12) == 0xB8;
}

static bool IsRFC4862(const struct ipaddr *addr)
{
    static const unsigned char pchRFC4862[] = {0xFE,0x80,0,0,0,0,0,0};
    return IsIPv6(addr) && RawEq(addr, pchRFC4862, sizeof(pchRFC4862));
}

static bool IsRFC4193(const struct ipaddr *addr)
{
    return IsIPv6(addr) && ((GetByte(addr, 15) & 0xFE) == 0xFC);
}

static bool IsRFC4843(const struct ipaddr *addr)
{
    return IsIPv6(addr) && (GetByte(addr, 15) == 0x20 && GetByte(addr, 14) == 0x01 && GetByte(addr, 13) == 0x00 && (GetByte(addr, 12) & 0xF0) == 0x10);
}

static bool IsTor(const struct ipaddr *addr)
{
	/* FIXME */
	return false;
}

static bool IsLocal(const struct ipaddr *addr)
{
    // IPv4 loopback
   if (IsIPv4(addr) && (GetByte(addr, 3) == 127 || GetByte(addr, 3) == 0))
       return true;

   // IPv6 loopback (::1/128)
   static const unsigned char pchLocal[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
   if (IsIPv6(addr) && RawEq(addr, pchLocal, sizeof(pchLocal)))
       return true;

   return false;
}

static bool IsInternal(const struct ipaddr *addr)
{
    return addr->type == ADDR_TYPE_PADDING;
}

static bool IsValid(const struct ipaddr *addr)
{
    // unspecified IPv6 address (::/128)
    unsigned char ipNone6[16] = {};
    if (IsIPv6(addr) && RawEq(addr, ipNone6, sizeof(ipNone6)))
        return false;

    // documentation IPv6 address
    if (IsRFC3849(addr))
        return false;

    if (IsInternal(addr))
        return false;

    if (IsIPv4(addr))
    {
        // INADDR_NONE
        uint32_t ipNone = INADDR_NONE;
        if (RawEq(addr, &ipNone, sizeof(ipNone)))
            return false;

        // 0
        ipNone = 0;
        if (RawEq(addr, &ipNone, sizeof(ipNone)))
            return false;
    }

    return true;
}

static bool IsRoutable(const struct ipaddr *addr)
{
    return IsValid(addr) && !(IsRFC1918(addr) || IsRFC2544(addr) || IsRFC3927(addr) || IsRFC4862(addr) || IsRFC6598(addr) || IsRFC5737(addr) || (IsRFC4193(addr) && !IsTor(addr)) || IsRFC4843(addr) || IsLocal(addr) || IsInternal(addr));
}

/* Trick I learned from Harald Welte: create UDP socket, connect() and
 * then query address. */
static bool get_local_sockname(int af, void *saddr, socklen_t saddrlen)
{
    int fd = socket(af, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;

    if (connect(fd, saddr, saddrlen) != 0) {
        close(fd);
        return false;
    }

    if (getsockname(fd, saddr, &saddrlen) != 0) {
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

/* Return an ipaddr without port filled in */
static bool guess_one_address(struct ipaddr *addr, u16 portnum,
                              enum wire_addr_type type)
{
    addr->type = type;
    addr->port = portnum;

    /* We point to Google nameservers, works unless you're inside Google :) */
    switch (type) {
    case ADDR_TYPE_IPV4: {
        struct sockaddr_in sin;
	sin.sin_port = htons(53);
        /* 8.8.8.8 */
	sin.sin_addr.s_addr = 0x08080808;
        if (!get_local_sockname(AF_INET, &sin, sizeof(sin)))
            return false;
        addr->addrlen = sizeof(sin.sin_addr);
        memcpy(addr->addr, &sin.sin_addr, addr->addrlen);
        break;
    }
    case ADDR_TYPE_IPV6: {
        struct sockaddr_in6 sin6;
        /* 2001:4860:4860::8888 */
        static const unsigned char pchGoogle[16]
                = {0x20,0x01,0x48,0x60,0x48,0x60,0,0,0,0,0,0,8,8,8,8};
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_port = htons(53);
        memcpy(sin6.sin6_addr.s6_addr, pchGoogle, sizeof(pchGoogle));
        if (!get_local_sockname(AF_INET6, &sin6, sizeof(sin6)))
            return false;
        addr->addrlen = sizeof(sin6.sin6_addr);
        memcpy(addr->addr, &sin6.sin6_addr, addr->addrlen);
        break;
    }
    case ADDR_TYPE_PADDING:
        return false;
    }

    if (!IsRoutable(addr))
        return false;

    return true;
}

void guess_addresses(struct lightningd *ld)
{
    size_t n = tal_count(ld->wireaddrs);

    /* We allocate an extra, then remove if it's not needed. */
    tal_resize(&ld->wireaddrs, n+1);
    if (guess_one_address(&ld->wireaddrs[n], ld->portnum, ADDR_TYPE_IPV4)) {
        n++;
        tal_resize(&ld->wireaddrs, n+1);
    }
    if (!guess_one_address(&ld->wireaddrs[n], ld->portnum, ADDR_TYPE_IPV6))
        tal_resize(&ld->wireaddrs, n);
}