// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/***********************************************************************************************************************************

    VM Labs Aries 3 "NUON Multi-Media Architecture" processor-element skeleton

************************************************************************************************************************************/

#ifndef MAME_CPU_NUON_NUON_H
#define MAME_CPU_NUON_NUON_H

#pragma once

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

enum
{
	NUON_PC = 1,
	NUON_CC,
	NUON_R00,
	NUON_R01,
	NUON_R02,
	NUON_R03,
	NUON_R04,
	NUON_R05,
	NUON_R06,
	NUON_R07,
	NUON_R08,
	NUON_R09,
	NUON_R10,
	NUON_R11,
	NUON_R12,
	NUON_R13,
	NUON_R14,
	NUON_R15,
	NUON_R16,
	NUON_R17,
	NUON_R18,
	NUON_R19,
	NUON_R20,
	NUON_R21,
	NUON_R22,
	NUON_R23,
	NUON_R24,
	NUON_R25,
	NUON_R26,
	NUON_R27,
	NUON_R28,
	NUON_R29,
	NUON_R30,
	NUON_R31,
	NUON_V0H,
	NUON_V0L,
	NUON_V1H,
	NUON_V1L,
	NUON_V2H,
	NUON_V2L,
	NUON_V3H,
	NUON_V3L,
	NUON_V4H,
	NUON_V4L,
	NUON_V5H,
	NUON_V5L,
	NUON_V6H,
	NUON_V6L,
	NUON_V7H,
	NUON_V7L,
	NUON_HV00,
	NUON_HV02,
	NUON_HV04,
	NUON_HV06,
	NUON_HV08,
	NUON_HV10,
	NUON_HV12,
	NUON_HV14,
	NUON_HV16,
	NUON_HV18,
	NUON_HV20,
	NUON_HV22,
	NUON_HV24,
	NUON_HV26,
	NUON_HV28,
	NUON_HV30,
	NUON_SV0H,
	NUON_SV0MM,
	NUON_SV0M,
	NUON_SV0L,
	NUON_SV1H,
	NUON_SV1MM,
	NUON_SV1M,
	NUON_SV1L,
	NUON_SV2H,
	NUON_SV2MM,
	NUON_SV2M,
	NUON_SV2L,
	NUON_SV3H,
	NUON_SV3MM,
	NUON_SV3M,
	NUON_SV3L,
	NUON_SV4H,
	NUON_SV4MM,
	NUON_SV4M,
	NUON_SV4L,
	NUON_SV5H,
	NUON_SV5MM,
	NUON_SV5M,
	NUON_SV5L,
	NUON_SV6H,
	NUON_SV6MM,
	NUON_SV6M,
	NUON_SV6L,
	NUON_SV7H,
	NUON_SV7MM,
	NUON_SV7M,
	NUON_SV7L,
	NUON_P0H,
	NUON_P0M,
	NUON_P0L,
	NUON_P1H,
	NUON_P1M,
	NUON_P1L,
	NUON_P2H,
	NUON_P2M,
	NUON_P2L,
	NUON_P3H,
	NUON_P3M,
	NUON_P3L,
	NUON_P4H,
	NUON_P4M,
	NUON_P4L,
	NUON_P5H,
	NUON_P5M,
	NUON_P5L,
	NUON_P6H,
	NUON_P6M,
	NUON_P6L,
	NUON_P7H,
	NUON_P7M,
	NUON_P7L,

	NUON_RC0,
	NUON_RC1,
	NUON_ODMACTL,
};

class nuon_mpe_device : public cpu_device
{
public:
	nuon_mpe_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

private:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	// device_execute_interface overrides
	virtual u32 execute_min_cycles() const noexcept override { return 1; }
	virtual u32 execute_max_cycles() const noexcept override { return 1; }
	virtual u32 execute_input_lines() const noexcept override { return 0; }
	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override { }

	// device_memory_interface overrides
	virtual space_config_vector memory_space_config() const override;

	// device_disasm_interface overrides
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

	// device_state_interface overrides
	virtual void state_string_export(const device_state_entry &entry, std::string &str) const override;

	// internal memory map
	void internal_map(address_map &map);

	// internal functions
	u32 rc0_r(offs_t offset);
	void rc0_w(offs_t offset, u32 data);
	u32 rc1_r(offs_t offset);
	void rc1_w(offs_t offset, u32 data);
	u32 odmacptr_r(offs_t offset);
	void odmacptr_w(offs_t offset, u32 data);
	void execute_packet(bool &cont);
	void unimplemented_opcode(const u32 pc, const u16 *opc1, const u16 *opc2 = nullptr, const u16 *opc3 = nullptr, const u16 *opc4 = nullptr);

	enum : u16
	{
		CC_Z_BIT,
		CC_C_BIT,
		CC_V_BIT,
		CC_N_BIT,
		CC_MV_BIT,
		CC_C0Z_BIT,
		CC_C1Z_BIT,
		CC_MODGE_BIT,
		CC_MODMI_BIT,
		CC_CF0_BIT,
		CC_CF1_BIT,

		CC_Z = (1 << CC_Z_BIT),
		CC_C = (1 << CC_C_BIT),
		CC_V = (1 << CC_V_BIT),
		CC_N = (1 << CC_N_BIT),
		CC_MV = (1 << CC_MV_BIT),
		CC_C0Z = (1 << CC_C0Z_BIT),
		CC_C1Z = (1 << CC_C1Z_BIT),
		CC_MODGE = (1 << CC_MODGE_BIT),
		CC_MODMI = (1 << CC_MODMI_BIT),
		CC_CF0 = (1 << CC_CF0_BIT),
		CC_CF1 = (1 << CC_CF1_BIT)
	};

	const address_space_config m_program_config;
	address_space *m_program;

	// registers
	u32 m_pc;
	u32 m_branch_pc;
	u32 m_branch_delay;
	u16 m_cc;
	u16 m_rc0;
	u16 m_rc1;
	u8 m_odmactl;
	u32 m_odmacptr;

	union
	{
		u32 r[32];
	#ifdef LSB_FIRST
		struct { u64 l,h; } v[8];
		union {
			u32 l,h;
			u64 v;
		} hv[16];
		struct { u16 u0,w0,u1,w1,u2,w2,u3,w3; } sv[8];
		struct { u16 u0,u1,u2,c0,u3,c1,u4,c2; } p[8];
	#else
		struct { u64 h,l; } v[8];
		union {
			u32 h,l;
			u64 v;
		} hv[16];
		struct { u16 w3,u3,w2,u2,w1,u1,w0,u0; } sv[8];
		struct { u16 c2,u4,c1,u3,c0,u2,u1,u0; } p[8];
	#endif
	} m_regs;

	// other internal states
	int m_icount;
};

DECLARE_DEVICE_TYPE(NUON_MPE, nuon_mpe_device)

#endif /* MAME_CPU_NUON_NUON_H */
