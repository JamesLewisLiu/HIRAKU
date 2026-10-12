#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply HIRAKU's edits to pristine MAME sources (third_party/mame/README.md).

    third_party/mame/hiraku-edits.py SRC_DEVICES_DIR

SRC_DEVICES_DIR is a MAME src/devices tree, or third_party/mame/src/devices
holding fresh copies of the files listed in the README. The edits are applied
in place. Every anchor must be found exactly once, so the script fails loudly
when upstream changes the code an edit relies on. The H8 files need no edits.
"""
import sys
from pathlib import Path


def edit(text, old, new, name):
    n = text.count(old)
    if n != 1:
        sys.exit(f"hiraku-edits: {name}: anchor found {n} times, expected once:\n{old[:200]}")
    return text.replace(old, new)


def cut(text, start, end, replacement, name):
    """Replace [start, end) (end itself is kept) with replacement."""
    a = text.find(start)
    b = text.find(end, a + 1) if a >= 0 else -1
    if a < 0 or b < 0 or text.count(start) != 1:
        sys.exit(f"hiraku-edits: {name}: span not found")
    return text[:a] + replacement + text[b:]


STEP_DECODED = r'''// HIRAKU: the MEG program pre-decoded. step() extracts every field of every
// instruction on every one of 17 M steps a second; decode() does it when a
// program word changes and step_decoded() is step() working from the fields.
// HK_XT446_MEG_REF=1 runs step() instead, to check the two agree.
void swp30_device::meg_state::decode()
{
	for(int pc = 0; pc != 0x180; pc++) {
		u64 opcode = m_program[pc];
		decoded &d = m_dec[pc];
		d.sm = BIT(opcode, 0x04, 6);
		d.sr = BIT(opcode, 0x0b, 7);
		d.dm = BIT(opcode, 0x27, 6);
		d.dr = BIT(opcode, 0x30, 7);
		d.t  = BIT(opcode, 0x38, 3);
		d.mmode = BIT(opcode, 0x16, 2);
		d.alu = d.mmode != 0 || BIT(opcode, 0x1a, 6);
		u32 m1t = BIT(opcode, 0x14, 2);
		d.m1_from_t = m1t == 1 || m1t == 2;
		d.m1_expand = BIT(opcode, 0x13);
		d.m2_from_m = BIT(opcode, 0x12);
		d.amode = BIT(opcode, 0x18, 2);
		d.op = BIT(opcode, 0x1a, 2);
		u32 shift = BIT(opcode, 0x1c, 2);
		d.shift = shift == 3 ? 4 : shift;
		d.sat = BIT(opcode, 0x1e, 2);
		d.dsrc = BIT(opcode, 0x2d, 3);
		d.add_rand = !BIT(opcode, 0x0a);
		d.r_from_r = BIT(opcode, 0x37);
		d.memw = BIT(opcode, 0x3d);
		d.idx = BIT(opcode, 0x3e);
		d.twr = BIT(opcode, 0x3b, 1);
		d.t_from_value = BIT(opcode, 0x3c);
		d.memop = BIT(opcode, 0x24, 2);
		d.use_index = BIT(opcode, 0x21);
		d.absolute = BIT(opcode, 0x23);
	}
	m_dec_dirty = false;
}

void swp30_device::meg_state::step_decoded()
{
	if(m_mw_reg[m_delay_3])
		m_m[m_mw_reg[m_delay_3]] = m_mw_value[m_delay_3];

	if(m_rw_reg[m_delay_3])
		m_r[m_rw_reg[m_delay_3]] = m_rw_value[m_delay_3];

	if(m_index_active[m_delay_3])
		m_ram_index = m_index_value[m_delay_3];

	if(m_memw_active[m_delay_2]) {
		m_ram_write = m_memw_value[m_delay_2];
		m_memw_active[m_delay_2] = false;
	}
	if(m_memr_active[m_delay_2]) {
		m_ram_read = m_memr_value[m_delay_2];
		m_memr_active[m_delay_2] = false;
	}

	const decoded &d = m_dec[m_pc];
	const int sm = d.sm, sr = d.sr, dm = d.dm, dr = d.dr, t = d.t;

	if(d.alu) {
		s64 m1 = d.m1_from_t ? m_t[t] : m_const[m_pc];
		if(d.m1_expand)
			m1 = m1_expand(m1);

		s64 m2 = d.m2_from_m ? m_m[sm] : m_r[sr];

		s64 m;
		switch(d.mmode) {
		case 0: m = 0; break;
		case 1: m = m1 << (8+15); break;
		case 2: m = m1 * m2; break;
		default: m = m2 << 15; break;
		}

		s64 a;
		switch(d.amode) {
		case 0: a = m_p; break;
		case 1: a = sr ? (s64(m_r[sr]) << 15) : (m_p >> 15); break;
		case 2: a = sm ? (s64(m_m[sm]) << 15) : (m_p >> 15); break;
		default: a = 0; break;
		}

		s64 r;
		switch(d.op) {
		case 0: r = m + a; break;
		case 1: r = m - a; break;
		case 2: r = m + ((a < 0) ? -a : a); break;
		default: r = m & a; break;
		}

		r <<= d.shift;

		switch(d.sat) {
		case 0: r = util::sext(r, 42); break;
		case 1: r = std::clamp<s64>(r, -0x4000000000, 0x3fffffffff); break;
		case 2: r = std::clamp<s64>(r, 0, 0x3fffffffff); break;
		default: r = std::min<s64>((r < 0) ? -r : r, 0x3fffffffff); break;
		}

		m_p = r;
	}

	m_mw_reg[m_delay_3] = dm;
	if(dm) {
		u32 v;
		switch(d.dsrc) {
		case 0: case 1: case 2: case 3:
			v = get_lfo(m_pc >> 4);
			break;
		case 4: v = m_ram_read; break;
		case 5: v = m_swp->machine().rand() & 0xffffff; if(v & 0x00800000) v |= 0xff000000; break;
		case 6: {
			s64 p = m_p;
			if(d.add_rand)
				p += m_swp->machine().rand() & 0x07e0;
			v = pack24(p);
			break;
		}
		default: v = m_m[sm]; break;
		}
		m_mw_value[m_delay_3] = v;
	}

	m_rw_reg[m_delay_3] = dr;
	if(dr) {
		u32 v;
		if(d.r_from_r)
			v = m_r[sr];
		else {
			s64 p = m_p;
			if(d.add_rand)
				p += m_swp->machine().rand() & 0x07e0;
			v = pack24(p);
		}
		m_rw_value[m_delay_3] = v;
	}

	if(d.memw) {
		m_memw_active[m_delay_2] = true;
		m_memw_value[m_delay_2] = m_p / 32768;
	} else
		m_memw_active[m_delay_2] = false;

	if(d.idx) {
		m_index_active[m_delay_3] = true;
		m_index_value[m_delay_3] = m_p >> (15+8);
	} else
		m_index_active[m_delay_3] = false;

	if(d.twr) {
		if(d.t_from_value)
			m_t[t] = m_t_value[m_delay_2];
		else
			m_t[t] = m_const[m_pc];
	}
	m_t_value[m_delay_2] = s16(d.idx ? (m_p >> 8) & 0x7fff : std::clamp<s64>(m_p >> (15+8), -0x8000, 0x7fff));

	switch(d.memop) {
	case 0:
		break;
	case 1:
		if(!BIT(m_swp->m_revram_enable, map_bank(m_pc))) {
			u32 address = resolve_address(m_pc, m_offset[m_pc/3] + (d.use_index ? m_ram_index : 0) - m_sample_counter);
			if(address != 0xffffffff)
				m_swp->m_reverb_cache.write_word(address, revram_encode(m_ram_write));
		}
		break;
	default: {
		const s32 plus = d.memop == 3 ? 1 : 0;
		if(!d.absolute && BIT(m_swp->m_revram_enable, map_bank(m_pc))) {
			m_memr_value[m_delay_2] = 0;
			m_memr_active[m_delay_2] = true;
		} else {
			u32 address = d.absolute ?
				(m_offset[m_pc/3] + (d.use_index ? m_ram_index : 0) + plus) & 0x3ffff :
				resolve_address(m_pc, m_offset[m_pc/3] + (d.use_index ? m_ram_index : 0) - m_sample_counter + plus);
			if(address != 0xffffffff) {
				u16 val = m_swp->m_reverb_cache.read_word(address);
				m_memr_value[m_delay_2] = revram_decode(val);
				m_memr_active[m_delay_2] = true;
			}
		}
		break;
	}
	}

	m_delay_3 ++;
	if(m_delay_3 == 3)
		m_delay_3 = 0;

	m_delay_2 ++;
	if(m_delay_2 == 2)
		m_delay_2 = 0;

	m_pc ++;
	m_icount --;

	if(m_pc == 0x180)
		m_pc = 0;
}

'''


def swp30_cpp(s):
    s = edit(s, '#include "cpu/drcumlsh.h"\n\n#include "debugger.h"\n#include "emuopts.h"\n',
             '// HIRAKU: no DRC back-end (the MEG runs on the interpreter), no debugger/options headers\n', 'includes')
    s = edit(s, ''',
	  m_drccache(32*1024*1024 + sizeof(meg_state))
{''', '''
{''', 'drccache member init')
    s = edit(s, '''	m_drccache.allocate_cache(mconfig().options().drc_rwx());
	m_meg = m_drccache.alloc_near<meg_state>();''', '''	m_meg = new meg_state();   // HIRAKU: no DRC cache to allocate it in''', 'meg allocation')
    s = cut(s, '	// SWP30 compiles the entire MEG program as one block', '	m_meg_program_changed = true;\n\tm_meg_drc_active',
            '', 'drcuml setup')
    s = edit(s, '	m_meg_drc_active = allow_drc();', '	m_meg_drc_active = false;   // HIRAKU', 'drc active')
    s = cut(s, '// pack24 inline, result in I0', 'void swp30_device::meg_state::step()',
            '// HIRAKU: the UML code generator (drc_pack24, drc_t_value, drc) removed\n\n', 'drc code generator')
    # the program fetch: what meg_prg_map_r returns, without going through the bus
    s = edit(s, '	u64 opcode = m_swp->m_program_cache.read_qword(m_pc);',
             '	u64 opcode = m_program[m_pc];   // HIRAKU: what m_program_cache.read_qword(m_pc) returns (meg_prg_map_r), without the bus',
             'program fetch')
    s = edit(s, '''	m_program[m_program_address] = (m_program[m_program_address] & mask) | (u64(data) << shift);
''', '''	m_program[m_program_address] = (m_program[m_program_address] & mask) | (u64(data) << shift);
	m_dec_dirty = true;   // HIRAKU
''', 'prg_w')
    s = edit(s, '''	m_sample_counter = 0;
	m_retval = 0;
}''', '''	m_sample_counter = 0;
	m_retval = 0;
	m_dec_dirty = true;   // HIRAKU
}''', 'meg reset')
    a = s.find('void swp30_device::execute_run()\n{')
    b = s.find('void swp30_device::adc_step()')
    if a < 0 or b < 0:
        sys.exit('hiraku-edits: execute_run not found')
    s = s[:a] + STEP_DECODED + '''void swp30_device::execute_run()
{
	// HIRAKU: the interpreter path only, from the pre-decoded program
	static const bool reference = getenv("HK_XT446_MEG_REF") != nullptr;
	m_meg_program_changed = false;
	if(m_meg->m_dec_dirty)
		m_meg->decode();

	while(m_meg->m_icount > 0) {
		if(m_meg->m_pc == 0)
			sample_step();
		if(reference)
			m_meg->step();
		else
			m_meg->step_decoded();
	}
}

''' + s[b:]
    s = edit(s, '''	m_meg->lfo_step();
	m_meg->m_sample_counter ++;
}''', '''	m_meg->lfo_step();
	m_meg->m_sample_counter ++;
	if(hk_on_sample)
		hk_on_sample(m_adc);   // HIRAKU: the DAC outputs of this sample
}''', 'sample hook')
    return s


def swp30_h(s):
    s = edit(s, '#include "cpu/drcuml.h"\n', '// HIRAKU: no DRC (cpu/drcuml.h)\n#include <functional>\n', 'include')
    s = edit(s, '''	void map(address_map &map) ATTR_COLD;

protected:''', '''	void map(address_map &map) ATTR_COLD;

	// HIRAKU: called after each sample with the four DAC outputs (MAME's stream outputs 0-3)
	std::function<void(const std::array<s32, 4> &)> hk_on_sample;

protected:''', 'hook member')
    s = edit(s, '''		void drc(drcuml_block &block, u16 pc);
		void drc_pack24(drcuml_block &block, bool dither, uml::code_label label);
		void drc_t_value(drcuml_block &block, u32 index2);
''', '''		// HIRAKU: the program pre-decoded (see step_decoded() in swp30.cpp)
		struct decoded {
			u8 sm, sr, dm, dr, t;
			u8 mmode, alu, m1_from_t, m1_expand, m2_from_m, amode, op, shift, sat;
			u8 dsrc, add_rand, r_from_r, memw, idx, twr, t_from_value, memop, use_index, absolute;
		};
		std::array<decoded, 0x180> m_dec;
		bool m_dec_dirty = true;
		void decode();
		void step_decoded();
''', 'drc decls')
    s = edit(s, '''	drc_cache m_drccache;
	std::unique_ptr<drcuml_state> m_drcuml;
	uml::code_handle *m_meg_drc_entry;
	uml::code_handle *m_meg_drc_nocode;
	uml::code_handle *m_meg_drc_out_of_cycles;
''', '''	// HIRAKU: DRC state removed
''', 'drc members')
    return s


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = Path(sys.argv[1])
    for rel, fn in [('sound/swp30.cpp', swp30_cpp), ('sound/swp30.h', swp30_h)]:
        p = root / rel
        text = p.read_text()
        if 'HIRAKU' in text:
            sys.exit(f'hiraku-edits: {p} already edited (start from a pristine copy)')
        p.write_text(fn(text))
        print(f'edited {p}')


if __name__ == '__main__':
    main()
