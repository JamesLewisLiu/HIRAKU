#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply HIRAKU's edits to pristine MAME sources (third_party/mame/README.md).

    third_party/mame/hiraku-edits.py SRC_DEVICES_DIR

SRC_DEVICES_DIR is a MAME src/devices tree, or third_party/mame/src/devices
holding fresh copies of the files listed in the README (and minimp3.h in the
src/3rdparty next to it). The edits are applied
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
// program word changes and step_decoded() is step() working from the fields:
// the multiplier's operands through pointers chosen at decode time, and the
// pipeline's 3- and 2-step delay slots as template constants (run_decoded()
// cycles through the 6 phases) instead of counters wrapped on every step.
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
		d.m1p = d.m1_from_t ? &m_t[d.t] : &m_const[pc];
		d.m2p = d.m2_from_m ? &m_m[d.sm] : &m_r[d.sr];
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

template<int D3, int D2> inline __attribute__((always_inline)) void swp30_device::meg_state::step_decoded(s64 &p)
{
	if(m_mw_reg[D3])
		m_m[m_mw_reg[D3]] = m_mw_value[D3];

	if(m_rw_reg[D3])
		m_r[m_rw_reg[D3]] = m_rw_value[D3];

	if(m_index_active[D3])
		m_ram_index = m_index_value[D3];

	if(m_memw_active[D2]) {
		m_ram_write = m_memw_value[D2];
		m_memw_active[D2] = false;
	}
	if(m_memr_active[D2]) {
		m_ram_read = m_memr_value[D2];
		m_memr_active[D2] = false;
	}

	const decoded &d = m_dec[m_pc];
	const int sm = d.sm, sr = d.sr, dm = d.dm, dr = d.dr, t = d.t;

	if(d.alu) {
		s64 m1 = *d.m1p;
		if(d.m1_expand)
			m1 = m1_expand(m1);

		s64 m2 = *d.m2p;

		s64 m;
		switch(d.mmode) {
		case 0: m = 0; break;
		case 1: m = m1 << (8+15); break;
		case 2: m = m1 * m2; break;
		default: m = m2 << 15; break;
		}

		s64 a;
		switch(d.amode) {
		case 0: a = p; break;
		case 1: a = sr ? (s64(m_r[sr]) << 15) : (p >> 15); break;
		case 2: a = sm ? (s64(m_m[sm]) << 15) : (p >> 15); break;
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

		p = r;
	}

	m_mw_reg[D3] = dm;
	if(dm) {
		u32 v;
		switch(d.dsrc) {
		case 0: case 1: case 2: case 3:
			v = get_lfo(m_pc >> 4);
			break;
		case 4: v = m_ram_read; break;
		case 5: v = m_swp->machine().rand() & 0xffffff; if(v & 0x00800000) v |= 0xff000000; break;
		case 6: {
			s64 pr = p;
			if(d.add_rand)
				pr += m_swp->machine().rand() & 0x07e0;
			v = pack24(pr);
			break;
		}
		default: v = m_m[sm]; break;
		}
		m_mw_value[D3] = v;
	}

	m_rw_reg[D3] = dr;
	if(dr) {
		u32 v;
		if(d.r_from_r)
			v = m_r[sr];
		else {
			s64 pr = p;
			if(d.add_rand)
				pr += m_swp->machine().rand() & 0x07e0;
			v = pack24(pr);
		}
		m_rw_value[D3] = v;
	}

	if(d.memw) {
		m_memw_active[D2] = true;
		m_memw_value[D2] = p / 32768;
	} else
		m_memw_active[D2] = false;

	if(d.idx) {
		m_index_active[D3] = true;
		m_index_value[D3] = p >> (15+8);
	} else
		m_index_active[D3] = false;

	if(d.twr) {
		if(d.t_from_value)
			m_t[t] = m_t_value[D2];
		else
			m_t[t] = m_const[m_pc];
	}
	m_t_value[D2] = s16(d.idx ? (p >> 8) & 0x7fff : std::clamp<s64>(p >> (15+8), -0x8000, 0x7fff));

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
			m_memr_value[D2] = 0;
			m_memr_active[D2] = true;
		} else {
			u32 address = d.absolute ?
				(m_offset[m_pc/3] + (d.use_index ? m_ram_index : 0) + plus) & 0x3ffff :
				resolve_address(m_pc, m_offset[m_pc/3] + (d.use_index ? m_ram_index : 0) - m_sample_counter + plus);
			if(address != 0xffffffff) {
				u16 val = m_swp->m_reverb_cache.read_word(address);
				m_memr_value[D2] = revram_decode(val);
				m_memr_active[D2] = true;
			}
		}
		break;
	}
	}

	m_pc ++;
}

