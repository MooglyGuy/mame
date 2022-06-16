// license:BSD-3-Clause
// copyright-holders: Joakim Larsson Edstrom

/*
 * vme.h
 *
 * VME bus system
 *
 * Pinout: (from http://pinouts.ru/Slots/vmebus_pinout.shtml)

     P1/J1                                   P2/J2 (optional for 32 bit)
    +-A-B-C--+  A        B         C        +-A-B-C--+  A     B        C
 01 | [][][] | D00      BBSY*     D08       | [][][] | n/a   +5v      n/a
 02 | [][][] | D01      BCLR*     D09       | [][][] | n/a   GROUND   n/a
 03 | [][][] | D02      ACFAIL*   D10       | [][][] | n/a   RESERVED n/a
 04 | [][][] | D03      BG0IN*    D11       | [][][] | n/a   A24      n/a
 05 | [][][] | D04      BG0OUT*   D12       | [][][] | n/a   A25      n/a
 06 | [][][] | D05      BG1IN*    D13       | [][][] | n/a   A26      n/a
 07 | [][][] | D06      BG1OUT*   D14       | [][][] | n/a   A27      n/a
 08 | [][][] | D07      BG2IN*    D15       | [][][] | n/a   A28      n/a
 09 | [][][] | GROUND   BG2OUT*   GROUND    | [][][] | n/a   A29      n/a
 10 | [][][] | SYSCLK   BG3IN*    SYSFAIL*  | [][][] | n/a   A30      n/a
 11 | [][][] | GROUND   BG3OUT*   BERR*     | [][][] | n/a   A31      n/a
 12 | [][][] | DS1*     BR0*      SYSRESET* | [][][] | n/a   GROUND   n/a
 13 | [][][] | DS0*     BR1*      LWORD*    | [][][] | n/a   +5v      n/a
 14 | [][][] | WRITE*   BR2*      AM5       | [][][] | n/a   D16      n/a
 15 | [][][] | GROUND   BR3*      A23       | [][][] | n/a   D17      n/a
 16 | [][][] | DTACK*   AM0       A22       | [][][] | n/a   D18      n/a
 17 | [][][] | GROUND   AM1       A21       | [][][] | n/a   D19      n/a
 18 | [][][] | AS*      AM2       A20       | [][][] | n/a   D20      n/a
 19 | [][][] | GROUND   AM3       A19       | [][][] | n/a   D21      n/a
 20 | [][][] | IACK*    GROUND    A18       | [][][] | n/a   D22      n/a
 21 | [][][] | IACKIN*  SERCLK*   A17       | [][][] | n/a   D23      n/a
 22 | [][][] | IACKOUT* SERDAT*   A16       | [][][] | n/a   GROUND   n/a
 23 | [][][] | AM4      GROUND    A15       | [][][] | n/a   D24      n/a
 24 | [][][] | A07      IRQ7*     A14       | [][][] | n/a   D25      n/a
 25 | [][][] | A06      IRQ6*     A13       | [][][] | n/a   D26      n/a
 26 | [][][] | A05      IRQ5*     A12       | [][][] | n/a   D27      n/a
 27 | [][][] | A04      IRQ4*     A11       | [][][] | n/a   D28      n/a
 28 | [][][] | A03      IRQ3*     A10       | [][][] | n/a   D29      n/a
 29 | [][][] | A02      IRQ2*     A09       | [][][] | n/a   D30      n/a
 30 | [][][] | A01      IRQ1*     A08       | [][][] | n/a   D31      n/a
 31 | [][][] | -12v     +5v STDBY +12v      | [][][] | n/a   GROUND   n/a
 32 | [][][] | +5v      +5v       +5v       | [][][] | n/a   +5v      n/a

 */

#ifndef MAME_BUS_VME_VME_H
#define MAME_BUS_VME_VME_H

#pragma once

namespace vme
{
	enum address_modifier : u8
	{
		IACK  = 0x00, // interrupt acknowledge

		AM_09 = 0x09, // a32, non-privileged, data
		AM_0a = 0x0a, // a32, non-privileged, instruction
		AM_0b = 0x0b, // a32, non-privileged, block transfer
		AM_0d = 0x0d, // a32, privileged, data
		AM_0e = 0x0e, // a32, privileged, instruction
		AM_0f = 0x0f, // a32, privileged, block transfer

		AM_29 = 0x29, // a16, non-privileged
		AM_2d = 0x2d, // a16, privileged

		AM_39 = 0x39, // a24, non-privileged, data
		AM_3a = 0x3a, // a24, non-privileged, instruction
		AM_3b = 0x3b, // a24, non-privileged, block transfer
		AM_3d = 0x3d, // a24, privileged, data
		AM_3e = 0x3e, // a24, privileged, instruction
		AM_3f = 0x3f, // a24, privileged, block transfer
	};
}

class vme_bus_device
	: public device_t
	, public device_memory_interface
{
public:
<<<<<<< HEAD
	vme_bus_device(machine_config const &mconfig, char const *tag, device_t *owner, const XTAL &clock = XTAL(16'000'000), u8 datawidth = 32);
=======
	// VME BUS signals driven to or drived by the VME bus
	enum class control
	{
		AS,
		DS0,
		DS1,
		BERR,
		DTACK,
		WRITE
	};

	enum class address
	{
		DS0,
		DS1,
		LWORD
	};

	// construction/destruction
	template <typename T, typename U>
	vme_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, T &&opts, char const *dflt, uint32_t slot_nbr, U &&bus_tag)
		: vme_slot_device(mconfig, tag, owner)
	{
		option_reset();
		opts(*this);
		set_default_option(dflt);
		set_fixed(false);
		set_vme_slot(std::forward<U>(bus_tag), slot_nbr);
	}

	vme_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, const XTAL &clock = XTAL());

	// Callbacks to the board from the VME bus comes through here
	auto vme_j1_callback()  { return m_vme_j1_callback.bind(); }

	template <typename T> void set_vme_slot(T &&tag, uint32_t slot_nbr) { m_vme.set_tag(std::forward<T>(tag)); m_slot_nbr = slot_nbr; }

	virtual uint8_t read8(offs_t offset);
	virtual void write8(offs_t offset, uint8_t data);

protected:
	vme_slot_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, const XTAL &clock);

	// device-level overrides
	virtual void device_start() override;
	virtual void device_resolve_objects() override;
>>>>>>> 45d4cd52a81 (full xtal conversion)

	// configuration
	auto berr() { return m_berr.bind(); }
	template <unsigned I> auto irq() { return m_irq[I - 1].bind(); }

<<<<<<< HEAD
	// runtime
	void berr_w(int state) { m_berr(state); }
	int iackin_r() { return !m_iack; }
	void iackout_w(int state) { m_iack = !state; }
	template <unsigned I> void irq_w(int state);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
=======
	// callbacks
	devcb_write_line        m_vme_j1_callback;
	device_vme_card_interface *m_card;
};

DECLARE_DEVICE_TYPE(VME, vme_device)


class vme_card_interface;

class vme_device : public device_t,
	public device_memory_interface
{
public:
	vme_device(const machine_config &mconfig, const char *tag, device_t *owner, const XTAL &clock);
	~vme_device();

	// inline configuration
	void set_cputag(const char *tag) { m_cputag = tag; }
	void use_owner_spaces();

	const address_space_config m_a32_config;

	void add_vme_card(device_vme_card_interface *card);

	//
	// Address Modifiers
	//
	/* There are 6 address modifier lines. They allow the MASTER to pass additional binary
	   information to the SLAVE during data transfers. Table 2-3 lists all of the 64 possible
	   address modifier (AM) codes and classifies each into one of three categories:
	   - Defined
	   - Reserved
	   - User defined
	   The defined address modifier codes can be further classified into three categories:
	   Short addressing AM codes indicate that address lines A02-A15 are being used to select a BYTE(0-3) group.
	   Standard addressing AM codes ,indicate that address lines A02-A23 are being used to select a BYTE(0-3) group.
	   Extended addressing AM codes indicate that address lines A02-A31 are being used to select a BYTE(0-3) group.*/

	enum vme_amod_t
	{   // Defined and User Defined Address Modifier Values (long bnames from VME standard text. please use short)
		AMOD_EXTENDED_NON_PRIV_DATA = 0x09, //A32 SC (Single Cycle)
		A32_SC                      = 0x09, //A32 SC (Single Cycle)
		AMOD_EXTENDED_NON_PRIV_PRG  = 0x0A,
		AMOD_EXTENDED_NON_PRIV_BLK  = 0x0B,
		AMOD_EXTENDED_SUPERVIS_DATA = 0x0D,
		AMOD_EXTENDED_SUPERVIS_PRG  = 0x0E,
		AMOD_EXTENDED_SUPERVIS_BLK  = 0x0F,
		AMOD_USER_DEFINED_FIRST     = 0x10,
		AMOD_USER_DEFINED_LAST      = 0x1F,
		AMOD_SHORT_NON_PRIV_ACCESS  = 0x29, //A16 SC
		A16_SC                      = 0x29, //A16 SC
		AMOD_SHORT_SUPERVIS_ACCESS  = 0x2D,
		AMOD_STANDARD_NON_PRIV_DATA = 0x39, //A24 SC
		A24_SC                      = 0x39, //A24 SC
		AMOD_STANDARD_NON_PRIV_PRG  = 0x3A,
		AMOD_STANDARD_NON_PRIV_BLK  = 0x3B, //A24 BLT
		AMOD_STANDARD_SUPERVIS_DATA = 0x3D,
		AMOD_STANDARD_SUPERVIS_PRG  = 0x3E,
		AMOD_STANDARD_SUPERVIS_BLK  = 0x3F
	};
	void install_device(vme_amod_t amod, offs_t start, offs_t end, read8_delegate rhandler, write8_delegate whandler, uint32_t mask);
	void install_device(vme_amod_t amod, offs_t start, offs_t end, read8sm_delegate rhandler, write8sm_delegate whandler, uint32_t mask);
	void install_device(vme_amod_t amod, offs_t start, offs_t end, read8smo_delegate rhandler, write8smo_delegate whandler, uint32_t mask);
	//  void install_device(vme_amod_t amod, offs_t start, offs_t end, read8_delegate rhandler, write8_delegate whandler);
	void install_device(vme_amod_t amod, offs_t start, offs_t end, read16_delegate rhandler, write16_delegate whandler, uint32_t mask);
	void install_device(vme_amod_t amod, offs_t start, offs_t end, read32_delegate rhandler, write32_delegate whandler, uint32_t mask);

protected:
	vme_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, const XTAL &clock);

	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
	simple_list<device_vme_card_interface> m_device_list;
