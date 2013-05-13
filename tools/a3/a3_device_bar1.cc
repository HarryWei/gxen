/*
 * Cross device table
 *
 * Copyright (c) 2012-2013 Yusuke Suzuki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdint.h>
#include "a3_inttypes.h"
#include "a3_bit_mask.h"
#include "a3_device_table.h"
#include "a3_pramin.h"
#include "a3_shadow_page_table.h"
#include "a3_device_bar1.h"
#include "a3_context.h"
namespace a3 {

static uint32_t lower32(uint64_t data) {
    return bit_mask<32>(data);
}

static uint32_t upper32(uint64_t data) {
    return bit_mask<32>(data >> 32);
}

#if 0
static uint64_t encode(uint64_t phys) {
    phys >>= 8;
    phys |= 0x00000001; /* present */
    return phys;
}
#endif

device_bar1::device_bar1()
    : ramin_(2)
    , directory_(8)
    , entry_() {
    const uint64_t vm_size = 0x1000 * 128;
    // construct channel ramin
    ramin_.write32(0x0200, lower32(directory_.address()));
    ramin_.write32(0x0204, lower32(directory_.address()));
    ramin_.write32(0x0208, lower32(vm_size));
    ramin_.write32(0x020c, upper32(vm_size));

    // construct minimum page table
    struct page_directory dir = { };
    dir.word0 = 0;
    dir.word1 = (entry_.address()) >> 8 | 0x1;
    directory_.write32(0x0, dir.word0);
    directory_.write32(0x4, dir.word1);

    // refresh_channel();
    refresh_poll_area();

    A3_LOG("construct shadow BAR1 channel %" PRIX64 " with PDE %" PRIX64 " PTE %" PRIX64 " \n", ramin_.address(), directory_.address(), entry_.address());
}

void device_bar1::refresh_channel(context* ctx) {
    // set ramin as BAR1 channel
    ramin_.write32(0x0200, lower32(directory_.address()));
    ramin_.write32(0x0204, upper32(directory_.address()));
    registers::write32(0x001704, 0x80000000 | ramin_.address() >> 12);
}

void device_bar1::refresh_poll_area() {
    // set 0 as POLL_AREA
    registers::accessor registers;
    registers.mask32(0x002200, 0x00000001, 0x00000001);
    registers.write32(0x2254, 0x10000000 | 0x0);
}

void device_bar1::shadow(context* ctx) {
    A3_LOG("%" PRIu32 " BAR1 shadowed\n", ctx->id());
    for (uint32_t vcid = 0; vcid < A3_DOMAIN_CHANNELS; ++vcid) {
        const uint64_t offset = vcid * 0x1000ULL + ctx->poll_area();
        const uint32_t pcid = ctx->get_phys_channel_id(vcid);
        const uint64_t virt = pcid * 0x1000ULL;
        struct shadow_page_entry entry;
        const uint64_t gphys = ctx->bar1_channel()->table()->resolve(offset, &entry);
        if (gphys != UINT64_MAX) {
            map(virt, entry.virt().raw);
        }
    }
}

void device_bar1::map(uint64_t virt, uint64_t data) {
    if ((virt / kPAGE_DIRECTORY_COVERED_SIZE) != 0) {
        return;
    }
    const uint64_t index = virt / kSMALL_PAGE_SIZE;
    assert((virt % kSMALL_PAGE_SIZE) == 0);
    entry_.write32(0x8 * index, lower32(data));
    entry_.write32(0x8 * index + 0x4, upper32(data));
    A3_LOG("  BAR1 table %" PRIX64 " mapped to %" PRIX64 "\n", virt, data);
}

void device_bar1::flush() {
    A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
        const uint32_t engine = 1 | 4;
        registers::accessor registers;
        registers.wait_ne(0x100c80, 0x00ff0000, 0x00000000);
        registers.write32(0x100cb8, directory_.address() >> 8);
        registers.write32(0x100cbc, engine);
        registers.wait_eq(0x100c80, 0x00008000, 0x00008000);
    }
}

void device_bar1::write(context* ctx, const command& cmd) {
    uint64_t offset = cmd.offset - ctx->poll_area();
    offset += 0x1000ULL * ctx->id() * A3_DOMAIN_CHANNELS;
    device::instance()->write(1, offset, cmd.value, cmd.size());
}

uint32_t device_bar1::read(context* ctx, const command& cmd) {
    uint64_t offset = cmd.offset - ctx->poll_area();
    offset += 0x1000ULL * ctx->id() * A3_DOMAIN_CHANNELS;
    return device::instance()->read(1, offset, cmd.size());
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */