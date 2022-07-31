// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/*
    VM Labs Aries 3 "NUON Multi-Media Architecture" simulator

    - Changelist -
      10 Mar. 2018
      - Initial skeleton version.
*/

#include "emu.h"
#include "nuon.h"
#include "nuondasm.h"

//**************************************************************************
//  DEVICE INTERFACE
//**************************************************************************

DEFINE_DEVICE_TYPE(NUON,   nuon_mpe_device,   "nuon",   "Aries 3 \"Nuon\" MPE")

//-------------------------------------------------
//  nuon_mpe_device - constructor
//-------------------------------------------------

nuon_mpe_device::nuon_mpe_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: cpu_device(mconfig, NUON, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_BIG, 32, 32, 0, address_map_constructor(FUNC(nuon_mpe_device::internal_map), this))
	, m_pc(0x20300000)
	, m_branch_pc(0)
	, m_branch_delay(0)
	, m_icount(0)
{
}

//-------------------------------------------------
//  device_start
//-------------------------------------------------

void nuon_mpe_device::device_start()
{
	m_program = &space(AS_PROGRAM);

	// register our state for the debugger
	state_add(STATE_GENPC,     "GENPC",       m_pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC",       m_pc).noshow();
	state_add(STATE_GENFLAGS,  "GENFLAGS",    m_cc).callimport().callexport().formatstr("%37s").noshow();
	state_add(NUON_PC,         "pc",          m_pc).formatstr("%08X");
	state_add(NUON_CC,         "cc",          m_cc).callimport().callexport().formatstr("%37s");
	state_add(NUON_R00,        "r0",          m_regs.r[ 0]).formatstr("%08X");
	state_add(NUON_R01,        "r1",          m_regs.r[ 1]).formatstr("%08X");
	state_add(NUON_R02,        "r2",          m_regs.r[ 2]).formatstr("%08X");
	state_add(NUON_R03,        "r3",          m_regs.r[ 3]).formatstr("%08X");
	state_add(NUON_R04,        "r4",          m_regs.r[ 4]).formatstr("%08X");
	state_add(NUON_R05,        "r5",          m_regs.r[ 5]).formatstr("%08X");
	state_add(NUON_R06,        "r6",          m_regs.r[ 6]).formatstr("%08X");
	state_add(NUON_R07,        "r7",          m_regs.r[ 7]).formatstr("%08X");
	state_add(NUON_R08,        "r8",          m_regs.r[ 8]).formatstr("%08X");
	state_add(NUON_R09,        "r9",          m_regs.r[ 9]).formatstr("%08X");
	state_add(NUON_R10,        "r10",         m_regs.r[10]).formatstr("%08X");
	state_add(NUON_R11,        "r11",         m_regs.r[11]).formatstr("%08X");
	state_add(NUON_R12,        "r12",         m_regs.r[12]).formatstr("%08X");
	state_add(NUON_R13,        "r13",         m_regs.r[13]).formatstr("%08X");
	state_add(NUON_R14,        "r14",         m_regs.r[14]).formatstr("%08X");
	state_add(NUON_R15,        "r15",         m_regs.r[15]).formatstr("%08X");
	state_add(NUON_R16,        "r16",         m_regs.r[16]).formatstr("%08X");
	state_add(NUON_R17,        "r17",         m_regs.r[17]).formatstr("%08X");
	state_add(NUON_R18,        "r18",         m_regs.r[18]).formatstr("%08X");
	state_add(NUON_R19,        "r19",         m_regs.r[19]).formatstr("%08X");
	state_add(NUON_R20,        "r20",         m_regs.r[20]).formatstr("%08X");
	state_add(NUON_R21,        "r21",         m_regs.r[21]).formatstr("%08X");
	state_add(NUON_R22,        "r22",         m_regs.r[22]).formatstr("%08X");
	state_add(NUON_R23,        "r23",         m_regs.r[23]).formatstr("%08X");
	state_add(NUON_R24,        "r24",         m_regs.r[24]).formatstr("%08X");
	state_add(NUON_R25,        "r25",         m_regs.r[25]).formatstr("%08X");
	state_add(NUON_R26,        "r26",         m_regs.r[26]).formatstr("%08X");
	state_add(NUON_R27,        "r27",         m_regs.r[27]).formatstr("%08X");
	state_add(NUON_R28,        "r28",         m_regs.r[28]).formatstr("%08X");
	state_add(NUON_R29,        "r29",         m_regs.r[29]).formatstr("%08X");
	state_add(NUON_R30,        "r30",         m_regs.r[30]).formatstr("%08X");
	state_add(NUON_R31,        "r31",         m_regs.r[31]).formatstr("%08X");
	state_add(NUON_RC0,        "rc0",         m_rc0).formatstr("%03X");
	state_add(NUON_RC1,        "rc1",         m_rc1).formatstr("%03X");
	state_add(NUON_ODMACTL,    "odmactl",     m_odmactl).formatstr("%02X");
	state_add(NUON_V0H,        "v0[127:64]",  m_regs.v[0].h).formatstr("%16X");
	state_add(NUON_V0L,        "v0[64:0]",    m_regs.v[0].l).formatstr("%16X");
	state_add(NUON_V1H,        "v1[127:64]",  m_regs.v[1].h).formatstr("%16X");
	state_add(NUON_V1L,        "v1[64:0]",    m_regs.v[1].l).formatstr("%16X");
	state_add(NUON_V2H,        "v2[127:64]",  m_regs.v[2].h).formatstr("%16X");
	state_add(NUON_V2L,        "v2[64:0]",    m_regs.v[2].l).formatstr("%16X");
	state_add(NUON_V3H,        "v3[127:64]",  m_regs.v[3].h).formatstr("%16X");
	state_add(NUON_V3L,        "v3[64:0]",    m_regs.v[3].l).formatstr("%16X");
	state_add(NUON_V4H,        "v4[127:64]",  m_regs.v[4].h).formatstr("%16X");
	state_add(NUON_V4L,        "v4[64:0]",    m_regs.v[4].l).formatstr("%16X");
	state_add(NUON_V5H,        "v5[127:64]",  m_regs.v[5].h).formatstr("%16X");
	state_add(NUON_V5L,        "v5[64:0]",    m_regs.v[5].l).formatstr("%16X");
	state_add(NUON_V6H,        "v6[127:64]",  m_regs.v[6].h).formatstr("%16X");
	state_add(NUON_V6L,        "v6[64:0]",    m_regs.v[6].l).formatstr("%16X");
	state_add(NUON_V7H,        "v7[127:64]",  m_regs.v[7].h).formatstr("%16X");
	state_add(NUON_V7L,        "v7[64:0]",    m_regs.v[7].l).formatstr("%16X");
	state_add(NUON_HV00,       "hv00",        m_regs.hv[ 0].v).formatstr("%16X");
	state_add(NUON_HV02,       "hv02",        m_regs.hv[ 1].v).formatstr("%16X");
	state_add(NUON_HV04,       "hv04",        m_regs.hv[ 2].v).formatstr("%16X");
	state_add(NUON_HV06,       "hv06",        m_regs.hv[ 3].v).formatstr("%16X");
	state_add(NUON_HV08,       "hv08",        m_regs.hv[ 4].v).formatstr("%16X");
	state_add(NUON_HV10,       "hv10",        m_regs.hv[ 5].v).formatstr("%16X");
	state_add(NUON_HV12,       "hv12",        m_regs.hv[ 6].v).formatstr("%16X");
	state_add(NUON_HV14,       "hv14",        m_regs.hv[ 7].v).formatstr("%16X");
	state_add(NUON_HV16,       "hv16",        m_regs.hv[ 8].v).formatstr("%16X");
	state_add(NUON_HV18,       "hv18",        m_regs.hv[ 9].v).formatstr("%16X");
	state_add(NUON_HV20,       "hv20",        m_regs.hv[10].v).formatstr("%16X");
	state_add(NUON_HV22,       "hv22",        m_regs.hv[11].v).formatstr("%16X");
	state_add(NUON_HV24,       "hv24",        m_regs.hv[12].v).formatstr("%16X");
	state_add(NUON_HV26,       "hv26",        m_regs.hv[13].v).formatstr("%16X");
	state_add(NUON_HV28,       "hv28",        m_regs.hv[14].v).formatstr("%16X");
	state_add(NUON_HV30,       "hv30",        m_regs.hv[15].v).formatstr("%16X");
	state_add(NUON_SV0H,       "sv0[63:48]",  m_regs.sv[0].w3).formatstr("%04X");
	state_add(NUON_SV0MM,      "sv0[47:32]",  m_regs.sv[0].w3).formatstr("%04X");
	state_add(NUON_SV0M,       "sv0[31:16]",  m_regs.sv[0].w3).formatstr("%04X");
	state_add(NUON_SV0L,       "sv0[15:0]",   m_regs.sv[0].w3).formatstr("%04X");
	state_add(NUON_SV1H,       "sv1[63:48]",  m_regs.sv[1].w3).formatstr("%04X");
	state_add(NUON_SV1MM,      "sv1[47:32]",  m_regs.sv[1].w3).formatstr("%04X");
	state_add(NUON_SV1M,       "sv1[31:16]",  m_regs.sv[1].w3).formatstr("%04X");
	state_add(NUON_SV1L,       "sv1[15:0]",   m_regs.sv[1].w3).formatstr("%04X");
	state_add(NUON_SV2H,       "sv2[63:48]",  m_regs.sv[2].w3).formatstr("%04X");
	state_add(NUON_SV2MM,      "sv2[47:32]",  m_regs.sv[2].w3).formatstr("%04X");
	state_add(NUON_SV2M,       "sv2[31:16]",  m_regs.sv[2].w3).formatstr("%04X");
	state_add(NUON_SV2L,       "sv2[15:0]",   m_regs.sv[2].w3).formatstr("%04X");
	state_add(NUON_SV3H,       "sv3[63:48]",  m_regs.sv[3].w3).formatstr("%04X");
	state_add(NUON_SV3MM,      "sv3[47:32]",  m_regs.sv[3].w3).formatstr("%04X");
	state_add(NUON_SV3M,       "sv3[31:16]",  m_regs.sv[3].w3).formatstr("%04X");
	state_add(NUON_SV3L,       "sv3[15:0]",   m_regs.sv[3].w3).formatstr("%04X");
	state_add(NUON_SV4H,       "sv4[63:48]",  m_regs.sv[4].w3).formatstr("%04X");
	state_add(NUON_SV4MM,      "sv4[47:32]",  m_regs.sv[4].w3).formatstr("%04X");
	state_add(NUON_SV4M,       "sv4[31:16]",  m_regs.sv[4].w3).formatstr("%04X");
	state_add(NUON_SV4L,       "sv4[15:0]",   m_regs.sv[4].w3).formatstr("%04X");
	state_add(NUON_SV5H,       "sv5[63:48]",  m_regs.sv[5].w3).formatstr("%04X");
	state_add(NUON_SV5MM,      "sv5[47:32]",  m_regs.sv[5].w3).formatstr("%04X");
	state_add(NUON_SV5M,       "sv5[31:16]",  m_regs.sv[5].w3).formatstr("%04X");
	state_add(NUON_SV5L,       "sv5[15:0]",   m_regs.sv[5].w3).formatstr("%04X");
	state_add(NUON_SV6H,       "sv6[63:48]",  m_regs.sv[6].w3).formatstr("%04X");
	state_add(NUON_SV6MM,      "sv6[47:32]",  m_regs.sv[6].w3).formatstr("%04X");
	state_add(NUON_SV6M,       "sv6[31:16]",  m_regs.sv[6].w3).formatstr("%04X");
	state_add(NUON_SV6L,       "sv6[15:0]",   m_regs.sv[6].w3).formatstr("%04X");
	state_add(NUON_SV7H,       "sv7[63:48]",  m_regs.sv[7].w3).formatstr("%04X");
	state_add(NUON_SV7MM,      "sv7[47:32]",  m_regs.sv[7].w3).formatstr("%04X");
	state_add(NUON_SV7M,       "sv7[31:16]",  m_regs.sv[7].w3).formatstr("%04X");
	state_add(NUON_SV7L,       "sv7[15:0]",   m_regs.sv[7].w3).formatstr("%04X");
	state_add(NUON_P0H,        "p0[47:32]",   m_regs.p[0].c2).formatstr("%04X");
	state_add(NUON_P0M,        "p0[31:16]",   m_regs.p[0].c1).formatstr("%04X");
	state_add(NUON_P0L,        "p0[15:0]",    m_regs.p[0].c0).formatstr("%04X");
	state_add(NUON_P1H,        "p1[47:32]",   m_regs.p[1].c2).formatstr("%04X");
	state_add(NUON_P1M,        "p1[31:16]",   m_regs.p[1].c1).formatstr("%04X");
	state_add(NUON_P1L,        "p1[15:0]",    m_regs.p[1].c0).formatstr("%04X");
	state_add(NUON_P2H,        "p2[47:32]",   m_regs.p[2].c2).formatstr("%04X");
	state_add(NUON_P2M,        "p2[31:16]",   m_regs.p[2].c1).formatstr("%04X");
	state_add(NUON_P2L,        "p2[15:0]",    m_regs.p[2].c0).formatstr("%04X");
	state_add(NUON_P3H,        "p3[47:32]",   m_regs.p[3].c2).formatstr("%04X");
	state_add(NUON_P3M,        "p3[31:16]",   m_regs.p[3].c1).formatstr("%04X");
	state_add(NUON_P3L,        "p3[15:0]",    m_regs.p[3].c0).formatstr("%04X");
	state_add(NUON_P4H,        "p4[47:32]",   m_regs.p[4].c2).formatstr("%04X");
	state_add(NUON_P4M,        "p4[31:16]",   m_regs.p[4].c1).formatstr("%04X");
	state_add(NUON_P4L,        "p4[15:0]",    m_regs.p[4].c0).formatstr("%04X");
	state_add(NUON_P5H,        "p5[47:32]",   m_regs.p[5].c2).formatstr("%04X");
	state_add(NUON_P5M,        "p5[31:16]",   m_regs.p[5].c1).formatstr("%04X");
	state_add(NUON_P5L,        "p5[15:0]",    m_regs.p[5].c0).formatstr("%04X");
	state_add(NUON_P6H,        "p6[47:32]",   m_regs.p[6].c2).formatstr("%04X");
	state_add(NUON_P6M,        "p6[31:16]",   m_regs.p[6].c1).formatstr("%04X");
	state_add(NUON_P6L,        "p6[15:0]",    m_regs.p[6].c0).formatstr("%04X");
	state_add(NUON_P7H,        "p7[47:32]",   m_regs.p[7].c2).formatstr("%04X");
	state_add(NUON_P7M,        "p7[31:16]",   m_regs.p[7].c1).formatstr("%04X");
	state_add(NUON_P7L,        "p7[15:0]",    m_regs.p[7].c0).formatstr("%04X");

	save_item(NAME(m_regs.r[ 0]));
	save_item(NAME(m_regs.r[ 1]));
	save_item(NAME(m_regs.r[ 2]));
	save_item(NAME(m_regs.r[ 3]));
	save_item(NAME(m_regs.r[ 4]));
	save_item(NAME(m_regs.r[ 5]));
	save_item(NAME(m_regs.r[ 6]));
	save_item(NAME(m_regs.r[ 7]));
	save_item(NAME(m_regs.r[ 8]));
	save_item(NAME(m_regs.r[ 9]));
	save_item(NAME(m_regs.r[10]));
	save_item(NAME(m_regs.r[11]));
	save_item(NAME(m_regs.r[12]));
	save_item(NAME(m_regs.r[13]));
	save_item(NAME(m_regs.r[14]));
	save_item(NAME(m_regs.r[15]));
	save_item(NAME(m_regs.r[16]));
	save_item(NAME(m_regs.r[17]));
	save_item(NAME(m_regs.r[18]));
	save_item(NAME(m_regs.r[19]));
	save_item(NAME(m_regs.r[20]));
	save_item(NAME(m_regs.r[21]));
	save_item(NAME(m_regs.r[22]));
	save_item(NAME(m_regs.r[23]));
	save_item(NAME(m_regs.r[24]));
	save_item(NAME(m_regs.r[25]));
	save_item(NAME(m_regs.r[26]));
	save_item(NAME(m_regs.r[27]));
	save_item(NAME(m_regs.r[28]));
	save_item(NAME(m_regs.r[29]));
	save_item(NAME(m_regs.r[30]));
	save_item(NAME(m_regs.r[31]));
	save_item(NAME(m_pc));
	save_item(NAME(m_branch_pc));
	save_item(NAME(m_branch_delay));
	save_item(NAME(m_cc));
	save_item(NAME(m_rc0));
	save_item(NAME(m_rc1));
	save_item(NAME(m_odmactl));

	set_icountptr(m_icount);
}



//-------------------------------------------------
//  device_reset
//-------------------------------------------------

void nuon_mpe_device::device_reset()
{
	m_pc = 0x20300000;
	m_branch_pc = 0;
	m_branch_delay = 0;
	m_rc0 = 0;
	m_rc1 = 0;
	m_odmactl = (1 << 5);
	m_odmacptr = 0;
	for (int i = 0; i < 32; i++)
		m_regs.r[i] = 0;
	m_cc = CC_C1Z | CC_C0Z;
}



//**************************************************************************
//  INTERNAL ADDRESS MAP
//**************************************************************************

void nuon_mpe_device::internal_map(address_map &map)
{
	map(0x20500040, 0x20500043).lrw32(NAME([this](offs_t offset){ return m_cc; }), NAME([this](offs_t offset, u32 data){ m_cc = data; m_cc &= 0x07ff; }));
	map(0x205001e0, 0x205001e3).rw(FUNC(nuon_mpe_device::rc0_r), FUNC(nuon_mpe_device::rc0_w));
	map(0x205001f0, 0x205001f3).rw(FUNC(nuon_mpe_device::rc1_r), FUNC(nuon_mpe_device::rc1_w));
	map(0x20500500, 0x20500503).lrw32(NAME([this](offs_t offset){ return m_odmactl; }), NAME([this](offs_t offset, u32 data){ m_odmactl = (m_odmactl & 0x1f) | (data & 0x60); }));
	map(0x20500510, 0x20500513).rw(FUNC(nuon_mpe_device::odmacptr_r), FUNC(nuon_mpe_device::odmacptr_w));
}


//-------------------------------------------------
//  memory_space_config
//-------------------------------------------------

device_memory_interface::space_config_vector nuon_mpe_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config)
	};
}



//-------------------------------------------------
//  create_disassembler
//-------------------------------------------------

std::unique_ptr<util::disasm_interface> nuon_mpe_device::create_disassembler()
{
	return std::make_unique<nuon_disassembler>();
}



//-------------------------------------------------
//  state_string_export
//-------------------------------------------------

void nuon_mpe_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
	switch (entry.index())
	{
	case STATE_GENFLAGS:
	case NUON_CC:
		// CF1 CF0 MOD- MOD>= C1Z C0Z MV N V C Z
		str = string_format("%s %s %s %s %s %s %s %c %c %c %c",
			BIT(m_cc, CC_CF1_BIT) ? "CF1" : "...",
			BIT(m_cc, CC_CF0_BIT) ? "CF0" : "...",
			BIT(m_cc, CC_MODMI_BIT) ? "MOD-" : "....",
			BIT(m_cc, CC_MODGE_BIT) ? "MOD>=" : ".....",
			BIT(m_cc, CC_C1Z_BIT) ? "C1Z" : "...",
			BIT(m_cc, CC_C0Z_BIT) ? "C0Z" : "...",
			BIT(m_cc, CC_MV_BIT) ? "MV" : "..",
			BIT(m_cc, CC_N_BIT) ? 'N':'.',
			BIT(m_cc, CC_V_BIT) ? 'V':'.',
			BIT(m_cc, CC_C_BIT) ? 'C':'.',
			BIT(m_cc, CC_Z_BIT) ? 'Z':'.');
		break;
	}
}



//-------------------------------------------------
//  rc0_r/w
//-------------------------------------------------

u32 nuon_mpe_device::rc0_r(offs_t offset)
{
	return m_rc0;
}

void nuon_mpe_device::rc0_w(offs_t offset, u32 data)
{
	m_rc0 = data & 0x0fff;
	if (m_rc0)
		m_cc &= ~CC_C0Z;
	else
		m_cc |= CC_C0Z;
}



//-------------------------------------------------
//  rc1_r/w
//-------------------------------------------------

u32 nuon_mpe_device::rc1_r(offs_t offset)
{
	return m_rc1;
}

void nuon_mpe_device::rc1_w(offs_t offset, u32 data)
{
	m_rc1 = data & 0x0fff;
	if (m_rc1)
		m_cc &= ~CC_C1Z;
	else
		m_cc |= CC_C1Z;
}



//-------------------------------------------------
//  odmacptr_r/w
//-------------------------------------------------

u32 nuon_mpe_device::odmacptr_r(offs_t offset)
{
	return m_odmacptr;
}

void nuon_mpe_device::odmacptr_w(offs_t offset, u32 data)
{
	m_odmacptr = data;
	// TODO: Actual DMA timing
	if (data)
	{
		const u32 flags = m_program->read_dword(data);
		// const bool remote = BIT(flags, 28);
		u32 length = (flags >> 16) & 0xff;
		const bool to_internal = BIT(flags, 13);

		u32 base_addr = m_program->read_dword(data + 4);
		u32 internal_addr = m_program->read_dword(data + 8);

		printf("Doing DMA: %08x %08x %08x\n", flags, base_addr, internal_addr);

		if (to_internal)
		{
			while (length)
			{
				m_program->write_dword(internal_addr, m_program->read_dword(base_addr));
				internal_addr += 4;
				base_addr += 4;
				length--;
			}
		}
		else
		{
			while (length)
			{
				m_program->write_dword(base_addr, m_program->read_dword(internal_addr));
				internal_addr += 4;
				base_addr += 4;
				length--;
			}
		}
	}
}

//-------------------------------------------------
//  execute_packet
//-------------------------------------------------

void nuon_mpe_device::execute_packet(bool &cont)
{
	const u16 opc1 = m_program->read_word(m_pc);
	if ((opc1 & 0xc000) == 0x0000)
	{
		// 16-bit ALU
		cont = false;
		m_pc += 2;

		unimplemented_opcode(m_pc - 2, &opc1);
		return;
	}

	if ((opc1 & 0x4000) == 0x4000)
	{
		// 16-bit normal non-ALU
		cont = !(opc1 & 0x8000);
		m_pc += 2;

		if ((opc1 & 0x7c00) == 0x6800)
		{	// f110 10cc crrr rrrr, bra cc1, pc + r/7s << 1
			s32 pc_offset = (opc1 & 0x007f) << 1;
			if (pc_offset & (1 << 7))
				pc_offset |= 0xffffff00;

			switch ((opc1 & 0x0380) >> 5)
			{
			case 0x18: // c0ne
				if (m_rc0)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 2, &opc1);
				return;
			}
			return;
		}

		if ((opc1 & 0x7ffc) == 0x7904)
		{	// dec
			if (BIT(opc1, 1))
			{
				m_rc0--;
				if (m_rc0 == 0)
					m_cc |= CC_C0Z;
				else
					m_cc &= ~CC_C0Z;
			}
			if (BIT(opc1, 0))
			{
				m_rc1--;
				if (m_rc1 == 0)
					m_cc |= CC_C1Z;
				else
					m_cc &= ~CC_C1Z;
			}
			return;
		}

		unimplemented_opcode(m_pc - 2, &opc1);
		return;
	}

	if ((opc1 & 0xf800) == 0x8000)
	{
		// 16-bit special
		cont = ((opc1 & 0x0100) == 0x0000);
		m_pc += 2;

		const u16 opc1_masked = opc1 & 0xfeff;
		if (opc1_masked == 0x8000)
		{	// nop
			return;
		}

		if (opc1_masked == 0x8200)
		{	// breakpoint
			unimplemented_opcode(m_pc - 2, &opc1);
			return;
		}

		unimplemented_opcode(m_pc - 2, &opc1);
		return;
	}

	if ((opc1 & 0xf000) == 0x9000)
	{
		// 32-bit instruction
		const u16 opc2 = m_program->read_word(m_pc + 2);
		cont = ((opc2 & 0xf000) == 0xa000);
		m_pc += 4;

		// 32-bit ECU instructions
		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xef80) == 0xa300)
		{	// jmp cc, 20300000 + wv/12u << 1
			const u32 wv = ((opc2 & 0x007f) << 5) | (opc1 & 0x001f);
			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x11: // unconditional
				m_branch_pc = 0x20300000 | (wv << 1);
				m_branch_delay = 3;
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xee00) == 0xa800)
		{	// bra cc, pc + wv/14s << 1, nop
			s32 pc_offset = ((opc2 & 0x1ff) << 6) | ((opc1 & 0x1f) << 1);
			if (pc_offset & (1 << 14))
				pc_offset |= 0xffff8000;

			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x00: // non-equal/non-zero
				if (!BIT(m_cc, CC_Z_BIT))
					m_pc += pc_offset;
				return;
			case 0x11: // unconditional
				m_pc += pc_offset;
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xefff) == 0xae80)
		{	// jmp cc, (rn), nop
			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x11: // unconditional
				m_pc = m_regs.r[opc1 & 0x1f];
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		// 32-bit mem instructions
		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xefc0) == 0xa280)
		{	// ld_s 20500000 + om/11u << 2, rn
			const u16 om = ((opc2 & 0x003f) << 5) | ((opc1 & 0x01e0) >> 5);
			m_regs.r[opc1 & 0x1f] = m_program->read_dword(0x20500000 + (om << 2));
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xefc0) == 0xaa80)
		{	// st_s rn, 20500000 + om/11u << 2
			const u16 om = ((opc2 & 0x003f) << 5) | ((opc1 & 0x01e0) >> 5);
			m_program->write_dword(0x20500000 + (om << 2), m_regs.r[opc1 & 0x1f]);
			printf("st_s Writing %08x to %08x\n", m_regs.r[opc1 & 0x1f], 0x20500000 + (om << 2));
			return;
		}

		if ((opc1 & 0xfc03) == 0x9400 && (opc2 & 0xefff) == 0xac10)
		{	// st_v vm, (rn)
			const u32 rn = m_regs.r[(opc1 >> 5) & 0x1f] & 0xfffffff0;
			const u32 vm = opc1 & 0x1c;
			printf("st_v Writing %08x to %08x\n", m_regs.r[vm], rn);
			printf("st_v Writing %08x to %08x\n", m_regs.r[vm + 1], rn + 4);
			printf("st_v Writing %08x to %08x\n", m_regs.r[vm + 2], rn + 8);
			printf("st_v Writing %08x to %08x\n", m_regs.r[vm + 3], rn + 12);
			m_program->write_dword(rn, m_regs.r[vm]);
			m_program->write_dword(rn + 4, m_regs.r[vm + 1]);
			m_program->write_dword(rn + 8, m_regs.r[vm + 2]);
			m_program->write_dword(rn + 12, m_regs.r[vm + 3]);
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xee00) == 0xae00)
		{	// st_s #wv/10u, 20500000 + om/9 << 4
			const u16 om = (opc2 & 0x01e0) | ((opc1 >> 5) & 0x001f);
			const u16 wv = ((opc2 & 0x001f) << 5) | (opc1 & 0x001f);
			m_program->write_dword(0x20500000 + (om << 4), wv);
			return;
		}

		// 32-bit ALU instructions
		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa040)
		{	// add #wv/10u, rn
			u64 wv = ((opc2 & 0x1f) << 5) | ((opc1 & 0x01e0) >> 5);
			u64 src = m_regs.r[opc1 & 0x1f];
			u64 sum = src + wv;
			m_regs.r[opc1 & 0x1f] = (u32)sum;
			if (sum != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(sum, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (sum & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if ((s32)sum < (s32)src)
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa820)
		{	// bits #v/5u, >>#d/5u, rn
			const u16 sk = (opc1 >> 5) & 0x1f;
			const u16 m = opc1 & 0x1f;

			u32 val = m_regs.r[sk] >> (opc2 & 0x1f);
			val &= ~(0xffffffff << (m + 1));
			m_regs.r[sk] = val;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (val == 0)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			return;
		}

		unimplemented_opcode(m_pc - 4, &opc1, &opc2);
		return;
	}

	if ((opc1 & 0xf800) == 0x8800)
	{
		// 32-bit prefix
		const u16 opc2 = m_program->read_word(m_pc + 2);
		const u16 opc3 = m_program->read_word(m_pc + 4);

		if ((opc3 & 0xc000) == 0x0000)
		{
			// 32+16-bit ALU
			cont = false;
			m_pc += 6;

			unimplemented_opcode(m_pc - 6, &opc1, &opc2, &opc3);
			return;
		}

		if ((opc3 & 0x4000) == 0x4000)
		{
			// 32+16-bit normal non-ALU
			cont = !(opc3 & 0x8000);
			m_pc += 6;

			if ((opc3 & 0x7c00) == 0x5c00)
			{	// mv_s #xw/32s, rn
				m_regs.r[(opc3 >> 5) & 0x1f] = ((opc1 & 0x7ff) << 21) | (opc2 << 5) | (opc3 & 0x1f);
				return;
			}

			unimplemented_opcode(m_pc - 6, &opc1, &opc2, &opc3);
			return;
		}

		if ((opc3 & 0xf000) == 0x9000)
		{
			// 32+32-bit instructions
			u16 opc4 = m_program->read_word(m_pc + 6);
			cont = ((opc4 & 0xf000) == 0xa000);
			m_pc += 8;

			unimplemented_opcode(m_pc - 8, &opc1, &opc2, &opc3, &opc4);
			return;
		}

		cont = false;
		m_pc += 6;
		unimplemented_opcode(m_pc - 6, &opc1, &opc2, &opc3);
		return;
	}

	cont = false;
	m_pc += 2;
	unimplemented_opcode(m_pc - 2, &opc1);
}



//-------------------------------------------------
//  execute_run
//-------------------------------------------------

void nuon_mpe_device::execute_run()
{
	while (m_icount > 0)
	{
		bool cont = true;
		while (cont)
		{
			debugger_instruction_hook(m_pc);
			execute_packet(cont);
			if (m_branch_delay)
			{
				m_branch_delay--;
				if (!m_branch_delay)
				{
					m_pc = m_branch_pc;
				}
			}
		}
		m_icount -= 1;
	}
}



//-------------------------------------------------
//  unimplemented_opcode - bail on unspuported
//  instruction
//-------------------------------------------------

void nuon_mpe_device::unimplemented_opcode(const u32 pc, const u16 *opc1, const u16 *opc2, const u16 *opc3, const u16 *opc4)
{
//  machine().debug_break();
	if (opc4 != nullptr)
	{
		fatalerror("Nuon: unknown opcode (%04x %04x %04x %04x) at %08x\n", *opc1, *opc2, *opc3, *opc4, pc);
	}
	else if (opc3 != nullptr)
	{
		fatalerror("Nuon: unknown opcode (%04x %04x %04x) at %08x\n", *opc1, *opc2, *opc3, pc);
	}
	else if (opc2 != nullptr)
	{
		fatalerror("Nuon: unknown opcode (%04x %04x) at %08x\n", *opc1, *opc2, pc);
	}
	else
	{
		fatalerror("Nuon: unknown opcode (%04x) at %08x\n", *opc1, pc);
	}
}
