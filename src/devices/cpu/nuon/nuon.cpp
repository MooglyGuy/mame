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

#define VERBOSE (1)
#include "logmacro.h"

//**************************************************************************
//  DEVICE INTERFACE
//**************************************************************************

DEFINE_DEVICE_TYPE(NUON,   nuon_mpe_device,   "nuon",   "Aries 3 \"Nuon\" MPE")

//-------------------------------------------------
//  nuon_mpe_device - constructor
//-------------------------------------------------

nuon_mpe_device::nuon_mpe_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: cpu_device(mconfig, NUON, tag, owner, clock)
	, m_commbus_out(*this)
	, m_program_config("program", ENDIANNESS_BIG, 32, 32)
	, m_dma_config("dma", ENDIANNESS_BIG, 32, 32)
	, m_dram(*this, "dram")
	, m_iram(*this, "iram")
	, m_pc(0x20300000)
	, m_branch_pc(0)
	, m_branch_delta(0)
	, m_branch_delay(0)
	, m_configa(0x04040003)
	, m_icount(0)
{
}

//-------------------------------------------------
//  device_start
//-------------------------------------------------

void nuon_mpe_device::device_start()
{
	m_commbus_out.resolve_safe();

	m_program = &space(AS_PROGRAM);
	m_dma = &space(AS_IO);

	// register our state for the debugger
	state_add(STATE_GENPC,     "GENPC",       m_pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC",       m_pc).noshow();
	state_add(STATE_GENFLAGS,  "GENFLAGS",    m_cc).callimport().callexport().formatstr("%37s").noshow();
	state_add(NUON_PC,         "pc",          m_pc).formatstr("%08X");
	state_add(NUON_CC,         "cc",          m_cc).callimport().callexport().formatstr("%37s");
	state_add(NUON_SP,         "sp",          m_sp).formatstr("%08X");
	state_add(NUON_ACSHIFT,    "acshift",     m_acshift).formatstr("%02X");
	state_add(NUON_RZ,         "rz",          m_rz).formatstr("%08X");
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
	state_add(NUON_RC0,        "rc0",         m_rc0).formatstr("%04X");
	state_add(NUON_RC1,        "rc1",         m_rc1).formatstr("%04X");
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
	save_item(NAME(m_regs_new.r[ 0]));
	save_item(NAME(m_regs_new.r[ 1]));
	save_item(NAME(m_regs_new.r[ 2]));
	save_item(NAME(m_regs_new.r[ 3]));
	save_item(NAME(m_regs_new.r[ 4]));
	save_item(NAME(m_regs_new.r[ 5]));
	save_item(NAME(m_regs_new.r[ 6]));
	save_item(NAME(m_regs_new.r[ 7]));
	save_item(NAME(m_regs_new.r[ 8]));
	save_item(NAME(m_regs_new.r[ 9]));
	save_item(NAME(m_regs_new.r[10]));
	save_item(NAME(m_regs_new.r[11]));
	save_item(NAME(m_regs_new.r[12]));
	save_item(NAME(m_regs_new.r[13]));
	save_item(NAME(m_regs_new.r[14]));
	save_item(NAME(m_regs_new.r[15]));
	save_item(NAME(m_regs_new.r[16]));
	save_item(NAME(m_regs_new.r[17]));
	save_item(NAME(m_regs_new.r[18]));
	save_item(NAME(m_regs_new.r[19]));
	save_item(NAME(m_regs_new.r[20]));
	save_item(NAME(m_regs_new.r[21]));
	save_item(NAME(m_regs_new.r[22]));
	save_item(NAME(m_regs_new.r[23]));
	save_item(NAME(m_regs_new.r[24]));
	save_item(NAME(m_regs_new.r[25]));
	save_item(NAME(m_regs_new.r[26]));
	save_item(NAME(m_regs_new.r[27]));
	save_item(NAME(m_regs_new.r[28]));
	save_item(NAME(m_regs_new.r[29]));
	save_item(NAME(m_regs_new.r[30]));
	save_item(NAME(m_regs_new.r[31]));
	save_item(NAME(m_pc));
	save_item(NAME(m_branch_pc));
	save_item(NAME(m_branch_delta));
	save_item(NAME(m_branch_delay));
	save_item(NAME(m_mpectl));
	save_item(NAME(m_excepsrc));
	save_item(NAME(m_cc));
	save_item(NAME(m_rz));
	save_item(NAME(m_intctl));
	save_item(NAME(m_inten1));
	save_item(NAME(m_rc0));
	save_item(NAME(m_rc1));
	save_item(NAME(m_acshift));
	save_item(NAME(m_sp));
	save_item(NAME(m_odmactl));
	save_item(NAME(m_odmacptr));
	save_item(NAME(m_commctl));
	save_item(NAME(m_mdmactl));
	save_item(NAME(m_mdmacptr));
	save_item(NAME(m_commxmit));
	save_item(NAME(m_configa));
	save_item(NAME(m_dcachectl));
	save_item(NAME(m_icachectl));

	set_icountptr(m_icount);

	u16 *irom = (u16 *)memregion("irom")->base();
	for (u32 i = 0; i < 0x10000/2; i++)
		irom[i] = 0xf231;
}



//-------------------------------------------------
//  device_reset
//-------------------------------------------------

void nuon_mpe_device::device_reset()
{
	m_pc = 0x20300000;
	m_branch_pc = 0;
	m_branch_delta = 0;
	m_branch_delay = 0;
	m_mpectl = (m_mpe_id == 0 ? 2 : 0);
	m_excepsrc = 0;
	m_rz = 0;
	m_intctl = 0;
	m_inten1 = 0;
	m_rc0 = 0;
	m_rc1 = 0;
	m_acshift = 0;
	m_sp = 0;
	m_odmactl = (1 << 5);
	m_odmacptr = 0;
	m_commctl = 0;
	m_commxmit[0] = 0;
	m_commxmit[1] = 0;
	m_commxmit[2] = 0;
	m_commxmit[3] = 0;
	m_dcachectl = 0;
	m_icachectl = 0;

	for (int i = 0; i < 32; i++)
	{
		m_regs.r[i] = 0;
		m_regs_new.r[i] = 0;
	}
	m_cc = CC_C1Z | CC_C0Z;

	if (m_mpe_id != 0)
		suspend(SUSPEND_REASON_HALT, true);
}

ROM_START(nuonmpe)
	ROM_REGION(0x10000, "irom", ROMREGION_ERASE00)
	ROM_LOAD("irom.bin", 0x00000, 0x10000, NO_DUMP)
ROM_END

const tiny_rom_entry *nuon_mpe_device::device_rom_region() const
{
	return ROM_NAME(nuonmpe);
}


//**************************************************************************
//  REGISTER MAP
//**************************************************************************

void nuon_mpe_device::dma_map(address_map &map)
{
	map(0x100000, 0x1067ff).ram().share("dram"); // data RAM (26/16/16/20 kBytes on MPE 0/1/2/3
	map(0x200000, 0x20ffff).rom().region("irom", 0);
	map(0x300000, 0x304fff).ram().share("iram"); // instruction RAM (20/16/16/20 kBytes on MPE 0/1/2/3
	map(0x400000, 0x4003ff).ram(); // data tag RAM
	map(0x480000, 0x4803ff).ram(); // instruction tag RAM
	map(0x500000, 0x500003).rw(FUNC(nuon_mpe_device::mpectl_r), FUNC(nuon_mpe_device::mpectl_w));
	map(0x500010, 0x500013).rw(FUNC(nuon_mpe_device::excepsrc_r), FUNC(nuon_mpe_device::excepsrc_w));
	map(0x500020, 0x500023).rw(FUNC(nuon_mpe_device::excepclr_r), FUNC(nuon_mpe_device::excepclr_w));
	map(0x500040, 0x500043).lrw32(NAME([this](offs_t offset){ return m_cc; }), NAME([this](offs_t offset, u32 data){ m_cc = data; m_cc &= 0x07ff; }));
	map(0x500080, 0x500083).rw(FUNC(nuon_mpe_device::rz_r), FUNC(nuon_mpe_device::rz_w));
	map(0x5000e0, 0x5000e3).rw(FUNC(nuon_mpe_device::intclr_r), FUNC(nuon_mpe_device::intclr_w));
	map(0x5000f0, 0x5000f3).rw(FUNC(nuon_mpe_device::intctl_r), FUNC(nuon_mpe_device::intctl_w));
	map(0x500100, 0x500103).rw(FUNC(nuon_mpe_device::inten1_r), FUNC(nuon_mpe_device::inten1_w));
	map(0x5001e0, 0x5001e3).rw(FUNC(nuon_mpe_device::rc0_r), FUNC(nuon_mpe_device::rc0_w));
	map(0x5001f0, 0x5001f3).rw(FUNC(nuon_mpe_device::rc1_r), FUNC(nuon_mpe_device::rc1_w));
	map(0x5002d0, 0x5002d3).rw(FUNC(nuon_mpe_device::acshift_r), FUNC(nuon_mpe_device::acshift_w));
	map(0x5002e0, 0x5002e3).rw(FUNC(nuon_mpe_device::sp_r), FUNC(nuon_mpe_device::sp_w));
	map(0x500300, 0x500303).rw(FUNC(nuon_mpe_device::gpr_r<0>), FUNC(nuon_mpe_device::gpr_w<0>));
	map(0x500310, 0x500313).rw(FUNC(nuon_mpe_device::gpr_r<1>), FUNC(nuon_mpe_device::gpr_w<1>));
	map(0x500320, 0x500323).rw(FUNC(nuon_mpe_device::gpr_r<2>), FUNC(nuon_mpe_device::gpr_w<2>));
	map(0x500330, 0x500333).rw(FUNC(nuon_mpe_device::gpr_r<3>), FUNC(nuon_mpe_device::gpr_w<3>));
	map(0x500340, 0x500343).rw(FUNC(nuon_mpe_device::gpr_r<4>), FUNC(nuon_mpe_device::gpr_w<4>));
	map(0x500350, 0x500353).rw(FUNC(nuon_mpe_device::gpr_r<5>), FUNC(nuon_mpe_device::gpr_w<5>));
	map(0x500360, 0x500363).rw(FUNC(nuon_mpe_device::gpr_r<6>), FUNC(nuon_mpe_device::gpr_w<6>));
	map(0x500370, 0x500373).rw(FUNC(nuon_mpe_device::gpr_r<7>), FUNC(nuon_mpe_device::gpr_w<7>));
	map(0x500380, 0x500383).rw(FUNC(nuon_mpe_device::gpr_r<8>), FUNC(nuon_mpe_device::gpr_w<8>));
	map(0x500390, 0x500393).rw(FUNC(nuon_mpe_device::gpr_r<9>), FUNC(nuon_mpe_device::gpr_w<9>));
	map(0x5003a0, 0x5003a3).rw(FUNC(nuon_mpe_device::gpr_r<10>), FUNC(nuon_mpe_device::gpr_w<10>));
	map(0x5003b0, 0x5003b3).rw(FUNC(nuon_mpe_device::gpr_r<11>), FUNC(nuon_mpe_device::gpr_w<11>));
	map(0x5003c0, 0x5003c3).rw(FUNC(nuon_mpe_device::gpr_r<12>), FUNC(nuon_mpe_device::gpr_w<12>));
	map(0x5003d0, 0x5003d3).rw(FUNC(nuon_mpe_device::gpr_r<13>), FUNC(nuon_mpe_device::gpr_w<13>));
	map(0x5003e0, 0x5003e3).rw(FUNC(nuon_mpe_device::gpr_r<14>), FUNC(nuon_mpe_device::gpr_w<14>));
	map(0x5003f0, 0x5003f3).rw(FUNC(nuon_mpe_device::gpr_r<15>), FUNC(nuon_mpe_device::gpr_w<15>));
	map(0x500400, 0x500403).rw(FUNC(nuon_mpe_device::gpr_r<16>), FUNC(nuon_mpe_device::gpr_w<16>));
	map(0x500410, 0x500413).rw(FUNC(nuon_mpe_device::gpr_r<17>), FUNC(nuon_mpe_device::gpr_w<17>));
	map(0x500420, 0x500423).rw(FUNC(nuon_mpe_device::gpr_r<18>), FUNC(nuon_mpe_device::gpr_w<18>));
	map(0x500430, 0x500433).rw(FUNC(nuon_mpe_device::gpr_r<19>), FUNC(nuon_mpe_device::gpr_w<19>));
	map(0x500440, 0x500443).rw(FUNC(nuon_mpe_device::gpr_r<20>), FUNC(nuon_mpe_device::gpr_w<20>));
	map(0x500450, 0x500453).rw(FUNC(nuon_mpe_device::gpr_r<21>), FUNC(nuon_mpe_device::gpr_w<21>));
	map(0x500460, 0x500463).rw(FUNC(nuon_mpe_device::gpr_r<22>), FUNC(nuon_mpe_device::gpr_w<22>));
	map(0x500470, 0x500473).rw(FUNC(nuon_mpe_device::gpr_r<23>), FUNC(nuon_mpe_device::gpr_w<23>));
	map(0x500480, 0x500483).rw(FUNC(nuon_mpe_device::gpr_r<24>), FUNC(nuon_mpe_device::gpr_w<24>));
	map(0x500490, 0x500493).rw(FUNC(nuon_mpe_device::gpr_r<25>), FUNC(nuon_mpe_device::gpr_w<25>));
	map(0x5004a0, 0x5004a3).rw(FUNC(nuon_mpe_device::gpr_r<26>), FUNC(nuon_mpe_device::gpr_w<26>));
	map(0x5004b0, 0x5004b3).rw(FUNC(nuon_mpe_device::gpr_r<27>), FUNC(nuon_mpe_device::gpr_w<27>));
	map(0x5004c0, 0x5004c3).rw(FUNC(nuon_mpe_device::gpr_r<28>), FUNC(nuon_mpe_device::gpr_w<28>));
	map(0x5004d0, 0x5004d3).rw(FUNC(nuon_mpe_device::gpr_r<29>), FUNC(nuon_mpe_device::gpr_w<29>));
	map(0x5004e0, 0x5004e3).rw(FUNC(nuon_mpe_device::gpr_r<30>), FUNC(nuon_mpe_device::gpr_w<30>));
	map(0x5004f0, 0x5004f3).rw(FUNC(nuon_mpe_device::gpr_r<31>), FUNC(nuon_mpe_device::gpr_w<31>));
	map(0x500500, 0x500503).lrw32(NAME([this](offs_t offset){ return m_odmactl; }), NAME([this](offs_t offset, u32 data){ m_odmactl = (m_odmactl & 0x1f) | (data & 0x60); }));
	map(0x500510, 0x500513).rw(FUNC(nuon_mpe_device::odmacptr_r), FUNC(nuon_mpe_device::odmacptr_w));
	map(0x500600, 0x500603).rw(FUNC(nuon_mpe_device::mdmactl_r), FUNC(nuon_mpe_device::mdmactl_w));
	map(0x500610, 0x500613).rw(FUNC(nuon_mpe_device::mdmacptr_r), FUNC(nuon_mpe_device::mdmacptr_w));
	map(0x5007f0, 0x5007f3).rw(FUNC(nuon_mpe_device::commctl_r), FUNC(nuon_mpe_device::commctl_w));
	map(0x500800, 0x50080f).rw(FUNC(nuon_mpe_device::commxmit_r), FUNC(nuon_mpe_device::commxmit_w));
	map(0x500ff0, 0x500ff3).r(FUNC(nuon_mpe_device::configa_r));
	map(0x500ff8, 0x500ffb).rw(FUNC(nuon_mpe_device::dcachectl_r), FUNC(nuon_mpe_device::dcachectl_w));
	map(0x500ffc, 0x500fff).rw(FUNC(nuon_mpe_device::icachectl_r), FUNC(nuon_mpe_device::icachectl_w));
}



//-------------------------------------------------
//  memory_space_config
//-------------------------------------------------

device_memory_interface::space_config_vector nuon_mpe_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config),
		std::make_pair(AS_IO, &m_dma_config)
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
//  mpectl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::mpectl_r(offs_t offset)
{
	LOG("%s: MPE Control read: %08x\n", machine().describe_context(), m_mpectl);
	return m_mpectl;
}

void nuon_mpe_device::mpectl_w(offs_t offset, u32 data)
{
	LOG("%s: MPE Control write: %08x\n", machine().describe_context(), data);
	if (!BIT(m_mpectl, 1) && BIT(data, 1))
	{
		m_mpectl |= (1 << 1);
		resume(SUSPEND_REASON_HALT);
	}
}



//-------------------------------------------------
//  excepclr_r/w
//-------------------------------------------------

u32 nuon_mpe_device::excepclr_r(offs_t offset)
{
	LOG("%s: Exception Clear read: %08x\n", machine().describe_context(), 0);
	return 0;
}

void nuon_mpe_device::excepclr_w(offs_t offset, u32 data)
{
	LOG("%s: Exception Clear write: %08x\n", machine().describe_context(), data);
	for (u32 bit = 0; bit <= 12; bit++)
	{
		if (BIT(data, bit))
		{
			m_excepsrc &= ~(1 << bit);
		}
	}
}



//-------------------------------------------------
//  excepsrc_r/w
//-------------------------------------------------

u32 nuon_mpe_device::excepsrc_r(offs_t offset)
{
	LOG("%s: Exception Source Register read: %08x\n", machine().describe_context(), m_excepsrc);
	return m_excepsrc;
}

void nuon_mpe_device::excepsrc_w(offs_t offset, u32 data)
{
	LOG("%s: Exception Source Register write: %08x\n", machine().describe_context(), data);
	for (u32 bit = 0; bit <= 12; bit++)
	{
		if (BIT(data, bit))
		{
			m_excepsrc |= (1 << bit);
		}
	}
}



//-------------------------------------------------
//  rz_r/w
//-------------------------------------------------

u32 nuon_mpe_device::rz_r(offs_t offset)
{
	LOG("%s: Subroutine Return Address read: %08x\n", machine().describe_context(), m_rz);
	return m_rz;
}

void nuon_mpe_device::rz_w(offs_t offset, u32 data)
{
	LOG("%s: Subroutine Return Address write: %08x\n", machine().describe_context(), data);
	m_rz = data;
}



//-------------------------------------------------
//  intclr_r/w
//-------------------------------------------------

u32 nuon_mpe_device::intclr_r(offs_t offset)
{
	LOG("%s: Interrupt Clear read: %08x\n", machine().describe_context(), 0);
	return 0;
}

void nuon_mpe_device::intclr_w(offs_t offset, u32 data)
{
	LOG("%s: Interrupt Clear write: %08x\n", machine().describe_context(), data);
}



//-------------------------------------------------
//  intctl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::intctl_r(offs_t offset)
{
	LOG("%s: Interrupt Control read: %08x\n", machine().describe_context(), m_intctl);
	return m_intctl;
}

void nuon_mpe_device::intctl_w(offs_t offset, u32 data)
{
	LOG("%s: Interrupt Control write: %08x\n", machine().describe_context(), data);
	LOG("%s:     imaskHw1_clr: %d\n", machine().describe_context(), BIT(data, 0));
	LOG("%s:     imaskHw1_set: %d\n", machine().describe_context(), BIT(data, 1));
	LOG("%s:     imaskSw1_clr: %d\n", machine().describe_context(), BIT(data, 2));
	LOG("%s:     imaskSw1_set: %d\n", machine().describe_context(), BIT(data, 3));
	LOG("%s:     imaskHw2_clr: %d\n", machine().describe_context(), BIT(data, 4));
	LOG("%s:     imaskHw2_set: %d\n", machine().describe_context(), BIT(data, 5));
	LOG("%s:     imaskSw2_clr: %d\n", machine().describe_context(), BIT(data, 6));
	LOG("%s:     imaskSw2_set: %d\n", machine().describe_context(), BIT(data, 7));
	if (BIT(data, 0))
		m_intctl &= ~(1 << 1);
	else if (BIT(data, 1))
		m_intctl |= (1 << 1);

	if (BIT(data, 2))
		m_intctl &= ~(1 << 3);
	else if (BIT(data, 3))
		m_intctl |= (1 << 3);

	if (BIT(data, 4))
		m_intctl &= ~(1 << 5);
	else if (BIT(data, 5))
		m_intctl |= (1 << 5);

	if (BIT(data, 6))
		m_intctl &= ~(1 << 7);
	else if (BIT(data, 7))
		m_intctl |= (1 << 7);
}



//-------------------------------------------------
//  inten1_r/w
//-------------------------------------------------

u32 nuon_mpe_device::inten1_r(offs_t offset)
{
	LOG("%s: Level-1 Interrupt Enable read: %08x\n", machine().describe_context(), m_inten1);
	return m_inten1;
}

void nuon_mpe_device::inten1_w(offs_t offset, u32 data)
{
	LOG("%s: Level-1 Interrupt Enable write: %08x\n", machine().describe_context(), data);
	m_inten1 = data;
}



//-------------------------------------------------
//  rc0_r/w
//-------------------------------------------------