// n steps from m_pc, not past the end of the program (the caller wraps m_pc)
void swp30_device::meg_state::run_decoded(int n)
{
	// phase k: D3 = k % 3, D2 = k % 2
	static constexpr int phase_of[3][2] = {{0, 3}, {4, 1}, {2, 5}};
	int k = phase_of[m_delay_3][m_delay_2];
	m_icount -= n;
	s64 p = m_p;   // in a register for the whole run
	while(n) {
		switch(k) {
		case 0: step_decoded<0, 0>(p); if(!--n) { k = 1; break; } [[fallthrough]];
		case 1: step_decoded<1, 1>(p); if(!--n) { k = 2; break; } [[fallthrough]];
		case 2: step_decoded<2, 0>(p); if(!--n) { k = 3; break; } [[fallthrough]];
		case 3: step_decoded<0, 1>(p); if(!--n) { k = 4; break; } [[fallthrough]];
		case 4: step_decoded<1, 0>(p); if(!--n) { k = 5; break; } [[fallthrough]];
		case 5: step_decoded<2, 1>(p); --n; k = 0; break;
		}
	}
	m_p = p;
	m_delay_3 = k % 3;
	m_delay_2 = k % 2;
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
		else {
			m_meg->run_decoded(std::min<int>(m_meg->m_icount, 0x180 - m_meg->m_pc));
			if(m_meg->m_pc == 0x180)
				m_meg->m_pc = 0;
		}
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
			const s16 *m1p;   // the multiplier's operands: m_t[t] or m_const[pc], and m_m[sm] or m_r[sr]
			const s32 *m2p;
		};
		std::array<decoded, 0x180> m_dec;
		bool m_dec_dirty = true;
		void decode();
		template<int D3, int D2> void step_decoded(s64 &p);
		void run_decoded(int n);
''', 'drc decls')
    s = edit(s, '''	drc_cache m_drccache;
	std::unique_ptr<drcuml_state> m_drcuml;
	uml::code_handle *m_meg_drc_entry;
	uml::code_handle *m_meg_drc_nocode;
	uml::code_handle *m_meg_drc_out_of_cycles;
''', '''	// HIRAKU: DRC state removed
''', 'drc members')
    return s


def psx_cpp(s):
    # the loop body becomes execute_one(), so a recompiled block can hand one instruction back to
    # the interpreter; execute_run() runs blocks where it has them (runtime/sys573/mips.cpp)
    s = edit(s, '''void psxcpu_device::execute_run()
{
	do
	{
		if( LOG_BIOSCALL ) log_bioscall();
''', '''// HIRAKU: one instruction, the body of MAME's execute_run() loop (without the m_icount--)
void psxcpu_device::execute_one()
{
	{
		if( LOG_BIOSCALL ) log_bioscall();
''', 'execute_one head')
    s = edit(s, '''		m_icount--;
	} while( m_icount > 0 );
}
''', '''	}
}

void psxcpu_device::execute_run()
{
	do
	{
		// HIRAKU: recompiled blocks (runtime/sys573/mips.cpp). Outside a branch delay slot, a
		// block compiled from the code at m_pc runs in place of the interpreter when there is
		// one; it takes its own cycles off m_icount. m_hk_rec records where blocks start.
		if( m_hk_rec ) m_hk_rec( *this );
		if( m_hk_tab && m_delayr < PSXCPU_DELAYR_PC )
		{
			const void *b = m_hk_tab[ ( m_pc >> 2 ) & 0x3fffff ];
			if( b && m_hk_run( *this, b ) )
				continue;
		}

		execute_one();
		m_icount--;
	} while( m_icount > 0 );
}
''', 'execute_run loop')
    # SIO1 as a UART (sio_uart.h) for 573 boards with a multi-session unit; MAME's otherwise
    s = edit(s, '''		map(0x1f801050 | i, 0x1f80105f | i).rw("sio1", FUNC(psxsio_device::read), FUNC(psxsio_device::write));
