// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/***********************************************************************************************************************************

	Skeleton driver for Nuon "Aries 3" development kit

************************************************************************************************************************************/

#include "emu.h"
#include "cpu/nuon/nuon.h"

class nuon_state : public driver_device
{
public:
	nuon_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_testram(*this, "testram")
	{ }

	void nuondev(machine_config &config);

private:
	virtual void driver_init() override;

	void mem_map(address_map &map);

	required_device<cpu_device> m_maincpu;
	required_shared_ptr<u32> m_testram;
};

void nuon_state::mem_map(address_map &map)
{
	map(0x20000000, 0x203fffff).ram().share("testram");
	map(0xf0000000, 0xf03fffff).rom().region("maincpu", 0);
}

static INPUT_PORTS_START(nuondev)
INPUT_PORTS_END

void nuon_state::nuondev(machine_config &config)
{
	NUON_MPE(config, m_maincpu, 108'000'000);
	m_maincpu->set_addrmap(AS_PROGRAM, &nuon_state::mem_map);
}

void nuon_state::driver_init()
{
	memcpy((u8 *)m_testram.target() + 0x300000, (const u8 *)memregion("maincpu")->base(), 0x100);
}

ROM_START(nuondev)
	ROM_REGION32_BE(0x400000, "maincpu", 0)
	ROM_LOAD16_WORD("nuondev.bin", 0x000000, 0x400000, CRC(7fec0804) SHA1(fca76f6c4143a78c4dd6a785f48838d155296b80))
ROM_END

CONS( 2000, nuondev, 0, 0, nuondev, nuondev, nuon_state, empty_init, "VM Labs, Inc.", "NUON Development System", MACHINE_IS_SKELETON )
