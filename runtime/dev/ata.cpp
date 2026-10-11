// SPDX-License-Identifier: GPL-3.0-or-later
#include "ata.h"

#include <algorithm>
#include <cstring>

namespace hk {

// ---------------------------------------------------------------------------
// Disc images
// ---------------------------------------------------------------------------

DiscImage::~DiscImage() {
    if (f_) fclose(f_);
}

bool DiscImage::open(const std::string &path0) {
    std::string path = path0;
    if (path.size() > 4 && (path.substr(path.size() - 4) == ".cue" || path.substr(path.size() - 4) == ".CUE")) {
        FILE *c = fopen(path.c_str(), "r");
        if (!c) return false;
        char line[1024];
        std::string bin;
        while (fgets(line, sizeof line, c)) {
            char *q1 = strchr(line, '"'), *q2 = q1 ? strrchr(line, '"') : nullptr;
            if (strncmp(line, "FILE", 4) == 0 && q1 && q2 > q1) { bin.assign(q1 + 1, q2); break; }
        }
        fclose(c);
        if (bin.empty()) return false;
        auto slash = path.find_last_of('/');
        path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + bin;
    }
    f_ = fopen(path.c_str(), "rb");
    if (!f_) return false;
    fseeko(f_, 0, SEEK_END);
    uint64_t size = (uint64_t)ftello(f_);
    uint8_t head[16];
    fseeko(f_, 0, SEEK_SET);
    if (fread(head, 1, 16, f_) != 16) return false;
    static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    if (memcmp(head, sync, 12) == 0 && size % 2352 == 0) { stride_ = 2352; skip_ = 16; }
    else if (size % 2048 == 0) { stride_ = 2048; skip_ = 0; }
    else return false;
    nsectors_ = (uint32_t)(size / stride_);
    return true;
}

bool DiscImage::read_sector(uint32_t lba, uint8_t *out) const {
    if (lba >= nsectors_) return false;
    fseeko(f_, (off_t)lba * stride_ + skip_, SEEK_SET);
    return fread(out, 1, 2048, f_) == 2048;
}

DiskImage::~DiskImage() {
    if (f_) fclose(f_);
}

bool DiskImage::open(const std::string &path) {
    f_ = fopen(path.c_str(), "rb");
    if (!f_) return false;
    fseeko(f_, 0, SEEK_END);
    nsectors_ = (uint32_t)((uint64_t)ftello(f_) / 512);
    return nsectors_ > 0;
}

bool DiskImage::read_sector(uint32_t lba, uint8_t *out) const {
    if (lba >= nsectors_) return false;
    fseeko(f_, (off_t)lba * 512, SEEK_SET);
    return fread(out, 1, 512, f_) == 512;
}

// ---------------------------------------------------------------------------
// ATA registers
// ---------------------------------------------------------------------------

namespace {
enum : uint8_t { ST_ERR = 0x01, ST_DRQ = 0x08, ST_DSC = 0x10, ST_DRDY = 0x40, ST_BSY = 0x80 };
enum : uint8_t { IR_COD = 0x01, IR_IO = 0x02 };
}  // namespace

AtaBus::AtaBus() = default;

void AtaBus::attach(int unit, std::unique_ptr<AtaDevice> d) {
    // power-on state: a hard disk is ready (DRDY|DSC); an ATAPI device shows 0 and its signature
    if ((unit & 1) == 0 && !d->atapi_) status_ = ST_DRDY | ST_DSC;
    drives_[unit & 1] = std::move(d);
}

AtaDevice *AtaBus::cur() { return drives_[(device_ >> 4) & 1].get(); }

uint32_t AtaBus::lba() const {
    if (device_ & 0x40) return (uint32_t)(device_ & 0x0f) << 24 | (uint32_t)cyl_hi_ << 16 | (uint32_t)cyl_lo_ << 8 | sector_;
    // CHS with the geometry IDENTIFY reports: 16 heads, 63 sectors per track
    uint32_t cyl = (uint32_t)cyl_hi_ << 8 | cyl_lo_;
    return (cyl * 16 + (device_ & 0x0f)) * 63 + (sector_ ? sector_ - 1 : 0);
}

uint16_t AtaBus::read_dma() {
    if (phase_ != Phase::dma_in || data_pos_ >= data_.size()) return 0;
    uint16_t v = data_[data_pos_] | (data_pos_ + 1 < data_.size() ? data_[data_pos_ + 1] << 8 : 0);
    data_pos_ += 2;
    if (data_pos_ >= data_.size()) {
        if (dmarq) dmarq(false);
        finish(false);
    }
    return v;
}

void AtaBus::set_irq(bool on) {
    bool line = on && !(devctl_ & 0x02);    // nIEN masks the line
    if (on != irq_ || line != irq_) {}
    irq_ = on;
    if (irq) irq(line);
}

uint16_t AtaBus::read(int cs, int reg) {
    if (!cur() && !(cs == 0 && reg == 6)) return 0;     // nothing on this unit
    if (cs == 1) {
        if (reg == 6) return status_;                    // alternate status: no side effects
        return 0;
    }
    switch (reg) {
    case 0: {                                            // data
        if (phase_ != Phase::data_in || data_pos_ + 1 >= data_.size() + 1) return 0;
        uint16_t v = data_[data_pos_] | (data_pos_ + 1 < data_.size() ? data_[data_pos_ + 1] << 8 : 0);
        data_pos_ += 2;
        if (data_pos_ >= chunk_end_) {
            if (data_pos_ >= data_.size()) {
                finish(false);
            } else if (!cur()->atapi_) {                 // hard disk: next block of sectors
                chunk_end_ = std::min(data_.size(), data_pos_ + (size_t)512 * multiple_);
                status_ = ST_DRDY | ST_DSC | ST_DRQ;
                set_irq(true);
            } else {                                     // next DRQ block
                size_t n = std::min<size_t>(byte_limit_, data_.size() - data_pos_);
                chunk_end_ = data_pos_ + n;
                cyl_lo_ = n & 0xff;
                cyl_hi_ = (n >> 8) & 0xff;
                status_ = ST_DRQ;
                nsector_ = IR_IO;
                set_irq(true);
            }
        }
        return v;
    }
    case 1: return error_;
    case 2: return nsector_;
    case 3: return sector_;
    case 4: return cyl_lo_;
    case 5: return cyl_hi_;
    case 6: return device_;
    case 7:                                              // status: reading acknowledges the interrupt
        set_irq(false);
        return status_;
    }
    return 0;
}

void AtaBus::write(int cs, int reg, uint16_t v) {
    if (cs == 1) {
        if (reg == 6) {                                  // device control
            uint8_t old = devctl_;
            devctl_ = v & 0xff;
            if ((old & 0x04) && !(devctl_ & 0x04)) {     // end of soft reset: device signature
                phase_ = Phase::idle;
                error_ = 1;
                nsector_ = 1;
                sector_ = 1;
                device_ = 0;
                bool atapi = drives_[0] && drives_[0]->atapi_;
                status_ = atapi ? 0 : (ST_DRDY | ST_DSC);
                cyl_lo_ = atapi ? 0x14 : 0;
                cyl_hi_ = atapi ? 0xeb : 0;
                set_irq(false);
            }
            set_irq(irq_);
        }
        return;
    }
    switch (reg) {
    case 0:                                              // data
        if (phase_ == Phase::packet_in && packet_pos_ < 12) {
            packet_[packet_pos_++] = v & 0xff;
            packet_[packet_pos_++] = v >> 8;
            if (packet_pos_ >= 12) packet_done();
        }
        break;
    case 1: feature_ = v & 0xff; break;
    case 2: nsector_ = v & 0xff; break;
    case 3: sector_ = v & 0xff; break;
    case 4: cyl_lo_ = v & 0xff; break;
    case 5: cyl_hi_ = v & 0xff; break;
    case 6: device_ = v & 0xff; break;
    case 7:
        if (cur()) command(v & 0xff);
        break;
    }
}

void AtaBus::finish(bool error, uint8_t key, uint8_t asc) {
    int u = (device_ >> 4) & 1;
    phase_ = Phase::idle;
    data_.clear();
    if (cur() && !cur()->atapi_) {                       // hard disk: DRDY/DSC, error register = ABRT etc.
        status_ = ST_DRDY | ST_DSC | (error ? ST_ERR : 0);
        error_ = error ? key : 0;
        set_irq(true);
        return;
    }
    sense_key_[u] = key;
    asc_[u] = asc;
    status_ = error ? ST_ERR : 0;
    error_ = error ? (uint8_t)(key << 4) : 0;
    nsector_ = IR_IO | IR_COD;
    set_irq(true);
}

void AtaBus::start_data_in(std::vector<uint8_t> d) {
    if (d.empty()) { finish(false); return; }
    // the host may limit the size of each DRQ block (byte count registers on entry)
    byte_limit_ = (uint16_t)(cyl_lo_ | cyl_hi_ << 8);
    if (byte_limit_ == 0 || byte_limit_ == 0xffff) byte_limit_ = 0xfffe;
    byte_limit_ &= ~1;
    data_ = std::move(d);
    data_pos_ = 0;
    size_t n = std::min<size_t>(byte_limit_, data_.size());
    chunk_end_ = n;
    cyl_lo_ = n & 0xff;
    cyl_hi_ = (n >> 8) & 0xff;
    phase_ = Phase::data_in;
    status_ = ST_DRQ;
    nsector_ = IR_IO;
    set_irq(true);
}

void AtaBus::disk_command(uint8_t cmd) {
    AtaDevice *d = cur();
    switch (cmd) {
    case 0xec: {                                         // IDENTIFY DEVICE
        std::vector<uint8_t> id(512, 0);
        auto w = [&](int i, uint16_t v) { id[2 * i] = v & 0xff; id[2 * i + 1] = v >> 8; };
        auto str = [&](int i, int words, const char *s) {
            for (int k = 0; k < words * 2; k++) id[2 * i + (k ^ 1)] = (uint8_t)((size_t)k < strlen(s) ? s[k] : ' ');
        };
        uint32_t n = d->disk->sectors();
        uint32_t cyls = std::min<uint32_t>(n / (16 * 63), 16383);
        w(0, 0x0040);                                    // fixed disk
        w(1, (uint16_t)cyls);
        w(3, 16);
        w(6, 63);
        str(10, 10, "HIRAKU0000000001");
        str(23, 4, "1.0");
        str(27, 20, "HIRAKU ATA DISK");
        w(47, 0x8010);                                   // READ/WRITE MULTIPLE: up to 16 sectors
        w(49, 0x0300);                                   // LBA, DMA
        w(53, 0x0003);
        w(54, (uint16_t)cyls);
        w(55, 16);
        w(56, 63);
        w(57, (uint16_t)(cyls * 16 * 63));
        w(58, (uint16_t)((cyls * 16 * 63) >> 16));
        w(60, (uint16_t)n);
        w(61, (uint16_t)(n >> 16));
        w(63, 0x0007);                                   // multiword DMA 0-2
        data_ = std::move(id);
        data_pos_ = 0;
        chunk_end_ = 512;
        phase_ = Phase::data_in;
        status_ = ST_DRDY | ST_DSC | ST_DRQ;
        set_irq(true);
        return;
    }
    case 0x20: case 0x21:                                // READ SECTORS
    case 0xc4:                                           // READ MULTIPLE
    case 0xc8: case 0xc9: {                              // READ DMA
        uint32_t count = nsector_ ? nsector_ : 256, first = lba();
        std::vector<uint8_t> buf((size_t)count * 512);
        for (uint32_t k = 0; k < count; k++)
            if (!d->disk->read_sector(first + k, buf.data() + (size_t)k * 512)) {
                finish(true, 0x10);                      // IDNF: sector not found
                return;
            }
        data_ = std::move(buf);
        data_pos_ = 0;
        if (cmd == 0xc8 || cmd == 0xc9) {
            phase_ = Phase::dma_in;
            status_ = ST_DRDY | ST_DSC | ST_DRQ;
            if (dmarq) dmarq(true);
        } else {
            phase_ = Phase::data_in;
            chunk_end_ = std::min(data_.size(), (size_t)512 * (cmd == 0xc4 ? multiple_ : 1));
            status_ = ST_DRDY | ST_DSC | ST_DRQ;
            set_irq(true);
        }
        return;
    }
    case 0xc6:                                           // SET MULTIPLE MODE
        multiple_ = nsector_ ? nsector_ : 1;
        finish(false);
        return;
    case 0x40: case 0x41:                                // READ VERIFY
    case 0x10:                                           // RECALIBRATE
    case 0x91:                                           // INITIALIZE DEVICE PARAMETERS
    case 0xef:                                           // SET FEATURES
    case 0xe0: case 0xe1: case 0xe3:                     // STANDBY / IDLE
    case 0xe7:                                           // FLUSH CACHE
        finish(false);
        return;
    case 0x00:                                           // NOP: always aborts (ATA spec)
        finish(true, 0x04);
        return;
    default:
        fprintf(stderr, "ata: %s: unsupported ATA command %02x\n", d->name_.c_str(), cmd);
        finish(true, 0x04);                              // ABRT
        return;
    }
}

void AtaBus::command(uint8_t cmd) {
    AtaDevice *d = cur();
    if (log) fprintf(stderr, "ata: %s command %02x\n", d->name_.c_str(), cmd);
    if (!d->atapi_) { disk_command(cmd); return; }
    switch (cmd) {
    case 0xa0:                                           // PACKET
        phase_ = Phase::packet_in;
        packet_pos_ = 0;
        status_ = ST_DRQ;
        nsector_ = IR_COD;
        break;
    case 0xa1: {                                         // IDENTIFY PACKET DEVICE
        std::vector<uint8_t> id(512, 0);
        auto w = [&](int i, uint16_t v) { id[2 * i] = v & 0xff; id[2 * i + 1] = v >> 8; };
        auto str = [&](int i, int words, const char *s) {
            for (int k = 0; k < words * 2; k++) {
                char ch = (size_t)k < strlen(s) ? s[k] : ' ';
                id[2 * i + (k ^ 1)] = (uint8_t)ch;   // ATA strings: byte-swapped words
            }
        };
        w(0, 0x8580);                                    // ATAPI, CD-ROM, removable, 12-byte packets
        str(23, 4, "1.0");
        str(27, 20, "HIRAKU ATAPI CD-ROM");
        w(49, 0x0300);                                   // LBA + DMA capable
        byte_limit_ = 512;
        cyl_lo_ = 0;
        cyl_hi_ = 2;
        start_data_in(std::move(id));
        break;
    }
    case 0x08:                                           // DEVICE RESET
        finish(false);
        status_ = 0;
        cyl_lo_ = 0x14;
        cyl_hi_ = 0xeb;
        break;
    case 0xef:                                           // SET FEATURES
    case 0xe1:                                           // IDLE IMMEDIATE
    case 0xe0:                                           // STANDBY IMMEDIATE
        finish(false);
        break;
    default:
        fprintf(stderr, "ata: %s: unsupported ATA command %02x\n", d->name_.c_str(), cmd);
        finish(true, 0x05, 0x20);
        break;
    }
}

void AtaBus::packet_done() {
    AtaDevice *d = cur();
    const uint8_t *p = packet_;
    int u = (device_ >> 4) & 1;
    if (log) {
        fprintf(stderr, "ata: %s packet", d->name_.c_str());
        for (int k = 0; k < 12; k++) fprintf(stderr, " %02x", p[k]);
        fprintf(stderr, "\n");
    }
    const bool medium = d->disc != nullptr;
    auto be16 = [&](int i) { return (uint32_t)p[i] << 8 | p[i + 1]; };
    auto be32 = [&](int i) { return be16(i) << 16 | be16(i + 2); };
    switch (p[0]) {
    case 0x00:                                           // TEST UNIT READY
        if (!medium) finish(true, 0x02, 0x3a);
        else finish(false);
        break;
    case 0x03: {                                         // REQUEST SENSE
        std::vector<uint8_t> s(18, 0);
        s[0] = 0x70;
        s[2] = sense_key_[u];
        s[7] = 10;
        s[12] = asc_[u];
        s.resize(std::min<size_t>(s.size(), p[4] ? p[4] : 18));
        sense_key_[u] = asc_[u] = 0;
        start_data_in(std::move(s));
        break;
    }
    case 0x12: {                                         // INQUIRY
        std::vector<uint8_t> s(36, 0);
        s[0] = 0x05;                                     // CD/DVD device
        s[1] = 0x80;                                     // removable
        s[3] = 0x21;
        s[4] = 31;
        memcpy(&s[8], "HIRAKU  CD-ROM          1.0 ", 28);
        s.resize(std::min<size_t>(s.size(), p[4] ? p[4] : 36));
        start_data_in(std::move(s));
        break;
    }
    case 0x1b:                                           // START STOP UNIT
    case 0x1e:                                           // PREVENT ALLOW MEDIUM REMOVAL
    case 0x2b:                                           // SEEK
    case 0xbb:                                           // SET CD SPEED
    case 0x55:                                           // MODE SELECT(10)
        finish(false);
        break;
    case 0x25: {                                         // READ CAPACITY
        if (!medium) { finish(true, 0x02, 0x3a); break; }
        uint32_t last = d->disc->sectors() - 1;
        std::vector<uint8_t> s = {(uint8_t)(last >> 24), (uint8_t)(last >> 16), (uint8_t)(last >> 8), (uint8_t)last, 0, 0, 8, 0};
        start_data_in(std::move(s));
        break;
    }
    case 0x28:                                           // READ(10)
    case 0xa8: {                                         // READ(12)
        if (!medium) { finish(true, 0x02, 0x3a); break; }
        uint32_t lba = be32(2);
        uint32_t n = p[0] == 0x28 ? be16(7) : be32(6);
        std::vector<uint8_t> buf((size_t)n * 2048);
        for (uint32_t k = 0; k < n; k++)
            if (!d->disc->read_sector(lba + k, buf.data() + (size_t)k * 2048)) {
                finish(true, 0x05, 0x21);                // LBA out of range
                return;
            }
        start_data_in(std::move(buf));
        break;
    }
    case 0x43: {                                         // READ TOC (format 0, one data track)
        if (!medium) { finish(true, 0x02, 0x3a); break; }
        uint32_t lead_out = d->disc->sectors();
        std::vector<uint8_t> s = {0, 18, 1, 1,
                                  0, 0x14, 1, 0, 0, 0, 0, 0,
                                  0, 0x14, 0xaa, 0, (uint8_t)(lead_out >> 24), (uint8_t)(lead_out >> 16), (uint8_t)(lead_out >> 8), (uint8_t)lead_out};
        s.resize(std::min<size_t>(s.size(), be16(7) ? be16(7) : s.size()));
        start_data_in(std::move(s));
        break;
    }
    case 0x5a: {                                         // MODE SENSE(10): header only
        std::vector<uint8_t> s(8, 0);
        s[1] = 6;
        s.resize(std::min<size_t>(s.size(), be16(7) ? be16(7) : s.size()));
        start_data_in(std::move(s));
        break;
    }
    default:
        fprintf(stderr, "ata: %s: unsupported ATAPI packet %02x\n", d->name_.c_str(), p[0]);
        finish(true, 0x05, 0x20);                        // ILLEGAL REQUEST / invalid command
        break;
    }
}

}  // namespace hk