''', '''		if( hk_psx_sio1_uart ) // HIRAKU
			map(0x1f801050 | i, 0x1f80105f | i).rw("sio1", FUNC(psxsio1_uart_device::read), FUNC(psxsio1_uart_device::write));
		else
			map(0x1f801050 | i, 0x1f80105f | i).rw("sio1", FUNC(psxsio_device::read), FUNC(psxsio_device::write));
''', 'sio1 map')
    s = edit(s, '''	auto &sio1(PSX_SIO1(config, "sio1", DERIVED_CLOCK(1, 2)));
	sio1.irq_handler().set("irq", FUNC(psxirq_device::intin8));
}
''', '''	if( hk_psx_sio1_uart ) // HIRAKU
	{
		auto &sio1(PSX_SIO1_UART(config, "sio1", DERIVED_CLOCK(1, 2)));
		sio1.irq_handler().set("irq", FUNC(psxirq_device::intin8));
	}
	else
	{
		auto &sio1(PSX_SIO1(config, "sio1", DERIVED_CLOCK(1, 2)));
		sio1.irq_handler().set("irq", FUNC(psxirq_device::intin8));
	}
}

// HIRAKU: set before the machine is configured (runtime/sys573/board.cpp)
bool hk_psx_sio1_uart = false;
''', 'sio1 config')
    return s


def psx_h(s):
    s = edit(s, '''	virtual void execute_run() override;
''', '''	virtual void execute_run() override;
	void execute_one(); // HIRAKU
''', 'execute_one decl')
    s = edit(s, '''#include "sio.h"
''', '''#include "sio.h"
#include "sio_uart.h" // HIRAKU

// HIRAKU: SIO1 as a UART (sio_uart.h) when set, before the machine is configured: 573 boards with
// a multi-session unit
extern bool hk_psx_sio1_uart;
''', 'sio_uart include')
    s = edit(s, '''	gte m_gte;
''', '''	gte m_gte;

	// HIRAKU: recompiled blocks (runtime/sys573/mips.cpp): m_hk_tab is indexed by
	// (pc >> 2) & 0x3fffff, m_hk_run runs the block it holds (false: interpret instead)
	friend struct hk_psx;
public:
	const void *const *m_hk_tab = nullptr;
	bool ( *m_hk_run )( psxcpu_device &, const void * ) = nullptr;
	void ( *m_hk_rec )( psxcpu_device & ) = nullptr;
protected:
''', 'hk members')
    return s


def mdec_cpp(s):
    # the IDCT over the nonzero coefficients only (most of a block's 64 are zero), from a transposed
    # table; the sums are the same integers in another order (unsigned, so they wrap as MAME's do)
    s = edit(s, '''void psxmdec_device::mdec_idct( int32_t *p_n_src, int32_t *p_n_dst )
{
''', '''void psxmdec_device::mdec_idct( int32_t *p_n_src, int32_t *p_n_dst )
{
	// HIRAKU: the same sums, over the nonzero coefficients
	{
		uint32_t n_acc[ DCTSIZE2 ] = {};
		for( uint32_t n_vu = 0; n_vu < DCTSIZE2; n_vu++ )
		{
			const uint32_t n_s = p_n_src[ n_vu ];
			if( n_s == 0 )
				continue;
			const int32_t *p_n_c = &m_hk_cos_t[ n_vu * DCTSIZE2 ];
			for( uint32_t n_yx = 0; n_yx < DCTSIZE2; n_yx++ )
				n_acc[ n_yx ] += n_s * (uint32_t)p_n_c[ n_yx ];
		}
		for( uint32_t n_yx = 0; n_yx < DCTSIZE2; n_yx++ )
			p_n_dst[ n_yx ] = (int32_t)n_acc[ n_yx ] >> ( MDEC_COS_PRECALC_BITS + 2 );
		return;
	}

''', 'mdec_idct')
    s = edit(s, '''						p_n_cos[ ( n_v * 8 ) + n_y ] ) >> ( 30 - MDEC_COS_PRECALC_BITS ) );
				}
			}
		}
	}
}
''', '''						p_n_cos[ ( n_v * 8 ) + n_y ] ) >> ( 30 - MDEC_COS_PRECALC_BITS ) );
				}
			}
		}
	}

	// HIRAKU: transposed, for mdec_idct()
	for( uint32_t n_yx = 0; n_yx < DCTSIZE2; n_yx++ )
		for( uint32_t n_vu = 0; n_vu < DCTSIZE2; n_vu++ )
			m_hk_cos_t[ n_vu * DCTSIZE2 + n_yx ] = p_n_cos_precalc[ n_yx * DCTSIZE2 + n_vu ];
}
''', 'mdec_cos_precalc')
    return s


def mdec_h(s):
    s = edit(s, '''	int32_t p_n_cos_precalc[ DCTSIZE2 * DCTSIZE2 ];
