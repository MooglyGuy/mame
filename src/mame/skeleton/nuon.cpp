// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/***********************************************************************************************************************************

	Skeleton driver for Nuon "Aries 3" development kit

************************************************************************************************************************************/

#include "emu.h"
#include "cpu/nuon/nuon.h"

#define VERBOSE (1)
#include "logmacro.h"

class nuon_state : public driver_device
{
public:
	nuon_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_mpe(*this, "mpe%u", 0u)
	{ }

	void nuondev(machine_config &config);

private:
	virtual void driver_init() override;

	void mem_map(address_map &map);
	void mpe0_map(address_map &map);
	void mpe1_map(address_map &map);
	void mpe2_map(address_map &map);
	void mpe3_map(address_map &map);

	void commbus_w(offs_t offset, u32 data);

	required_device_array<nuon_mpe_device, 4> m_mpe;
};

void nuon_state::mem_map(address_map &map)
{
	map(0x20000000, 0x207fffff).m(m_mpe[0], FUNC(nuon_mpe_device::dma_map));
	map(0x20800000, 0x20ffffff).m(m_mpe[1], FUNC(nuon_mpe_device::dma_map));
	map(0x21000000, 0x217fffff).m(m_mpe[2], FUNC(nuon_mpe_device::dma_map));
	map(0x21800000, 0x21ffffff).m(m_mpe[3], FUNC(nuon_mpe_device::dma_map));
	map(0x40000000, 0x407fffff).ram().share("mainram");
	map(0x80000000, 0x807fffff).ram().share("sysram");
	map(0xf0000000, 0xf03fffff).rom().region("maincpu", 0);
}

void nuon_state::mpe0_map(address_map &map)
{
	map(0x20000000, 0x207fffff).m(m_mpe[0], FUNC(nuon_mpe_device::dma_map));
	map(0x40000000, 0x407fffff).ram().share("mainram");
	map(0x80000000, 0x807fffff).ram().share("sysram");
	map(0xf0000000, 0xf03fffff).rom().region("maincpu", 0);
}

void nuon_state::mpe1_map(address_map &map)
{
	map(0x20000000, 0x207fffff).m(m_mpe[1], FUNC(nuon_mpe_device::dma_map));
}

void nuon_state::mpe2_map(address_map &map)
{
	map(0x20000000, 0x207fffff).m(m_mpe[2], FUNC(nuon_mpe_device::dma_map));
}

void nuon_state::mpe3_map(address_map &map)
{
	map(0x20000000, 0x207fffff).m(m_mpe[3], FUNC(nuon_mpe_device::dma_map));
	map(0x40000000, 0x407fffff).ram().share("mainram");
	map(0x80000000, 0x807fffff).ram().share("sysram");
	map(0xf0000000, 0xf03fffff).rom().region("maincpu", 0);
}

void nuon_state::commbus_w(offs_t offset, u32 data)
{
	static const char *const s_device_ids[0x80] =
	{
		"MPE 0", "MPE 1", "MPE 2", "MPE 3", "MPE 4", "MPE 5", "MPE 6", "MPE 7",
		"MPE 8", "MPE 9", "MPE 10", "MPE 11", "MPE 12", "MPE 13", "MPE 14", "MPE 15",
		"MPE 16", "MPE 17", "MPE 18", "MPE 19", "MPE 20", "MPE 21", "MPE 22", "MPE 23",
		"MPE 24", "MPE 25", "MPE 26", "MPE 27", "MPE 28", "MPE 29", "MPE 30", "MPE 31",
		"MPE 32", "MPE 33", "MPE 34", "MPE 35", "MPE 36", "MPE 37", "MPE 38", "MPE 39",
		"MPE 40", "MPE 41", "MPE 42", "MPE 43", "MPE 44", "MPE 45", "MPE 46", "MPE 47",
		"MPE 48", "MPE 49", "MPE 50", "MPE 51", "MPE 52", "MPE 53", "MPE 54", "MPE 55",
		"MPE 56", "MPE 57", "MPE 58", "MPE 59", "MPE 60", "MPE 61", "MPE 62", "MPE 63",
		"Reserved", "Video Output Ctlr.", "Video Input Ctlr.", "Audio Intf.", "Debug Ctlr.", "Misc. I/O Intf.", "ROM Bus Intf.", "DMA Ctlr.",
		"External Host", "BDU", "Coded Data Intf.", "Serial Bus", "Reserved", "Reserved", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
	};

	const u8 target_id = offset & 0x7f;
	const u8 source_id = (offset >> 7) & 0x7f;
	const u8 packet_num = (offset >> 14) & 3;

	LOG("Communication Bus Write, Packet %d from %s, to %s: %08x\n", packet_num, s_device_ids[source_id], s_device_ids[target_id], data);
}

static INPUT_PORTS_START(nuondev)
INPUT_PORTS_END

void nuon_state::nuondev(machine_config &config)
{
	NUON_MPE(config, m_mpe[0], 108'000'000, 0);
	m_mpe[0]->set_addrmap(AS_PROGRAM, &nuon_state::mpe0_map);
	m_mpe[0]->set_addrmap(AS_IO, &nuon_state::mem_map);
	m_mpe[0]->commbus_out().set(FUNC(nuon_state::commbus_w));

	NUON_MPE(config, m_mpe[1], 108'000'000, 1);
	m_mpe[1]->set_addrmap(AS_PROGRAM, &nuon_state::mpe1_map);
	m_mpe[1]->set_addrmap(AS_IO, &nuon_state::mem_map);
	m_mpe[1]->commbus_out().set(FUNC(nuon_state::commbus_w));

	NUON_MPE(config, m_mpe[2], 108'000'000, 2);
	m_mpe[2]->set_addrmap(AS_PROGRAM, &nuon_state::mpe2_map);
	m_mpe[2]->set_addrmap(AS_IO, &nuon_state::mem_map);
	m_mpe[2]->commbus_out().set(FUNC(nuon_state::commbus_w));

	NUON_MPE(config, m_mpe[3], 108'000'000, 3);
	m_mpe[3]->set_addrmap(AS_PROGRAM, &nuon_state::mpe3_map);
	m_mpe[3]->set_addrmap(AS_IO, &nuon_state::mem_map);
	m_mpe[3]->commbus_out().set(FUNC(nuon_state::commbus_w));
}

void nuon_state::driver_init()
{
	const u32 *flashrom = (const u32 *)memregion("maincpu")->base();
	for (u32 i = 0; i < 0x100/4; i++)
		m_mpe[0]->space(AS_PROGRAM).write_dword(0x20300000 + i * 4, flashrom[i]);
}

ROM_START(nuondev)
	ROM_REGION32_BE(0x400000, "maincpu", 0)
	ROM_LOAD16_WORD("nuondev.bin", 0x000000, 0x400000, CRC(e5e29693) SHA1(9bc2844973be0b49a4632029868a7907fa01ed51))
ROM_END

CONS( 2000, nuondev, 0, 0, nuondev, nuondev, nuon_state, empty_init, "VM Labs, Inc.", "NUON Development System", MACHINE_IS_SKELETON )
