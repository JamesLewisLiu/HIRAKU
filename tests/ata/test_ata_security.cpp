// SPDX-License-Identifier: GPL-3.0-or-later
// ATA security on a hard disk, as MAME's ata_mass_storage does it (DJ Main
// complete MIX and later): a disk with passwords set is locked at power-on and
// soft reset, reads fail with ERR/ABRT, and SECURITY UNLOCK (F2) with the
// right 32 bytes clears that password. Both must be cleared to read.
#include <cstdio>
#include <cstring>
#include <vector>

#include <unistd.h>

#include "dev/ata.h"

using namespace hk;

namespace {
int g_fail = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } \
    } while (0)

bool g_irq = false;

void select_lba(AtaBus &b, uint32_t lba, uint8_t count) {
    b.write(0, 2, count);
    b.write(0, 3, lba & 0xff);
    b.write(0, 4, (lba >> 8) & 0xff);
    b.write(0, 5, (lba >> 16) & 0xff);
    b.write(0, 6, 0x40 | ((lba >> 24) & 0x0f));   // LBA, master
}

// SECURITY UNLOCK with a 512-byte block: word 0 bit 0 = master, bytes 2..33 the password
void unlock(AtaBus &b, bool master, const std::vector<uint8_t> &pw) {
    std::vector<uint8_t> blk(512, 0);
    blk[0] = master ? 1 : 0;
    memcpy(blk.data() + 2, pw.data(), 32);
    g_irq = false;
    b.write(0, 7, 0xf2);
    CHECK(g_irq);                                // MAME: DRQ and an interrupt at once
    CHECK(b.read(0, 7) & 0x08);
    for (int k = 0; k < 512; k += 2) b.write(0, 0, (uint16_t)(blk[k] | blk[k + 1] << 8));
}

bool read_ok(AtaBus &b, uint32_t lba, uint8_t expect) {
    select_lba(b, lba, 1);
    g_irq = false;
    b.write(0, 7, 0x20);                         // READ SECTORS
    const uint8_t st = (uint8_t)b.read(0, 7);
    if (st & 0x01) {                             // ERR
        CHECK(b.read(0, 1) == 0x04);             // ABRT
        CHECK(!g_irq);                           // MAME's security_error raises none
        return false;
    }
    CHECK(st & 0x08);
    uint16_t w = b.read(0, 0);
    for (int k = 1; k < 256; k++) b.read(0, 0);
    return (w & 0xff) == expect;
}
}  // namespace

int main() {
    // a 64-sector disk image, each sector filled with its LBA
    char path[] = "/tmp/hk_ata_secXXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 2;
    for (int s = 0; s < 64; s++) {
        std::vector<uint8_t> sec(512, (uint8_t)s);
        if (write(fd, sec.data(), sec.size()) != 512) return 2;
    }
    close(fd);

    std::vector<uint8_t> master(32), user(32), wrong(32, 0x55);
    for (int k = 0; k < 32; k++) master[k] = (uint8_t)(0x40 + k), user[k] = (uint8_t)(0x10 + 3 * k);

    AtaBus bus;
    bus.irq = [](bool on) { if (on) g_irq = true; };
    auto disk = std::make_unique<AtaDevice>("hdd", false);
    disk->disk = std::make_unique<DiskImage>();
    if (!disk->disk->open(path)) return 2;
    disk->master_password = master;
    disk->user_password = user;
    bus.attach(0, std::move(disk));

    CHECK(!read_ok(bus, 5, 5));                  // locked from power-on
    unlock(bus, false, wrong);
    CHECK(!read_ok(bus, 5, 5));                  // wrong password: still locked
    unlock(bus, false, user);
    CHECK(!read_ok(bus, 5, 5));                  // user cleared, master still set
    unlock(bus, true, master);
    CHECK(read_ok(bus, 5, 5));                   // both cleared: the data
    CHECK(read_ok(bus, 63, 63));

    bus.write(1, 6, 0x04);                       // soft reset: locked again
    bus.write(1, 6, 0x00);
    CHECK(!read_ok(bus, 5, 5));

    unlink(path);
    if (g_fail) { fprintf(stderr, "%d failure(s)\n", g_fail); return 1; }
    printf("ata security: ok\n");
    return 0;
}