u32 nuon_mpe_device::rc0_r(offs_t offset)
{
	LOG("%s: rc0 counter read: %08x\n", machine().describe_context(), m_rc0);
	return m_rc0;
}

void nuon_mpe_device::rc0_w(offs_t offset, u32 data)
{
	LOG("%s: rc0 counter write: %08x\n", machine().describe_context(), data);
	m_rc0 = data & 0xffff;
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
	LOG("%s: rc1 counter read: %08x\n", machine().describe_context(), m_rc1);
	return m_rc1;
}

void nuon_mpe_device::rc1_w(offs_t offset, u32 data)
{
	LOG("%s: rc1 counter write: %08x\n", machine().describe_context(), data);
	m_rc1 = data & 0xffff;
	if (m_rc1)
		m_cc &= ~CC_C1Z;
	else
		m_cc |= CC_C1Z;
}



//-------------------------------------------------
//  acshift_r/w
//-------------------------------------------------

u32 nuon_mpe_device::acshift_r(offs_t offset)
{
	LOG("%s: Scalar Multiply Shift Control read: %08x\n", machine().describe_context(), m_acshift);
	return m_acshift;
}

void nuon_mpe_device::acshift_w(offs_t offset, u32 data)
{
	LOG("%s: Scalar Multiply Shift Control write: %08x\n", machine().describe_context(), data);
	m_acshift = data & 0x000000ff;
}



//-------------------------------------------------
//  sp_r/w
//-------------------------------------------------

u32 nuon_mpe_device::sp_r(offs_t offset)
{
	LOG("%s: Stack Pointer read: %08x\n", machine().describe_context(), m_sp);
	return m_sp;
}

void nuon_mpe_device::sp_w(offs_t offset, u32 data)
{
	LOG("%s: Stack Pointer write: %08x\n", machine().describe_context(), data);
	m_sp = data & 0xfffffff0;
}



//-------------------------------------------------
//  gpr_r/w
//-------------------------------------------------

template <int RegIdx>
u32 nuon_mpe_device::gpr_r(offs_t offset)
{
	return m_regs.r[RegIdx];
}

template <int RegIdx>
void nuon_mpe_device::gpr_w(offs_t offset, u32 data)
{
	m_regs.r[RegIdx] = data;
}



//-------------------------------------------------
//  odmacptr_r/w
//-------------------------------------------------

u32 nuon_mpe_device::odmacptr_r(offs_t offset)
{
	LOG("%s: Other Bus DMA packet pointer read: %08x\n", machine().describe_context(), m_odmacptr);
	return m_odmacptr;
}

void nuon_mpe_device::odmacptr_w(offs_t offset, u32 data)
{
	LOG("%s: Other Bus DMA packet pointer write: %08x\n", machine().describe_context(), data);

	m_odmacptr = data;
	// TODO: Actual DMA timing
	if (data)
	{
		const u32 flags = m_program->read_dword(data);
		const bool remote = BIT(flags, 28);
		u32 length = (flags >> 16) & 0xff;
		const bool to_internal = BIT(flags, 13);

		const u32 base_addr = m_program->read_dword(data + 4);
		const u32 internal_addr = m_program->read_dword(data + 8);

		u32 src_addr = to_internal ? base_addr : internal_addr;
		u32 dst_addr = to_internal ? internal_addr : base_addr;
		address_space *src_space = to_internal ? m_dma : (remote ? m_dma : m_program);
		address_space *dst_space = to_internal ? (remote ? m_dma : m_program) : m_dma;

		if (to_internal)
			LOG("Flags %08x: Transferring %08x bytes from base %08x to internal %08x\n", flags, length * 4, base_addr, internal_addr);
		else
			LOG("Flags %08x: Transferring %08x bytes from internal %08x to base %08x\n", flags, length * 4, internal_addr, base_addr);

		while (length)
		{
			dst_space->write_dword(dst_addr, src_space->read_dword(src_addr));
			src_addr += 4;
			dst_addr += 4;
			length--;
		}
	}
}



//-------------------------------------------------
//  mdmactl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::mdmactl_r(offs_t offset)
{
	LOG("%s: Main-Bus DMA control read: %08x\n", machine().describe_context(), m_mdmactl);
	return m_mdmactl;
}

void nuon_mpe_device::mdmactl_w(offs_t offset, u32 data)
{
	LOG("%s: Main-Bus DMA control write: %08x\n", machine().describe_context(), data);
	LOG("%s:     Write Done Count: %02x\n", machine().describe_context(), data >> 24);
	LOG("%s:     Read Done Count: %02x\n", machine().describe_context(), (data >> 16) & 0xff);
	LOG("%s:     Command Error: %d\n", machine().describe_context(), BIT(data, 15));
	LOG("%s:     Command Pointer Error: %d\n", machine().describe_context(), BIT(data, 14));
	LOG("%s:     Decrement Write Done Count: %d\n", machine().describe_context(), BIT(data, 11));
	LOG("%s:     Decrement Read Done Count: %d\n", machine().describe_context(), BIT(data, 10));
	LOG("%s:     Done Count Enable: %d\n", machine().describe_context(), BIT(data, 9));
	LOG("%s:     Done Count Disable: %d\n", machine().describe_context(), BIT(data, 8));
	LOG("%s:     Priority: %d\n", machine().describe_context(), (data >> 5) & 3);
	LOG("%s:     Command Pending: %d\n", machine().describe_context(), BIT(data, 4));
	LOG("%s:     Active Level: %d\n", machine().describe_context(), data & 15);
	if (BIT(data, 8))
	{
		data &= ~(3 << 8);
	}
	m_mdmactl = data;
}



//-------------------------------------------------
//  mdmactl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::mdmacptr_r(offs_t offset)
{
	LOG("%s: Main-Bus DMA command pointer read: %08x\n", machine().describe_context(), m_mdmacptr);
	return m_mdmacptr;
}