>>>>>>> 45d4cd52a81 (full xtal conversion)

	// device_memory_interface implementation
	virtual space_config_vector memory_space_config() const override;

private:
	void iack(address_map &map) ATTR_COLD;
	void a16(address_map &map) ATTR_COLD;
	void a24(address_map &map) ATTR_COLD;
	void a32(address_map &map) ATTR_COLD;

	u32 read_iack(address_space &space, offs_t offset, u32 mem_mask);
	u32 read_berr(address_space &space, offs_t offset, u32 mem_mask);
	void write_berr(address_space &space, offs_t offset, u32 data, u32 mem_mask);

	address_space_config const m_asc[64];

	devcb_write_line::array<7> m_irq;
	devcb_write_line m_berr;

	u8 m_irq_count[7];
	bool m_iack;
};

class vme_slot_device
	: public device_t
	, public device_slot_interface
{
public:
	vme_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = DERIVED_CLOCK(1, 1));

	template <typename T>
	vme_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, T &&slot_options, char const *default_option, bool const fixed = false)
		: vme_slot_device(mconfig, tag, owner, DERIVED_CLOCK(1, 1))
	{
		option_reset();
		slot_options(*this);
		set_default_option(default_option);
		set_fixed(fixed);
	}

protected:
	// device_t implementation
	virtual void device_config_complete() override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;

private:
};

class device_vme_card_interface : public device_interface
{
public:
	template <vme::address_modifier AM, offs_t base = 0> u32 vme_read32(offs_t offset, u32 mem_mask)
	{
		m_master = true;
		u32 const data = m_bus->space(AM).read_dword(base + (offset << 2), mem_mask);
		m_master = false;

		return data;
	}
	template <vme::address_modifier AM, offs_t base = 0> u16 vme_read16(offs_t offset, u16 mem_mask)
	{
		m_master = true;
		u16 const data = m_bus->space(AM).read_word(base + (offset << 1), mem_mask);
		m_master = false;

		return data;
	}
	template <vme::address_modifier AM, offs_t base = 0> void vme_write32(offs_t offset, u32 data, u32 mem_mask)
	{
		m_master = true;
		m_bus->space(AM).write_dword(base + (offset << 2), data, mem_mask);
		m_master = false;
	}
	template <vme::address_modifier AM, offs_t base = 0> void vme_write16(offs_t offset, u16 data, u16 mem_mask)
	{
		m_master = true;
		m_bus->space(AM).write_word(base + (offset << 1), data, mem_mask);
		m_master = false;
	}
	u32 vme_iack_r(offs_t offset)
	{
		return m_bus->space(vme::IACK).read_dword(offset);
	}

	void set_bus(vme_bus_device &bus);

protected:
	device_vme_card_interface(machine_config const &mconfig, device_t &device);

	// device_interface implementation
	virtual void interface_post_start() override;

	// configuration
	auto vme_berr() { return m_berr.bind(); }                               // receive a bus error
	template <unsigned I> auto vme_irq() { return m_irq[I - 1].bind(); }    // receive a bus interrupt
	auto vme_iack() { return m_iack.bind(); }                               // provide an interrupt acknowledge vector

	// runtime
	void vme_berr_w(int state) { m_bus->berr_w(state); }                    // generate a bus error
	template <unsigned I> void vme_irq_w(int state);                        // generate a bus interrupt

	address_space &vme_space(vme::address_modifier am) const { return m_bus->space(am); }

private:
	vme_bus_device *m_bus;

	devcb_write_line m_berr;
	devcb_write_line::array<7> m_irq;
	devcb_read32 m_iack;

	u8 m_irq_active;
	bool m_master;
};

DECLARE_DEVICE_TYPE(VME, vme_bus_device)
DECLARE_DEVICE_TYPE(VME_SLOT, vme_slot_device)

#endif // MAME_BUS_VME_VME_H