''', '''	int32_t p_n_cos_precalc[ DCTSIZE2 * DCTSIZE2 ];
	int32_t m_hk_cos_t[ DCTSIZE2 * DCTSIZE2 ]; // HIRAKU: p_n_cos_precalc transposed
''', 'cos_t')
    return s


def video_psx_cpp(s):
    # textured pixels: the level tables are (component << 8), computed instead of read from three
    # 128 KiB tables; and shading at 0x80 (raw textures, most sprites) gives the texel back unchanged
    # (each shade table maps level l at 0x80 to l), so the shade tables are skipped then.
    s = edit(s, """#define SHADEDPIXEL( PIXELUPDATE ) \\
		if( n_bgr != 0 ) \\
		{ \\
			WRITE_PIXEL( \\
				p_n_redshade[ p_n_redlevel[ n_bgr ] | n_r.w.h ] | \\
				p_n_greenshade[ p_n_greenlevel[ n_bgr ] | n_g.w.h ] | \\
				p_n_blueshade[ p_n_bluelevel[ n_bgr ] | n_b.w.h ] | \\
				( n_bgr & 0x8000 ) ) \\
		} \\
""", """/* HIRAKU: p_n_redlevel[] and friends, computed */
#define HK_RLEVEL( c ) ( ( ( c ) & 0x1f ) << 8 )
#define HK_GLEVEL( c ) ( ( ( ( c ) >> 5 ) & 0x1f ) << 8 )
#define HK_BLEVEL( c ) ( ( ( ( c ) >> 10 ) & 0x1f ) << 8 )
#define HK_NEUTRAL ( n_r.w.h == 0x80 && n_g.w.h == 0x80 && n_b.w.h == 0x80 )

#define SHADEDPIXEL( PIXELUPDATE ) \\
		if( n_bgr != 0 ) \\
		{ \\
			if( HK_NEUTRAL ) \\
				WRITE_PIXEL( n_bgr ) \\
			else \\
				WRITE_PIXEL( \\
					p_n_redshade[ HK_RLEVEL( n_bgr ) | n_r.w.h ] | \\
					p_n_greenshade[ HK_GLEVEL( n_bgr ) | n_g.w.h ] | \\
					p_n_blueshade[ HK_BLEVEL( n_bgr ) | n_b.w.h ] | \\
					( n_bgr & 0x8000 ) ) \\
		} \\
""", 'SHADEDPIXEL')
    s = edit(s, """				WRITE_PIXEL( \\
					p_n_redtrans[ p_n_f[ p_n_redlevel[ n_bgr ] | n_r.w.h ] | p_n_redb[ *( p_vram ) ] ] | \\
					p_n_greentrans[ p_n_f[ p_n_greenlevel[ n_bgr ] | n_g.w.h ] | p_n_greenb[ *( p_vram ) ] ] | \\
					p_n_bluetrans[ p_n_f[ p_n_bluelevel[ n_bgr ] | n_b.w.h ] | p_n_blueb[ *( p_vram ) ] ] | \\
					0x8000 ) \\
			} \\
			else \\
			{ \\
				WRITE_PIXEL( \\
					p_n_redshade[ p_n_redlevel[ n_bgr ] | n_r.w.h ] | \\
					p_n_greenshade[ p_n_greenlevel[ n_bgr ] | n_g.w.h ] | \\
					p_n_blueshade[ p_n_bluelevel[ n_bgr ] | n_b.w.h ] ) \\
			} \\
""", """				WRITE_PIXEL( \\
					p_n_redtrans[ p_n_f[ HK_RLEVEL( n_bgr ) | n_r.w.h ] | p_n_redb[ *( p_vram ) ] ] | \\
					p_n_greentrans[ p_n_f[ HK_GLEVEL( n_bgr ) | n_g.w.h ] | p_n_greenb[ *( p_vram ) ] ] | \\
					p_n_bluetrans[ p_n_f[ HK_BLEVEL( n_bgr ) | n_b.w.h ] | p_n_blueb[ *( p_vram ) ] ] | \\
					0x8000 ) \\
			} \\
			else if( HK_NEUTRAL ) \\
			{ \\
				WRITE_PIXEL( n_bgr ) \\
			} \\
			else \\
			{ \\
				WRITE_PIXEL( \\
					p_n_redshade[ HK_RLEVEL( n_bgr ) | n_r.w.h ] | \\
					p_n_greenshade[ HK_GLEVEL( n_bgr ) | n_g.w.h ] | \\
					p_n_blueshade[ HK_BLEVEL( n_bgr ) | n_b.w.h ] ) \\
			} \\
""", 'TRANSPARENTPIXEL')
    # FlatRectangle (the 573's install screens' solid fills): opaque and no mask test, every pixel
    # of a row gets the same value, so the row is filled at once (std::fill_n), the same pixels
    s = edit(s, """			if( ( (int32_t)n_drawarea_x1 - drawx ) > 0 )
			{
				n_distance -= ( n_drawarea_x1 - drawx );
				drawx = n_drawarea_x1;
			}

			SOLIDFILL( FLATRECTANGEUPDATE )
		}

		n_y++;
		n_h--;
	}
}

void psxgpu_device::FlatRectangle8x8()""", """			if( ( (int32_t)n_drawarea_x1 - drawx ) > 0 )
			{
				n_distance -= ( n_drawarea_x1 - drawx );
				drawx = n_drawarea_x1;
			}

			if( ( n_cmd & 0x02 ) == 0 && !m_check_stp )
			{
				/* HIRAKU: opaque and no mask test: every pixel the same colour, filled at once */
				if( n_distance > ( (int32_t)n_drawarea_x2 - drawx ) + 1 )
					n_distance = ( n_drawarea_x2 - drawx ) + 1;
				if( n_distance > 0 )
					std::fill_n( p_p_vram[ drawy ] + drawx, n_distance, (uint16_t)( (
						p_n_redshade[ MID_LEVEL | n_r.w.h ] |
						p_n_greenshade[ MID_LEVEL | n_g.w.h ] |
						p_n_blueshade[ MID_LEVEL | n_b.w.h ] ) | ( m_draw_stp ? 0x8000 : 0 ) ) );
			}
			else
			{
				SOLIDFILL( FLATRECTANGEUPDATE )
			}
		}

		n_y++;
		n_h--;
	}
}

void psxgpu_device::FlatRectangle8x8()""", "video/psx.cpp FlatRectangle fill")
    return s

def atapihle_cpp(s):
    # a packet's byte count limit below one sector (the multi-session unit's firmware asks
    # for 8 bytes of REQUEST SENSE's 18): MAME rounds the chunk down to a whole sector, 0,
    # and offers no data at all. As windyfairy's System 573 fork does: send all of it.
    return edit(s, """			if (m_buffer_size % m_sector_bytes)
				m_buffer_size = m_buffer_size - (m_buffer_size % m_sector_bytes);
""", """			if (m_buffer_size % m_sector_bytes)
			{
				// HIRAKU: below one sector, all of it (windyfairy's System 573 fork)
				const uint32_t whole = m_buffer_size - (m_buffer_size % m_sector_bytes);
				m_buffer_size = whole ? whole : m_data_size;
			}
""", 'fill_buffer sector rounding')


def ins8250_cpp(s):
    # the TX FIFO by head and tail alone: 16 bytes queued (a THRE handler filling the FIFO
    # while the last byte still shifts out, as the multi-session unit's and Keyboardmania's
    # firmware do) look like none, and all 16 are lost. A count, as the RX FIFO has.
    s = edit(s, """	if(m_ttail != m_thead)
	{
		transmit_register_setup(m_tfifo[m_ttail]);
		++m_ttail &= 0x0f;
		m_regs.lsr &= ~INS8250_LSR_TSRE;
		if(m_ttail == m_thead)
""", """	if(m_tnum) // HIRAKU: by count, a full FIFO isn't an empty one
	{
		transmit_register_setup(m_tfifo[m_ttail]);
		++m_ttail &= 0x0f;
		m_tnum--;
		m_regs.lsr &= ~INS8250_LSR_TSRE;
		if(!m_tnum)
""", 'tra_complete count')
    s = edit(s, """	m_tfifo[m_thead] = data;
	++m_thead &= 0x0f;
""", """	if(m_tnum == 16) // HIRAKU: a write to a full FIFO is lost
		return;
	m_tfifo[m_thead] = data;
	++m_thead &= 0x0f;
	m_tnum++;
""", 'push_tx count')
    s = edit(s, """	save_item(NAME(m_ttail));
""", """	save_item(NAME(m_ttail));
	save_item(NAME(m_tnum));
""", 'save m_tnum')
    s = edit(s, """	m_thead = m_ttail = 0;
	m_timeout->adjust""", """	m_thead = m_ttail = m_tnum = 0;
	m_timeout->adjust""", 'reset m_tnum')
    s = edit(s, """		std::fill(std::begin(m_tfifo), std::end(m_tfifo), 0);
		m_thead = m_ttail = 0;
""", """		std::fill(std::begin(m_tfifo), std::end(m_tfifo), 0);
		m_thead = m_ttail = m_tnum = 0;
""", 'fcr clear m_tnum')
    return s


def ins8250_h(s):
    return edit(s, """	int m_thead, m_ttail;
""", """	int m_thead, m_ttail, m_tnum; // HIRAKU: m_tnum
""", 'm_tnum')


def minimp3_h(s):
    # On AArch64 the NEON path rounds the 16-bit output as the SSE path does (clamp, then round
    # to nearest even), not half away from zero: the same samples on every host.
    def neon(s, indent, store, name):
        i = ' ' * indent
        old = (f"{i}a = VADD(a, VSET(0.5f));\n"
               f"{i}b = VADD(b, VSET(0.5f));\n"
               f"{i}pcma = vqmovn_s32(vqaddq_s32(vcvtq_s32_f32(a), vreinterpretq_s32_u32(vcltq_f32(a, VSET(0)))));\n"
               f"{i}pcmb = vqmovn_s32(vqaddq_s32(vcvtq_s32_f32(b), vreinterpretq_s32_u32(vcltq_f32(b, VSET(0)))));\n"
               f"{i}{store}\n")
        new = ("#if defined(__aarch64__) || defined(_M_ARM64)\n"
               f"{i}/* HIRAKU: as the SSE path (clamp, round to nearest even), for the same samples on every host */\n"
               f"{i}pcma = vqmovn_s32(vcvtnq_s32_f32(vmaxq_f32(vminq_f32(a, VSET(32767.0f)), VSET(-32768.0f))));\n"
               f"{i}pcmb = vqmovn_s32(vcvtnq_s32_f32(vmaxq_f32(vminq_f32(b, VSET(32767.0f)), VSET(-32768.0f))));\n"
               "#else\n" + old[:-len(f"{i}{store}\n")] + "#endif\n" + f"{i}{store}\n")
        return edit(s, old, new, name)
    s = neon(s, 12, 'vst1_lane_s16(dstr + (15 - i)*nch, pcma, 1);', 'minimp3 synth rounding')
    return neon(s, 8, 'vst1_lane_s16(out+i  , pcma, 0);', 'minimp3 f32_to_s16 rounding')


def rs232_cpp(s):
    # Twinkle: its RS-232 ports hold the DVD player (xvd701) or nothing. MAME's default option
    # list names ~20 serial devices (terminals, printers, pty...) that HIRAKU doesn't carry.
    s = cut(s, '#include "adsp2181ekl.h"', 'void default_rs232_devices(device_slot_interface &device)',
            '// HIRAKU: no serial devices of MAME\'s default list (the explicit instantiations of\n'
            '// MAME\'s two-parameter device_finder go too: the shim\'s finder has one)\n\n', 'rs232.cpp includes')
    s = cut(s, '\tdevice.option_add("adsp2181ekl",', '}',
            '\t// HIRAKU: none (see above)\n', 'rs232.cpp default devices')
    return s


def xvd701_h(s):
    # Twinkle's DVD player: its state after each command, for HIRAKU's picture (PLAN.md section 21,
    # TW-3). MAME models the serial protocol and shows nothing.
    s = edit(s, '\tvirtual void input_txd(int state) override { device_serial_interface::rx_w(state); }\n',
             '\tvirtual void input_txd(int state) override { device_serial_interface::rx_w(state); }\n'
             '\n'
             '\t// HIRAKU: the player\'s state after each command it takes: the chapter, STOP/PLAYING/PAUSE\n'
             '\t// (0/1/2), the power, and whether playback starts again at the chapter\'s beginning\n'
             '\tstd::function<void(int chapter, int status, bool powered, bool from_start)> m_hk_state;\n',
             'xvd701.h hook')
    s = edit(s, '\tint m_chapter;\n',
             '\tint m_chapter;\n\tbool m_hk_seek = false; // HIRAKU: this command (re)started the chapter\n', 'xvd701.h seek flag')
    return s


def xvd701_cpp(s):
    s = edit(s, '\tm_chapter = chapter;\n', '\tm_chapter = chapter;\n\tm_hk_seek = true; // HIRAKU\n', 'xvd701.cpp seek')
    s = edit(s, '\t\tm_command[10] == sum(m_command, sizeof(m_command) - 1))\n\t{\n',
             '\t\tm_command[10] == sum(m_command, sizeof(m_command) - 1))\n\t{\n\t\tm_hk_seek = false; // HIRAKU\n',
             'xvd701.cpp command start')
    s = edit(s, '\t\t\tcreate_packet(STATUS_OK, response);\n\t\t}\n\t}\n}\n\nDEFINE_DEVICE_TYPE(JVC_XVD701',
             '\t\t\tcreate_packet(STATUS_OK, response);\n\t\t}\n\n'
             '\t\tif (m_hk_state) m_hk_state(m_chapter, m_playback_status, m_is_powered, m_hk_seek); // HIRAKU\n'
             '\t}\n}\n\nDEFINE_DEVICE_TYPE(JVC_XVD701', 'xvd701.cpp command end')
    return s


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = Path(sys.argv[1])
    for rel, fn in [('sound/swp30.cpp', swp30_cpp), ('sound/swp30.h', swp30_h),
                    ('cpu/psx/psx.cpp', psx_cpp), ('cpu/psx/psx.h', psx_h),
                    ('cpu/psx/mdec.cpp', mdec_cpp), ('cpu/psx/mdec.h', mdec_h),
                    ('video/psx.cpp', video_psx_cpp), ('bus/ata/atapihle.cpp', atapihle_cpp),
                    ('machine/ins8250.cpp', ins8250_cpp), ('machine/ins8250.h', ins8250_h),
                    ('../3rdparty/minimp3/minimp3.h', minimp3_h),
                    ('bus/rs232/rs232.cpp', rs232_cpp),
                    ('bus/rs232/xvd701.h', xvd701_h), ('bus/rs232/xvd701.cpp', xvd701_cpp)]:
        p = root / rel
        text = p.read_text()
        if 'HIRAKU' in text:
            sys.exit(f'hiraku-edits: {p} already edited (start from a pristine copy)')
        p.write_text(fn(text))
        print(f'edited {p}')


if __name__ == '__main__':
    main()