void nuon_mpe_device::mdmacptr_w(offs_t offset, u32 data)
{
	LOG("%s: Main-Bus DMA command pointer write (initiating DMA): %08x\n", machine().describe_context(), data);
	m_mdmacptr = data;

	if (data != 0)
	{
		const u32 flags = m_program->read_dword(m_mdmacptr);
		LOG("%s:     DMA Flags: %08x\n", machine().describe_context(), flags);
		LOG("%s:                PLAST: %d\n", machine().describe_context(), BIT(flags, 31));
		LOG("%s:                BATCH: %d\n", machine().describe_context(), BIT(flags, 30));
		LOG("%s:                CHAIN: %d\n", machine().describe_context(), BIT(flags, 29));
		LOG("%s:               REMOTE: %d\n", machine().describe_context(), BIT(flags, 28));
		LOG("%s:               DIRECT: %d\n", machine().describe_context(), BIT(flags, 27));
		LOG("%s:                  DUP: %d\n", machine().describe_context(), BIT(flags, 26));
		LOG("%s:              TRIGGER: %d\n", machine().describe_context(), BIT(flags, 25));
		LOG("%s:                ERROR: %d\n", machine().describe_context(), BIT(flags, 24));
		LOG("%s:         LENGTH/XSIZE: %d\n", machine().describe_context(), (flags >> 16) & 0xff);
		LOG("%s:                 TYPE: %d\n", machine().describe_context(), (flags >> 14) & 3);
		LOG("%s:                 READ: %d\n", machine().describe_context(), BIT(flags, 13));
		LOG("%s:                DEBUG: %d\n", machine().describe_context(), BIT(flags, 12));
		LOG("%s:                 MODE: %03x\n", machine().describe_context(), flags & 0x0fff);
		switch ((flags >> 14) & 3)
		{
		case 0: // Linear
		{
			static const char *const s_ul_types[4] = { "Must Be Zero", "Byte 1", "Byte 0", "Word" };
			static const char *const s_iis_types[8] =
			{
				"ULULULULULULULUL",
				"Byte Mode",
				"UL--UL--UL--UL--",
				"U-L-U-L-U-L-U-L-",
				"UL------UL------",
				"U---L---U---L---",
				"UL--------------",
				"U-------L-------"
			};
			LOG("%s:                     Linear:\n", machine().describe_context());
			LOG("%s:                          UL: %s\n", machine().describe_context(), s_ul_types[(flags >> 3) & 3]);
			LOG("%s:                         IIS: %s\n", machine().describe_context(), s_iis_types[flags & 7]);

			const u8 iis_type = flags & 7;

			if (iis_type != 0)
			{
				fatalerror("Unsupported linear DMA IIS type: %d (%s)\n", iis_type, s_iis_types[iis_type]);
			}

			u32 dst = m_program->read_dword(m_mdmacptr + 4);
			u32 length = (flags >> 16) & 0xff;
			if (BIT(flags, 27) || BIT(flags, 26))
			{
				// Direct Transfer or DUP mode
				u32 val;
				if (BIT(flags, 26))
					val = m_program->read_dword(m_program->read_dword(m_mdmacptr + 8));
				else
					val = m_program->read_dword(m_mdmacptr + 8);

				LOG("%s:     Transferring %d words of %08x to %08x\n", machine().describe_context(), length, val, dst);
				while (length)
				{
					m_dma->write_dword(dst, val);
					dst += 4;
					length--;
				}
			}
			else
			{
				// Memory Transfer
				u32 src = m_program->read_dword(m_mdmacptr + 8);
				if (BIT(flags, 13))
					std::swap<u32>(dst, src);

				LOG("%s:     Transferring %d words from %08x to %08x\n", machine().describe_context(), length, src, dst);
				while (length)
				{
					m_dma->write_dword(dst, m_program->read_dword(src));
					dst += 4;
					src += 4;
					length--;
				}
			}
		}	break;
		case 2: // Motion Predictor
		{
			static const char *const s_rc_types[4] = { "Luma", "Cr/Cb", "Cr", "Cb" };
			static const char *const s_fff_types[4] = { "16x16 frame", "16x16 field", "16x8 field top", "16x8 field bottom" };
			LOG("%s:                     Motion Predictor:\n", machine().describe_context());
			LOG("%s:                                 Mode: %s\n", machine().describe_context(), BIT(flags, 11) ? "Pixel" : "MCU");
			LOG("%s:                                    m: %d\n", machine().describe_context(), BIT(flags, 10));
			LOG("%s:                               Length: %s\n", machine().describe_context(), BIT(flags, 9) ? "Manual" : "Auto");
			LOG("%s:                                   mm: %d%d\n", machine().describe_context(), BIT(flags, 8), BIT(flags, 7));
			LOG("%s:                         Scale to 4:3: %s\n", machine().describe_context(), BIT(flags, 6));
			LOG("%s:                                    m: %d\n", machine().describe_context(), BIT(flags, 5));
			LOG("%s:                                   RC: %s\n", machine().describe_context(), s_rc_types[(flags >> 3) & 3]);
			LOG("%s:                                  FFF: %s\n", machine().describe_context(), s_fff_types[(flags >> 1) & 3]);
		}	break;
		case 3: // Pixel
		{
			static const char *const s_pix_types[16] =
			{
				"Z-only, mode 5", "4bpp", "16bpp", "8bpp",
				"32bpp", "16bpp+16Z", "32bpp+32Z", "Z-only, mode 6",
				"32bpp MPE, 16bpp DRAM", "16bpp+16Z MPE, 16bpp+16Z tripbuf C DRAM", "16bpp+16Z MPE, 16bpp+16Z tripbuf B DRAM", "16bpp+16Z MPE, 16bpp+16Z tripbuf A DRAM",
				"Z-only to 16bpp+16Z tripbuf in DRAM", "16bpp+16Z MPE, 16bpp+16Z doublebuf B DRAM", "16bpp+16Z MPE, 16bpp+16Z doublebuf A DRAM", "Z-only to 16bpp+16Z doublebuf in DRAM"
			};
			static const char *const s_z_types[8] = { "Never", "<", "==", "<=", ">", "!=", ">=", "Always" };
			LOG("%s:                     Pixel Mode:\n", machine().describe_context());
			LOG("%s:                         Cluster Addressing: %d\n", machine().describe_context(), BIT(flags, 11));
			LOG("%s:                                Backwards B: %d\n", machine().describe_context(), BIT(flags, 9));
			LOG("%s:                                  Direction: %s\n", machine().describe_context(), BIT(flags, 8) ? "Vertical" : "Horizontal");
			LOG("%s:                                       Type: %s\n", machine().describe_context(), s_pix_types[(flags >> 4) & 15]);
			LOG("%s:                                  Z-Compare: %d\n", machine().describe_context(), s_z_types[(flags >> 1) & 7]);
			LOG("%s:                                Backwards A: %d\n", machine().describe_context(), BIT(flags, 0));
			LOG("%s:     SDRAM Address: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 4));
			LOG("%s:       X Ptr & Len: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 8));
			LOG("%s:       Y Ptr & Len: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 12));
			if (BIT(flags, 27))
			{
				LOG("%s:       Pixel Data: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 16));
			}
			else
			{
				LOG("%s:         MPE Addr: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 16));
			}
		}	break;
		default:
			LOG("%s:                     Unsupported Type (1)\n", machine().describe_context());
			LOG("%s:     2nd Word: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 4));
			LOG("%s:     3rd Word: %08x\n", machine().describe_context(), m_program->read_dword(m_mdmacptr + 8));
			break;
		}
	}
}



//-------------------------------------------------
//  commctl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::commctl_r(offs_t offset)
{
	LOG("%s: Communication Bus status read: %08x\n", machine().describe_context(), m_commctl);
	return m_commctl;
}

void nuon_mpe_device::commctl_w(offs_t offset, u32 data)
{
	LOG("%s: Communication Bus control write: %08x\n", machine().describe_context(), data);
	m_commctl = data;
}



//-------------------------------------------------
//  commxmit_r/w
//-------------------------------------------------

u32 nuon_mpe_device::commxmit_r(offs_t offset)
{
	LOG("%s: Communication Bus transmit packet word %d read: %08x\n", machine().describe_context(), offset, m_commxmit[offset]);
	return m_commxmit[offset];
}

void nuon_mpe_device::commxmit_w(offs_t offset, u32 data)
{
	//LOG("%s: Communication Bus transmit packet word %d write: %08x\n", machine().describe_context(), offset, data);
	m_commxmit[offset] = data;

	const u8 target_id = m_commctl & 0x7f;
	const u8 source_id = (m_configa >> 8) & 3;
	m_commbus_out((offset << 14) | (source_id << 7) | target_id, data);
}



//-------------------------------------------------
//  configa_r
//-------------------------------------------------

u32 nuon_mpe_device::configa_r(offs_t offset)
{
	// Bits 31-24: NUON release
	// Bits 23-16: MPE release
	//		$01: Oz / Aries 1
	//		$03: Aries 2
	//		$04: Aries 3
	// Bits 15-8: MPE Identifier
	//		$00: MPE 0
	//		$01: MPE 1
	//		$02: MPE 2
	//		$03: MPE 3
	//		$99: Reserved
	LOG("%s: Configuration A read: %08x\n", machine().describe_context(), m_configa);
	return m_configa;
}



//-------------------------------------------------
//  dcachectl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::dcachectl_r(offs_t offset)
{
	LOG("%s: Data Cache Control read: %08x\n", machine().describe_context(), m_dcachectl);
	return m_dcachectl;
}

void nuon_mpe_device::dcachectl_w(offs_t offset, u32 data)
{
	LOG("%s: Data Cache Control write: %08x\n", machine().describe_context(), data);
	LOG("%s:     Cache block size: %d bytes\n", machine().describe_context(), 16 << (data & 3));
	LOG("%s:     Cache way size: %d bytes\n", machine().describe_context(), 1024 << ((data >> 4) & 3));
	LOG("%s:     Cache way count: %d\n", machine().describe_context(), ((data >> 8) & 7) + 1);
	LOG("%s:     Stall on write miss: %d\n", machine().describe_context(), ((data >> 28) & 7) ? 0 : 1);
	m_dcachectl = data;
}



//-------------------------------------------------
//  icachectl_r/w
//-------------------------------------------------

u32 nuon_mpe_device::icachectl_r(offs_t offset)
{
	LOG("%s: Instruction Cache Control read: %08x\n", machine().describe_context(), m_icachectl);
	return m_icachectl;
}

void nuon_mpe_device::icachectl_w(offs_t offset, u32 data)
{
	LOG("%s: Instruction Cache Control write: %08x\n", machine().describe_context(), data);
	LOG("%s:     Cache block size: %d bytes\n", machine().describe_context(), 16 << (data & 3));
	LOG("%s:     Cache way size: %d bytes\n", machine().describe_context(), 1024 << ((data >> 4) & 3));
	LOG("%s:     Cache way count: %d\n", machine().describe_context(), ((data >> 8) & 7) + 1);
	m_icachectl = data;
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

		if ((opc1 & 0xfc00) == 0x0000)
		{	// add rm, rn
			// 0000 00mm mmmn nnnn
			const u64 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 n = opc1 & 0x1f;
			const u64 rn = m_regs.r[n];
			const u64 sum = rn + rm;
			m_regs_new.r[n] = (u32)sum;
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

			if ((s32)sum < (s32)rm)
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x0400)
		{	// add #v/5u, rn
			u64 v = (opc1 & 0x03e0) >> 5;
			u64 src = m_regs.r[opc1 & 0x1f];
			u64 sum = src + v;
			m_regs_new.r[opc1 & 0x1f] = (u32)sum;
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

		if ((opc1 & 0xfc00) == 0x0800)
		{	// copy rm, rn
			// 0000 10mm mmmn nnnn
			const u32 val = m_regs.r[(opc1 & 0x03e0) >> 5];
			m_regs_new.r[opc1 & 0x001f] = val;

			if (val != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc63) == 0x0c01)
		{	// sub_sv vm, vn
			// 0000 11mm m00n nn01
			u32 m = (opc1 & 0x0380) >> 5;
			u32 n = opc1 & 0x001c;
			for (int i = 0; i < 4; i++)
			{
				const u16 vn = m_regs.r[n + i] >> 16;
				const u16 vm = m_regs.r[m + i] >> 16;
				m_regs_new.r[n + i] = (vn - vm) << 16;
			}
			return;
		}

		// 0000 11nn nnn0 0010                                                                    [alu] neg rn
		if ((opc1 & 0xfc1f) == 0x0c02)
		{	// neg rn
			// 0000 11nn nnn0 0010
			const u32 n = (opc1 & 0x03e0) >> 5;
			u64 rn = m_regs.r[n];
			u64 result = 0 - rn;
			m_regs_new.r[n] = (u32)result;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (0 != BIT(rn, 31) && BIT(rn, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x1000)
		{	// sub rm, rn
			u64 rn = m_regs.r[opc1 & 0x001f];
			u64 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			u64 result = rn - rm;
			m_regs_new.r[opc1 & 0x1f] = (u32)result;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rn, 31) != BIT(rm, 31) && BIT(rm, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x1400)
		{	// sub #v/5u, rn
			u64 rn = m_regs.r[opc1 & 0x001f];
			u64 v = (opc1 & 0x03e0) >> 5;
			u64 result = rn - v;
			m_regs_new.r[opc1 & 0x1f] = (u32)result;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rn, 31) != BIT(v, 31) && BIT(v, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x1800)
		{	// eor #v/5s, rn
			// 0001 10vv vvvn nnnn
			const u32 n = opc1 & 0x001f;
			const u32 rn = m_regs.r[n];
			u32 v = (opc1 & 0x03e0) >> 5;
			if (BIT(v, 4))
				v |= 0xfffffff0;
			const u32 res = rn ^ v;
			m_regs_new.r[n] = res;

			if (!res)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(res, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x1c00)
		{	// cmp rm, rn
			// 0001 11mm mmmn nnnn
			u64 rn = m_regs.r[opc1 & 0x001f];
			u64 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			u64 result = rn - rm;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rn, 31) != BIT(rm, 31) && BIT(rm, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x2000)
		{	// cmp #v/5u, rn
			u64 rn = m_regs.r[opc1 & 0x001f];
			u64 v = (opc1 & 0x03e0) >> 5;
			u64 result = rn - v;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rn, 31) != BIT(v, 31) && BIT(v, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x2400)
		{	// and rm, rn
			// 0010 01mm mmmn nnnn
			const u32 n = opc1 & 0x001f;
			const u32 rn = m_regs.r[n];
			const u32 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 res = rn & rm;
			m_regs_new.r[n] = res;

			if (!res)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(res, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x2c00)
		{	// eor rm, rn
			// 0001 10mm mmmn nnnn
			const u32 n = opc1 & 0x001f;
			const u32 rn = m_regs.r[n];
			const u32 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 res = rn ^ rm;
			m_regs_new.r[n] = res;

			if (!res)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(res, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x3000)
		{	// asl #(32 - (d/5u) & 31, rn
			// 0011 00nn nnnd dddd
			const u32 d = opc1 & 0x001f;
			const u32 n = (opc1 & 0x03e0) >> 5;
			const u32 rn = m_regs.r[n];
			const u32 val = rn << d;
			m_regs_new.r[n] = val;
			if (val)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (BIT(rn, 31))
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x3400)
		{	// asr #d/5u, rn
			// 0011 01nn nnnd dddd
			const u32 d = opc1 & 0x001f;
			const u32 n = (opc1 & 0x03e0) >> 5;
			const s32 rn = (s32)m_regs.r[n];
			const s32 val = rn >> d;
			m_regs_new.r[n] = val;
			if (val)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (BIT(rn, 0))
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x3800)
		{	// lsr #d/5u, rn
			// 0011 01nn nnnd dddd
			const u32 d = opc1 & 0x001f;
			const u32 n = (opc1 & 0x03e0) >> 5;
			const u32 rn = (s32)m_regs.r[n];
			const u32 val = rn >> d;
			m_regs_new.r[n] = val;
			if (val)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (BIT(rn, 0))
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x3c00)
		{	// btst #d/5u, rn
			const u32 mask_bit = opc1 & 0x001f;
			const u32 mask = 1 << mask_bit;
			const u32 val = m_regs.r[(opc1 & 0x03e0) >> 5];
			if (val & mask)
			{
				m_cc &= ~CC_Z;
				if (mask_bit == 31)
				{
					m_cc |= CC_N;
				}
			}
			else
			{
				m_cc |= CC_Z;
				if (mask_bit == 31)
				{
					m_cc &= ~CC_N;
				}
			}
			m_cc &= ~CC_V;
			return;
		}

		unimplemented_opcode(m_pc - 2, &opc1);
		return;
	}

	if ((opc1 & 0x4000) == 0x4000)
	{
		// 16-bit normal non-ALU
		cont = !(opc1 & 0x8000);
		m_pc += 2;

		// 16-bit mem
		if ((opc1 & 0x7c00) == 0x4800)
		{	// ld_s (rm), rn
			m_regs_new.r[opc1 & 0x001f] = m_program->read_dword(m_regs.r[(opc1 & 0x03e0) >> 5]);
			return;
		}

		if ((opc1 & 0x7c00) == 0x5000)
		{	// ld_s 20500000 + m/5u << 4, rn
			// f101 00mm mmmn nnnn
			m_regs_new.r[opc1 & 0x001f] = m_program->read_dword(0x20500000 + ((opc1 & 0x03e0) >> 1));
			return;
		}

		if ((opc1 & 0x7c00) == 0x4c00)
		{	// st_s rn, (rm)
			m_program->write_dword(m_regs.r[(opc1 & 0x03e0) >> 5], m_regs.r[opc1 & 0x001f]);
			return;
		}

		if ((opc1 & 0x7c00) == 0x5400)
		{	// st_s rn, 20500000 + m/5u << 4
			// f101 01mm mmmn nnnn
			m_program->write_dword(0x20500000 | ((opc1 & 0x03e0) >> 1), m_regs.r[opc1 & 0x001f]);
			return;
		}

		if ((opc1 & 0x7c00) == 0x5800)
		{	// mv_s rn, rm
			m_regs_new.r[(opc1 & 0x03e0) >> 5] = m_regs.r[opc1 & 0x001f];
			return;
		}

		if ((opc1 & 0x7c00) == 0x5c00)
		{	// mv_s #v/5u, rm
			m_regs_new.r[(opc1 & 0x03e0) >> 5] = opc1 & 0x001f;
			return;
		}

		if ((opc1 & 0x7fe3) == 0x6020)
		{	// push vn
			// f110 0000 001n nn00
			const u32 n = opc1 & 0x001f;
			m_sp -= 0x10;
			m_program->write_dword(m_sp,      m_regs.r[n]);
			m_program->write_dword(m_sp + 4,  m_regs.r[n + 1]);
			m_program->write_dword(m_sp + 8,  m_regs.r[n + 2]);
			m_program->write_dword(m_sp + 12, m_regs.r[n + 3]);
			return;
		}

		if ((opc1 & 0x7fe3) == 0x6120)
		{	// push vn, rz
			// f110 0001 001n nn00
			const u32 n = opc1 & 0x001f;
			m_sp -= 0x10;
			m_program->write_dword(m_sp,      m_regs.r[n]);
			m_program->write_dword(m_sp + 4,  m_regs.r[n + 1]);
			m_program->write_dword(m_sp + 8,  m_regs.r[n + 2]);
			m_program->write_dword(m_sp + 12, m_rz);
			return;
		}

		if ((opc1 & 0x7fe3) == 0x6040)
		{	// pop vn
			// f110 0000 010n nn00
			const u32 n = opc1 & 0x001f;
			m_regs_new.r[n    ] = m_program->read_dword(m_sp);
			m_regs_new.r[n + 1] = m_program->read_dword(m_sp + 4);
			m_regs_new.r[n + 2] = m_program->read_dword(m_sp + 8);
			m_regs_new.r[n + 3] = m_program->read_dword(m_sp + 12);
			m_sp += 0x10;
			return;
		}

		if ((opc1 & 0x7fe3) == 0x6140)
		{	// pop vn, rz
			// f110 0001 010n nn00
			const u32 n = opc1 & 0x001f;
			m_regs_new.r[n    ] = m_program->read_dword(m_sp);
			m_regs_new.r[n + 1] = m_program->read_dword(m_sp + 4);
			m_regs_new.r[n + 2] = m_program->read_dword(m_sp + 8);
			m_rz                = m_program->read_dword(m_sp + 12);
			m_sp += 0x10;
			return;
		}

		if ((opc1 & 0x7c00) == 0x4000)
		{	// mul rm, rn, >>acshift, rn
			// f100 00nn nnnm mmmm
			const u32 n = (opc1 & 0x03e0) >> 5;
			const s32 rm = (s32)m_regs.r[opc1 & 0x001f];
			const s32 rn = (s32)m_regs.r[n];
			u64 product = (u64)mul_32x32(rm, rn);
			if (BIT(m_acshift, 6))
				product <<= 0x20 - (m_acshift & 0x1f);
			else
				product >>= (m_acshift & 0x3f);
			m_regs_new.r[n] = (u32)product;

			const u32 msw = (u32)(product >> 32);
			if (msw != 0 && msw != 0xffffffff)
				m_cc |= CC_MV;
			else
				m_cc &= ~CC_MV;

			return;
		}

		// 16-bit ECU
		if ((opc1 & 0x7c00) == 0x6800)
		{	// bra cc1, pc + r/7s << 1
			// f110 10cc crrr rrrr
			// 0110 1010 0111 1010

			s32 pc_offset = (opc1 & 0x007f) << 1;
			if (pc_offset & (1 << 7))
				pc_offset |= 0xffffff00;

			switch ((opc1 & 0x0380) >> 5)
			{
			case 0x00: // non-equal/non-zero
				if ((m_cc & CC_Z) && !m_branch_delay)
					return;
				m_branch_pc = (m_pc - 2) + pc_offset;
				m_branch_delay = 3;
				return;
			case 0x04: // equal/zero
				if ((m_cc & CC_Z) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x08: // less than: (N && !V) || (!N && V)
				if (BIT(m_cc, CC_N_BIT) != BIT(m_cc, CC_V_BIT) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x0c: // less than or equal: Z || (N && !V) || (!N && V)
				if (((m_cc & CC_Z) || BIT(m_cc, CC_N_BIT) != BIT(m_cc, CC_V_BIT)) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x10: // greater: (N && V && !Z) || (!N && !V && !Z)
			{
				const bool n = m_cc & CC_N;
				const bool v = m_cc & CC_V;
				const bool z = m_cc & CC_Z;
				if (!z && n == v && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
			}	return;
			case 0x14: // greater or equal: (N && V) || (!N && !V)
				if (BIT(m_cc, CC_N_BIT) == BIT(m_cc, CC_V_BIT) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x18: // counter 0 non-zero
				if (m_rc0 && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 2) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 2, &opc1);
				return;
			}
		}

		if ((opc1 & 0x7c00) == 0x6c00)
		{	// bra pc + r/10s<< 1
			// f110 11rr rrrr rrrr
			if (!m_branch_delay)
			{
				s32 pc_offset = (opc1 & 0x03ff) << 1;
				if (pc_offset & (1 << 10))
					pc_offset |= 0xfffff800;
				m_branch_pc = (m_pc - 2) + pc_offset;
				m_branch_delay = 3;
			}
			return;
		}

		if ((opc1 & 0x7fff) == 0x7001)
		{	// halt
			// f111 0000 0000 0001
			m_mpectl &= ~(1 << 1);
			suspend(SUSPEND_REASON_HALT, true);
			m_excepsrc |= (1 << 0);
			return;
		}

		if ((opc1 & 0x7c1f) == 0x7010)
		{	// rts cc
			switch ((opc1 & 0x03e0) >> 5)
			{
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_branch_pc = m_rz;
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 2, &opc1);
				return;
			}
		}

		if ((opc1 & 0x7c1f) == 0x7011)
		{	// rts cc, nop
			// f111 00cc ccc1 0001
			switch ((opc1 & 0x03e0) >> 5)
			{
			case 0x11: // unconditional
				m_branch_pc = m_rz;
				m_branch_delay = 1;
				return;
			default:
				unimplemented_opcode(m_pc - 2, &opc1);
				return;
			}
		}

		// 16-bit RCU
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
		cont = false;//((opc1 & 0x0100) == 0x0000);
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
		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xee00) == 0xa000)
		{	// bra cc, pc + wv/14s << 1
			// 1001 00cc cccv vvvv . 101f 000w wwww wwww
			s32 pc_offset = ((opc2 & 0x1ff) << 6) | ((opc1 & 0x1f) << 1);
			if (pc_offset & (1 << 14))
				pc_offset |= 0xffff8000;

			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x00: // non-equal/non-zero: !Z
				if (!(m_cc & CC_Z) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x04: // equal/zero: Z
				if ((m_cc & CC_Z) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x05: // carry set: c
				if (m_cc & CC_C)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x0b: // higher: !C && !Z
				if ((m_cc & (CC_C | CC_Z)) == 0 && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x0d: // lower or same: C || Z
				if ((m_cc & CC_C) || (m_cc & CC_Z))
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			case 0x10: // greater: (N && V && !Z) || (!N && !V && !Z)
			{
				const bool n = m_cc & CC_N;
				const bool v = m_cc & CC_V;
				const bool z = m_cc & CC_Z;
				if (!z && n == v && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
			}	return;
			case 0x14: // greater or equal: (N && V) || (!N && !V)
				if (BIT(m_cc, CC_N_BIT) == BIT(m_cc, CC_V_BIT) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xef80) == 0xa300)
		{	// jmp cc, 20300000 + wv/12u << 1
			const u32 wv = ((opc2 & 0x007f) << 5) | (opc1 & 0x001f);
			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_branch_pc = 0x20300000 | (wv << 1);
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xef80) == 0xa400)
		{	// jsr cc, 20200000 + wv/12u << 1
			// 1001 00cc cccv vvvv . 101f 0100 0www wwww
			const u32 wv = ((opc2 & 0x007f) << 5) | (opc1 & 0x001f);
			switch ((opc1 & 0x03e0) >> 5)
			{
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_branch_pc = 0x20200000 | (wv << 1) | (1ULL << 32);
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xef80) == 0xa500)
		{	// jsr cc, 20300000 + wv/12u << 1
			// 1001 00cc cccv vvvv . 101f 0101 0www wwww
			const u32 wv = ((opc2 & 0x007f) << 5) | (opc1 & 0x001f);
			switch ((opc1 & 0x03e0) >> 5)
			{
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_branch_pc = 0x20300000 | (wv << 1) | (1ULL << 32);
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xefff) == 0xa700)
		{	// jsr cc, (rn)
			// 1001 00cc cccn nnnn . 101f 0111 0000 0000
			const u32 rn = m_regs.r[opc1 & 0x001f];
			switch ((opc1 & 0x03e0) >> 5)
			{
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_branch_pc = rn | (1ULL << 32);
					m_branch_delay = 3;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xee00) == 0xa800)
		{	// bra cc, pc + wv/14s << 1, nop
			// 1001 00cc cccv vvvv . 101f 100w wwww wwww
			s32 pc_offset = ((opc2 & 0x1ff) << 6) | ((opc1 & 0x1f) << 1);
			if (pc_offset & (1 << 14))
				pc_offset |= 0xffff8000;

			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x00: // non-equal/non-zero
				if (!(m_cc & CC_Z) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x03: // carry clear
				if (!(m_cc & CC_C) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x04: // equal/zero
				if ((m_cc & CC_Z) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x08: // less than: (N && !V) || (!N && V)
				if (BIT(m_cc, CC_N_BIT) != BIT(m_cc, CC_V_BIT) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x0b: // higher: !C && !Z
				if ((m_cc & (CC_C | CC_Z)) == 0 && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x0c: // less than or equal: Z || (N && !V) || (!N && V)
			{
				const bool n = m_cc & CC_N;
				const bool v = m_cc & CC_V;
				if (((m_cc & CC_Z) || (n && !v) || (!n && v)) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
			}	return;
			case 0x0d: // lower or same: C || Z
				if (((m_cc & CC_C) || (m_cc & CC_Z)) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x10: // greater: (N && V && !Z) || (!N && !V && !Z)
			{
				const bool n = m_cc & CC_N;
				const bool v = m_cc & CC_V;
				const bool z = m_cc & CC_Z;
				if (!z && n == v && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
			}	return;
			case 0x14: // greater or equal: (N && V) || (!N && !V)
				if (BIT(m_cc, CC_N_BIT) == BIT(m_cc, CC_V_BIT) && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			case 0x18: // counter 0 non-zero
				if (m_rc0 && !m_branch_delay)
				{
					m_branch_pc = (m_pc - 4) + pc_offset;
					m_branch_delay = 1;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xef80) == 0xad00)
		{	// jsr cc, 20300000 + wv/12u << 1, nop
			const u32 wv = ((opc2 & 0x007f) << 5) | (opc1 & 0x001f);
			switch ((opc1 & 0x03e0) >> 5)
			{
			case 0x11: // unconditional
				m_branch_pc = 0x20300000 + (wv << 1) + (1ULL << 32);
				m_branch_delay = 1;
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xefff) == 0xae80)
		{	// jmp cc, (rn), nop
			const u32 rn = m_regs.r[opc1 & 0x1f];
			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x11: // unconditional
				m_branch_pc = rn;
				m_branch_delay = 1;
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		if ((opc1 & 0xfc00) == 0x9000 && (opc2 & 0xefff) == 0xaf00)
		{	// jsr cc, (rn)
			// 1001 00cc cccn nnnn . 101f 0111 0000 0000
			const u32 rn = m_regs.r[opc1 & 0x1f];
			switch ((opc1 >> 5) & 0x1f)
			{
			case 0x11: // unconditional
				if (!m_branch_delay)
				{
					m_rz = m_pc;
					m_pc = rn;
				}
				return;
			default:
				unimplemented_opcode(m_pc - 4, &opc1, &opc2);
				return;
			}
		}

		// 32-bit mem instructions
		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xefc0) == 0xa280)
		{	// ld_s 20500000 + om/11u << 2, rn
			const u16 om = ((opc2 & 0x003f) << 5) | ((opc1 & 0x03e0) >> 5);
			m_regs_new.r[opc1 & 0x001f] = m_program->read_dword(0x20500000 + (om << 2));
			return;
		}

		if ((opc1 & 0xfc03) == 0x9401 && (opc2 & 0xefc0) == 0xa380)
		{	// ld_v 20500000 + om/11u << 4, vn
			// 1001 01mm mmmn nn01 . 101f 0011 10oo oooo
			const u32 m = 0x20500000 + ((opc2 & 0x003f) << 9) + ((opc1 & 0x03e0) >> 1);
			const u32 n = opc1 & 0x001c;
			m_regs_new.r[n    ] = m_program->read_dword(m);
			m_regs_new.r[n + 1] = m_program->read_dword(m + 4);
			m_regs_new.r[n + 2] = m_program->read_dword(m + 8);
			m_regs_new.r[n + 3] = m_program->read_dword(m + 12);
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xefff) == 0xa400)
		{	// ld_b (rm), rn
			// 1001 01mm mmmn nnnn . 101f 0100 0000 0000
			m_regs_new.r[opc1 & 0x001f] = m_program->read_byte(m_regs.r[(opc1 & 0x03e0) >> 5]) << 24;
			return;
		}

		if ((opc1 & 0xfc03) == 0x9400 && (opc2 & 0xefff) == 0xa410)
		{	// ld_v (rm), vn
			// 1001 01mm mmmn nn00 . 101f 0100 0001 0000
			const u32 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 vn = opc1 & 0x001c;
			m_regs_new.r[vn]     = m_program->read_dword(rm);
			m_regs_new.r[vn + 1] = m_program->read_dword(rm + 4);
			m_regs_new.r[vn + 2] = m_program->read_dword(rm + 8);
			m_regs_new.r[vn + 3] = m_program->read_dword(rm + 12);
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xef80) == 0xa600)
		{	// mv_s #wv/12s, rn
			s32 val = ((opc2 & 0x007f) << 5) | (opc1 & 0x001f);
			if (val & (1 << 11))
				val |= 0xfffff000;

			m_regs_new.r[(opc1 & 0x03e0) >> 5] = val;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xefc0) == 0xaa40)
		{	// st_s rn, 20100000 + om/11u << 2
			const u16 om = ((opc2 & 0x003f) << 7) | ((opc1 & 0x03e0) >> 3);
			m_program->write_dword(0x20100000 + om, m_regs.r[opc1 & 0x1f]);
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xefc0) == 0xaa80)
		{	// st_s rn, 20500000 + om/11u << 2
			const u16 om = ((opc2 & 0x003f) << 7) | ((opc1 & 0x03e0) >> 3);
			m_program->write_dword(0x20500000 + om, m_regs.r[opc1 & 0x1f]);
			return;
		}

		if ((opc1 & 0xfc03) == 0x9401 && (opc2 & 0xefc0) == 0xab40)
		{	// st_v vn, 20100000 + om/11u << 4
			// 1001 01mm mmmn nn01 . 101f 1011 01oo oooo
			const u32 om = 0x20100000 + ((opc2 & 0x003f) << 9) + ((opc1 & 0x03e0) >> 1);
			const u32 vn = opc1 & 0x1c;
			m_program->write_dword(om,      m_regs.r[vn]);
			m_program->write_dword(om + 4,  m_regs.r[vn + 1]);
			m_program->write_dword(om + 8,  m_regs.r[vn + 2]);
			m_program->write_dword(om + 12, m_regs.r[vn + 3]);
			return;
		}

		if ((opc1 & 0xfc03) == 0x9401 && (opc2 & 0xefc0) == 0xab80)
		{	// st_v vn, 20500000 + om/11u << 4
			const u32 om = 0x20500000 + ((opc2 & 0x003f) << 9) + ((opc1 & 0x03e0) >> 1);
			const u32 vn = opc1 & 0x1c;
			m_program->write_dword(om,      m_regs.r[vn]);
			m_program->write_dword(om + 4,  m_regs.r[vn + 1]);
			m_program->write_dword(om + 8,  m_regs.r[vn + 2]);
			m_program->write_dword(om + 12, m_regs.r[vn + 3]);
			return;
		}

		if ((opc1 & 0xfc03) == 0x9400 && (opc2 & 0xefff) == 0xac10)
		{	// st_v vm, (rn)
			const u32 rn = m_regs.r[(opc1 >> 5) & 0x1f] & 0xfffffff0;
			const u32 vm = opc1 & 0x1c;
			m_program->write_dword(rn,      m_regs.r[vm]);
			m_program->write_dword(rn + 4,  m_regs.r[vm + 1]);
			m_program->write_dword(rn + 8,  m_regs.r[vm + 2]);
			m_program->write_dword(rn + 12, m_regs.r[vm + 3]);
			return;
		}

		if ((opc1 & 0xfc00) == 0x9400 && (opc2 & 0xee00) == 0xae00)
		{	// st_s #wv/10u, 20500000 + om/9 << 4
			// 1001 01mm mmmv vvvv . 101f 111o ooow wwww
			const u16 om = (opc2 & 0x01e0) | ((opc1 & 0x03e0) >> 5);
			const u16 wv = ((opc2 & 0x001f) << 5) | (opc1 & 0x001f);
			m_program->write_dword(0x20500000 + (om << 4), wv);
			return;
		}

		// 32-bit ALU instructions
		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa000)
		{	// add ro, rm, rn
			// 1001 10oo ooom mmmm . 101f 0000 000n nnnn
			u64 ro = m_regs.r[(opc1 & 0x03e0) >> 5];
			u64 rm = m_regs.r[opc1 & 0x1f];
			u64 sum = rm + ro;
			m_regs_new.r[opc2 & 0x1f] = (u32)sum;
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

			if ((s32)sum < (s32)rm)
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa020)
		{	// add #v/5u, rm, rn
			// 1001 10vv vvvm mmmm . 101f 0000 001n nnnn
			u64 v = (opc1 & 0x03e0) >> 5;
			u64 src = m_regs.r[opc1 & 0x1f];
			u64 sum = src + v;
			m_regs_new.r[opc2 & 0x1f] = (u32)sum;
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

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa040)
		{	// add #wv/10u, rn
			// 1001 10vv vvvn nnnn . 101f 0000 010w wwww
			u64 wv = ((opc2 & 0x1f) << 5) | ((opc1 & 0x03e0) >> 5);
			u64 src = m_regs.r[opc1 & 0x1f];
			u64 sum = src + wv;
			m_regs_new.r[opc1 & 0x1f] = (u32)sum;
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

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa0a0)
		{	// add rm, >>#d/5s, rn
			// 1001 10mm mmmn nnnn . 101f 0000 101d dddd
			u64 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			if (BIT(opc2, 4))
				rm <<= 0x10 - (opc2 & 0xf);
			else
				rm >>= opc2 & 0xf;
			const u64 src = m_regs.r[opc1 & 0x1f];
			const u64 sum = src + rm;
			m_regs_new.r[opc1 & 0x1f] = (u32)sum;
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

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa100)
		{	// sub ro, rm, rn
			// 1001 10oo ooom mmmm . 101f 0001 000n nnnn
			const u64 ro = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u64 rm = m_regs.r[opc1 & 0x001f];
			const u64 result = rm - ro;
			m_regs_new.r[opc2 & 0x1f] = (u32)result;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rm, 31) != BIT(ro, 31) && BIT(ro, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa120)
		{	// sub #v/5u, rm, rn
			// 1001 10vv vvvm mmmm . 101f 0001 001n nnnn
			const u64 v = (opc1 & 0x03e0) >> 5;
			const u64 rm = m_regs.r[opc1 & 0x001f];
			const u64 result = rm - v;
			m_regs_new.r[opc2 & 0x001f] = (u32)result;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rm, 31) != BIT(v, 31) && BIT(v, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa140)
		{	// sub #wv/10u, rn
			// 1001 10vv vvvn nnnn . 101f 0001 010w wwww
			const u64 wv = ((opc2 & 0x001f) << 5) | ((opc1 & 0x03e0) >> 5);
			const u64 rn = m_regs.r[opc1 & 0x001f];
			const u64 result = rn - wv;
			m_regs_new.r[opc1 & 0x001f] = (u32)result;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rn, 31) != BIT(wv, 31) && BIT(wv, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa240)
		{	// cmp #wv/10u, rn
			// 1001 10vv vvvn nnnn . 101f 0010 010w wwww
			const u64 wv = ((opc2 & 0x001f) << 5) | ((opc1 & 0x03e0) >> 5);
			const u64 rn = m_regs.r[opc1 & 0x001f];
			const u64 result = rn - wv;
			if (result != 0)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (result & 0xffffffff00000000ULL)
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			if (BIT(rn, 31) != BIT(wv, 31) && BIT(wv, 31) == BIT(result, 31))
				m_cc |= CC_V;
			else
				m_cc &= ~CC_V;

			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa320)
		{	// and #v/5s, rm, rn
			// 1001 10vv vvvm mmmm . 101f 0011 001n nnnn
			u32 v = ((opc1 & 0x03e0) >> 5);
			if (v & (1 << 4))
				v |= 0xfffffff0;
			const u32 rm = m_regs.r[opc1 & 0x001f];
			const u32 res = rm & v;
			m_regs_new.r[opc2 & 0x001f] = res;

			if (!res)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(res, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa340)
		{	// and #v/5s, <>#d/5s, rn
			// 1001 10vv vvvn nnnn . 101f 0011 010d dddd
			const u32 d = opc2 & 0x001f;
			const u32 n = opc1 & 0x001f;
			u32 v = (opc1 & 0x03e0) >> 5;
			if (BIT(v, 4))
				v |= 0xfffffff0;
			v = (v >> d) | (v << (32 - d));
			v &= m_regs.r[n];
			m_regs_new.r[n] = v;

			if (!v)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(v, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa3c0)
		{	// and ro, <>rm, rn
			// 1001 10oo ooom mmmm . 101f 0011 110n nnnn
			const u32 rm = m_regs.r[opc1 & 0x001f] & 0x1f;
			const u32 n = opc2 & 0x001f;
			u32 ro = m_regs.r[(opc1 & 0x03e0) >> 5];
			ro = (ro >> rm) | (ro << (32 - rm));
			ro &= m_regs.r[n];
			m_regs_new.r[n] = ro;

			if (!ro)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(ro, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa520)
		{	// or #v/5s, rm, rn
			// 1001 10vv vvvm mmmm . 101f 0101 001n nnnn
			u32 v = (opc1 & 0x03e0) >> 5;
			if (BIT(v, 4))
				v |= 0xfffffff0;
			v |= m_regs.r[opc1 & 0x001f];
			m_regs_new.r[opc2 & 0x001f] = v;

			if (!v)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(v, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa540)
		{	// or #v/5s, <>#d/5s, rn
			const u32 d = opc2 & 0x001f;
			const u32 n = opc1 & 0x001f;
			u32 v = (opc1 & 0x03e0) >> 5;
			if (BIT(v, 4))
				v |= 0xfffffff0;
			if (d)
				v = (v >> d) | (v << (32 - d));
			v |= m_regs.r[n];
			m_regs_new.r[n] = v;

			if (!v)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(v, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa580)
		{	// or ro, >>#d/5s, rn
			// 1001 10oo ooon nnnn . 101f 0101 100d dddd
			const u32 d = opc2 & 0x001f;
			const u32 ro = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 rn = opc1 & 0x001f;
			u32 val;
			if (BIT(opc2, 4))
				val = ro << (0x10 - (d & 0x0f));
			else
				val = ro >> d;
			m_regs_new.r[rn] = val;

			if (!val)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa5c0)
		{	// or ro, <>rm, rn
			// 1001 10oo ooom mmmm . 101f 0101 110n nnnn
			const u32 rm = m_regs.r[opc1 & 0x001f] & 0x1f;
			const u32 n = opc2 & 0x001f;
			u32 ro = m_regs.r[(opc1 & 0x03e0) >> 5];
			ro = (ro >> rm) | (ro << (32 - rm));
			ro |= m_regs.r[n];
			m_regs_new.r[n] = ro;

			if (!ro)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(ro, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa720)
		{	// asl #(32 - (d/5u) & 31, rm, rn
			// 1001 10mm mmmn nnnn . 101f 0111 001d dddd
			const u32 d = opc2 & 0x1f;
			const u32 val = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 result = val << ((32 - d) & 0x1f);
			m_regs_new.r[opc1 & 0x001f] = result;
			if (result)
				m_cc &= ~CC_Z;
			else
				m_cc |= CC_Z;

			if (BIT(result, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (BIT(val, 31))
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa760)
		{	// ls >>ro, rm, rn
			// 1001 10mm mmmo oooo . 101f 0111 011n nnnn
			const u32 ro = m_regs.r[opc1 & 0x001f];
			const u32 d = ro & 0x1f;
			u32 val = m_regs.r[(opc1 & 0x03e0) >> 5];
			if (d)
			{
				if (BIT(ro, 5))
				{
					if (BIT(val, 31))
						m_cc |= CC_C;
					else
						m_cc &= ~CC_C;
					val <<= 0x20 - d;
				}
				else
				{
					if (BIT(val, 0))
						m_cc |= CC_C;
					else
						m_cc &= ~CC_C;
					val >>= d;
				}
				m_regs_new.r[opc2 & 0x001f] = val;
			}

			if (!val)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa780)
		{	// lsr #d/5u, rm, rn
			// 1001 10mm mmmn nnnn . 101f 0111 100d dddd
			const u32 d = opc2 & 0x001f;
			const u32 rm = m_regs.r[(opc1 & 0x03e0) >> 5];
			const u32 val = rm >> d;
			m_regs_new.r[opc1 & 0x001f] = val;

			if (!val)
				m_cc |= CC_Z;
			else
				m_cc &= ~CC_Z;

			if (BIT(val, 31))
				m_cc |= CC_N;
			else
				m_cc &= ~CC_N;

			if (BIT(rm, 0))
				m_cc |= CC_C;
			else
				m_cc &= ~CC_C;

			m_cc &= ~CC_V;
			return;
		}

		if ((opc1 & 0xfc00) == 0x9800 && (opc2 & 0xefe0) == 0xa820)
		{	// bits #v/5u, >>#d/5u, rn
			const u16 rn = (opc1 & 0x03e0) >> 5;
			const u16 v = opc2 & 0x1f;

			u32 val = m_regs.r[rn] >> (opc1 & 0x1f);
			val &= ~(0xffffffff << (v + 1));
			m_regs_new.r[rn] = val;

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

		// 32-bit MUL instructions
		if ((opc1 & 0xfc00) == 0x9c00 && (opc2 & 0xefe0) == 0xa000)
		{	// mul ro, rm, >>acshift, rn
			// 1001 11mm mmmo oooo . 101f 0000 000n nnnn
			const s32 ro = (s32)m_regs.r[opc1 & 0x001f];
			const s32 rm = (s32)m_regs.r[(opc1 & 0x03e0) >> 5];
			u64 product = (u64)mul_32x32(ro, rm);
			if (BIT(m_acshift, 6))
				product <<= 0x20 - (m_acshift & 0x1f);
			else
				product >>= (m_acshift & 0x3f);
			m_regs_new.r[opc2 & 0x001f] = (u32)product;

			const u32 msw = (u32)(product >> 32);
			if (msw != 0 && msw != 0xffffffff)
				m_cc |= CC_MV;
			else
				m_cc &= ~CC_MV;

			return;
		}

		// 1001 11nn nnnm mmmm . 101f 0000 1ddd dddd                                              [mul] mul rm, rn, >>#d/7s, rn
		if ((opc1 & 0xfc00) == 0x9c00 && (opc2 & 0xef80) == 0xa080)
		{	// mul rm, rn, >>#d/7s, rn
			// 1001 11nn nnnm mmmm . 101f 0000 1ddd dddd
			const u32 n = (opc1 & 0x03e0) >> 5;
			const s32 rm = (s32)m_regs.r[opc1 & 0x001f];
			const s32 rn = (s32)m_regs.r[n];
			const u32 d = opc2 & 0x3f;
			u64 product = (u64)mul_32x32(rm, rn);
			if (BIT(opc2, 6))
				product <<= 0x20 - (d & 0x1f);
			else
				product >>= d;
			m_regs_new.r[n] = (u32)product;

			const u32 msw = (u32)(product >> 32);
			if (msw != 0 && msw != 0xffffffff)
				m_cc |= CC_MV;
			else
				m_cc &= ~CC_MV;

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

			if ((opc3 & 0xfc00) == 0x0400)
			{	// add #xw/32s, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 0000 01ww wwwn nnnn
				const s32 xw = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u32 n = opc3 & 0x001f;
				const u64 src = m_regs.r[n];
				const u64 sum = src + xw;
				m_regs_new.r[n] = (u32)sum;
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

			if ((opc3 & 0xfc00) == 0x1400)
			{	// sub #xw/32s, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 0001 01ww wwwn nnnn
				// 1000 1000 0000 0000 . 0001 0000 0000 0011 . 0001 0110 0001 1111

				const s32 xw = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u32 n = opc3 & 0x001f;
				const u64 rn = m_regs.r[n];
				const u64 xw_ext = (u64)(u32)xw;
				const u64 result = rn - xw_ext;
				m_regs_new.r[n] = (u32)result;
				if (result != 0)
					m_cc &= ~CC_Z;
				else
					m_cc |= CC_Z;

				if (BIT(result, 31))
					m_cc |= CC_N;
				else
					m_cc &= ~CC_N;

				if (result & 0xffffffff00000000ULL)
					m_cc |= CC_C;
				else
					m_cc &= ~CC_C;

				if (BIT(rn, 31) != BIT(xw_ext, 31) && BIT(xw_ext, 31) == BIT(result, 31))
					m_cc |= CC_V;
				else
					m_cc &= ~CC_V;
				return;
			}

			if ((opc3 & 0xfc00) == 0x2000)
			{	// cmp #xw/32s, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 0010 00ww wwwn nnnn
				const s32 xw = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u64 rn = m_regs.r[opc3 & 0x001f];
				const u64 xw_ext = (u64)(u32)xw;
				const u64 result = rn - xw_ext;
				if (result != 0)
					m_cc &= ~CC_Z;
				else
					m_cc |= CC_Z;

				if (BIT(result, 31))
					m_cc |= CC_N;
				else
					m_cc &= ~CC_N;

				if (result & 0xffffffff00000000ULL)
					m_cc |= CC_C;
				else
					m_cc &= ~CC_C;

				if (BIT(rn, 31) != BIT(xw_ext, 31) && BIT(xw_ext, 31) == BIT(result, 31))
					m_cc |= CC_V;
				else
					m_cc &= ~CC_V;
				return;
			}

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
				m_regs_new.r[(opc3 >> 5) & 0x1f] = ((opc1 & 0x7ff) << 21) | (opc2 << 5) | (opc3 & 0x1f);
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

			// 32+32-bit ecu instructions
			if ((opc3 & 0xfc00) == 0x9000 && (opc4 & 0xef00) == 0xa200 && (opc2 & 0x01ff) == 0x0000)
			{	// jmp cc, #xwv/31u << 1
				// 1000 1xxx xxxx xxxx . xxxx xxx0 0000 0000 . 1001 00cc cccv vvvv . 101f 0010 wwww wwww
				const u32 xwv = ((opc1 & 0x07ff) << 21) | ((opc2 & 0xfe00) << 5) | ((opc4 & 0x00ff) << 6) | ((opc3 & 0x001f) << 1);
				switch ((opc3 & 0x03e0) >> 5)
				{
				case 0x11: // unconditional
					if (!m_branch_delay)
					{
						m_branch_pc = xwv;
						m_branch_delay = 3;
					}
					return;
				default:
					unimplemented_opcode(m_pc - 8, &opc1, &opc2, &opc3, &opc4);
					return;
				}
				return;
			}

			if ((opc3 & 0xfc00) == 0x9000 && (opc4 & 0xef00) == 0xa400 && (opc2 & 0x01ff) == 0x0000)
			{	// jsr cc, #xwv/31u << 1
				// 1000 1xxx xxxx xxxx . xxxx xxx0 0000 0000 . 1001 00cc cccv vvvv . 101f 0100 wwww wwww
				const u32 xwv = ((opc1 & 0x07ff) << 21) | ((opc2 & 0xfe00) << 5) | ((opc4 & 0x00ff) << 6) | ((opc3 & 0x001f) << 1);
				switch ((opc3 & 0x03e0) >> 5)
				{
				case 0x11: // unconditional
					if (!m_branch_delay)
					{
						m_branch_pc = xwv | (1ULL << 32);
						m_branch_delay = 3;
					}
					return;
				default:
					unimplemented_opcode(m_pc - 8, &opc1, &opc2, &opc3, &opc4);
					return;
				}
				return;
			}

			if ((opc3 & 0xfc00) == 0x9000 && (opc4 & 0xef00) == 0xac00 && (opc2 & 0x01ff) == 0x0000)
			{	// jsr cc, #xwv/31u << 1, nop
				// 1000 1xxx xxxx xxxx . xxxx xxx0 0000 0000 . 1001 00cc cccv vvvv . 101f 1100 wwww wwww
				const u32 xwv = ((opc1 & 0x07ff) << 21) | ((opc2 & 0xfe00) << 5) | ((opc4 & 0x00ff) << 6) | ((opc3 & 0x001f) << 1);
				switch ((opc3 & 0x03e0) >> 5)
				{
				case 0x11: // unconditional
					m_branch_pc = xwv + (1ULL << 32);
					m_branch_delay = 1;
					return;
				default:
					unimplemented_opcode(m_pc - 8, &opc1, &opc2, &opc3, &opc4);
					return;
				}
			}

			// 32+32-bit mem instructions
			if ((opc3 & 0xfc00) == 0x9400 && (opc4 & 0xee00) == 0xae00 && (opc2 & 0x0018) == 0x0010)
			{	// st_s #xwv/32u, 20500000 + qpom/12u << 2
				const u32 xwv = ((opc1 & 0x07ff) << 21) | ((opc2 & 0xffe0) << 5) | ((opc4 & 0x001f) << 5) | (opc3 & 0x001f);
				const u32 qpom = (BIT(opc2, 2) << 11) | ((opc4 & 0x01e0) << 2) | ((opc3 & 0x03e0) >> 3) | (opc2 & 0x0003);
				m_program->write_dword(0x20500000 + (qpom << 2), xwv);
				return;
			}

			// 32+32-bit alu instructions
			if ((opc3 & 0xfc00) == 0x9800 && (opc4 & 0xefe0) == 0xa020)
			{	// add #xw/32s, rm, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 1001 10ww wwwm mmmm . 101f 0000 001n nnnn
				const u64 v = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u64 src = m_regs.r[opc3 & 0x1f];
				const u64 sum = src + v;
				m_regs_new.r[opc4 & 0x1f] = (u32)sum;
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

			if ((opc3 & 0xfc00) == 0x9800 && (opc4 & 0xefe0) == 0xa120)
			{	// sub #xw/32s, rm, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 1001 10ww wwwm mmmm . 101f 0001 001n nnnn
				const u64 xw = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u64 rm = m_regs.r[opc3 & 0x001f];
				const u64 xw_ext = (u64)(u32)xw;
				const u64 result = rm - xw_ext;
				m_regs_new.r[opc4 & 0x001f] = (u32)result;
				if (result != 0)
					m_cc &= ~CC_Z;
				else
					m_cc |= CC_Z;

				if (BIT(result, 31))
					m_cc |= CC_N;
				else
					m_cc &= ~CC_N;

				if (result & 0xffffffff00000000ULL)
					m_cc |= CC_C;
				else
					m_cc &= ~CC_C;

				if (BIT(rm, 31) != BIT(xw_ext, 31) && BIT(xw_ext, 31) == BIT(result, 31))
					m_cc |= CC_V;
				else
					m_cc &= ~CC_V;
				return;
			}

			if ((opc3 & 0xfc00) == 0x9800 && (opc4 & 0xefe0) == 0xa320)
			{	// and #xw/32s, rm, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 1001 10ww wwwm mmmm . 101f 0011 001n nnnn
				const u32 xw = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u32 rm = m_regs.r[opc3 & 0x001f];
				const u32 val = rm & xw;
				m_regs_new.r[opc4 & 0x001f] = val;

				if (!val)
					m_cc |= CC_Z;
				else
					m_cc &= ~CC_Z;

				if (BIT(val, 31))
					m_cc |= CC_N;
				else
					m_cc &= ~CC_N;

				m_cc &= ~CC_V;
				return;
			}

			if ((opc3 & 0xfc00) == 0x9800 && (opc4 & 0xefe0) == 0xa520)
			{	// or #xw/32s, rm, rn
				// 1000 1xxx xxxx xxxx . xxxx xxxx xxxx xxxx . 1001 10ww wwwm mmmm . 101f 0101 001n nnnn
				const u32 xw = ((opc1 & 0x07ff) << 21) | (opc2 << 5) | ((opc3 & 0x03e0) >> 5);
				const u32 rm = m_regs.r[opc3 & 0x001f];
				const u32 val = rm | xw;
				m_regs_new.r[opc4 & 0x001f] = val;

				if (!val)
					m_cc |= CC_Z;
				else
					m_cc &= ~CC_Z;

				if (BIT(val, 31))
					m_cc |= CC_N;
				else
					m_cc &= ~CC_N;

				m_cc &= ~CC_V;
				return;
			}

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
		debugger_instruction_hook(m_pc);

		memcpy(&m_regs_new, &m_regs, sizeof(gprs_t));

		bool cont = true;
		while (cont)
		{
			execute_packet(cont);
		}

		memcpy(&m_regs, &m_regs_new, sizeof(gprs_t));

		if (!(m_mpectl & (1 << 1)))
		{
			m_icount = 0;
			break;
		}

		if (m_branch_delay)
		{
			m_branch_delay--;
			if (!m_branch_delay)
			{
				if (BIT(m_branch_pc, 32))
					m_rz = (u32)m_pc;
				m_pc = (u32)m_branch_pc;
				print_func_name(m_pc);
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
		fatalerror("%s: unknown opcode (%04x %04x %04x %04x) at %08x\n", tag(), *opc1, *opc2, *opc3, *opc4, pc);
	}
	else if (opc3 != nullptr)
	{
		fatalerror("%s: unknown opcode (%04x %04x %04x) at %08x\n", tag(), *opc1, *opc2, *opc3, pc);
	}
	else if (opc2 != nullptr)
	{
		fatalerror("%s: unknown opcode (%04x %04x) at %08x\n", tag(), *opc1, *opc2, pc);
	}
	else
	{
		fatalerror("%s: unknown opcode (%04x) at %08x\n", tag(), *opc1, pc);
	}
}

void nuon_mpe_device::print_func_name(const u32 pc)
{
	u32 func_idx = 0;
	while (s_nuon_funcs[func_idx].addr < pc && s_nuon_funcs[func_idx].addr != 0xffffffff)
		func_idx++;

	if (s_nuon_funcs[func_idx].addr != 0xffffffff && s_nuon_funcs[func_idx].addr == pc)
	{
		logerror("Branching to function %s (%08x)\n", s_nuon_funcs[func_idx].name, s_nuon_funcs[func_idx].addr);
	}
}
