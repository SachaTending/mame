// license:BSD-3-Clause
// copyright-holders:Angelo Salese
/**************************************************************************************************

Paradise / Western Digital (S)VGA chipsets

- PVGA1A
- PVGA1A-JK / WD90C90-JK (same as PVGA1A with extra connectors?)

TODO:
- Complete WD90C00-JK
- WD90C11-LR / WD90C11A-LR (WD90C00 with new sequencer regs)
- WD90C30-LR / WD90C31-LR / WD90C31-ZS / WD90C31A-LR / WD90C31A-ZS
- WD90C33-ZZ
- WD90C24A-ZZ / WD90C24A2-ZZ (mobile chips, no ISA option)
- WD90C26A (apple/macpwrbk030.cpp macpb180c, no ISA)
- WD9710-MZ (PCI + MPEG-1, a.k.a. Pipeline 9710 / 9712)

- Memory Data pins (MD) & CNF
- /EBROM signal (for enabling ROM readback)

**************************************************************************************************/

#include "emu.h"
#include "pc_vga_paradise.h"

#include "screen.h"

#define LOG_BANK    (1U << 2) // log banking r/ws
#define LOG_LOCKED  (1U << 8) // log locking mechanism

#define VERBOSE (LOG_GENERAL | LOG_LOCKED)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

#define LOGBANK(...)     LOGMASKED(LOG_BANK,  __VA_ARGS__)
#define LOGLOCKED(...)     LOGMASKED(LOG_LOCKED,  __VA_ARGS__)


DEFINE_DEVICE_TYPE(PVGA1A, pvga1a_vga_device, "pvga1a_vga", "Paradise Systems PVGA1A")
DEFINE_DEVICE_TYPE(WD90C00, wd90c00_vga_device, "wd90c00_vga", "Western Digital WD90C00 VGA Controller")

pvga1a_vga_device::pvga1a_vga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: svga_device(mconfig, type, tag, owner, clock)
	, m_ext_gc_view(*this, "ext_gc_view")
{
}

pvga1a_vga_device::pvga1a_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: pvga1a_vga_device(mconfig, PVGA1A, tag, owner, clock)
{
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(pvga1a_vga_device::gc_map), this));
}

void pvga1a_vga_device::device_start()
{
	svga_device::device_start();
	zero();

	// Avoid an infinite loop when displaying.  0 is not possible anyway.
	vga.crtc.maximum_scan_line = 1;

	// copy over interfaces
	vga.svga_intf.vram_size = 1*1024*1024;
	//vga.memory = std::make_unique<uint8_t []>(vga.svga_intf.vram_size);
}

void pvga1a_vga_device::device_reset()
{
	svga_device::device_reset();

	m_memory_size = 0;
	m_video_control = 0; // Really &= 0x8; at POR according to docs
	m_video_select = 0;
	m_crtc_lock = 0;
	m_ext_gc_unlock = false;
	m_ext_gc_view.select(0);
}

uint8_t pvga1a_vga_device::mem_r(offs_t offset)
{
	if (svga.rgb8_en)
		return svga_device::mem_linear_r(offset + (svga.bank_w * 0x1000));
	return svga_device::mem_r(offset);
}

void pvga1a_vga_device::mem_w(offs_t offset, uint8_t data)
{
	// TODO: Address Offset B, not extensively tested
	// Should also enable thru bits 5-4 of PR1 but instead SW seems to use 7-6!?
	if (svga.rgb8_en)
	{
		svga_device::mem_linear_w(offset + (svga.bank_w * 0x1000), data);
		return;
	}
	svga_device::mem_w(offset, data);
}

void pvga1a_vga_device::gc_map(address_map &map)
{
	svga_device::gc_map(map);
	map(0x09, 0x0e).view(m_ext_gc_view);
	m_ext_gc_view[0](0x09, 0x0e).lr8(
		NAME([this] (offs_t offset) {
			LOGLOCKED("Attempt to R ext. register offset %02x while locked\n", offset + 9);
			return 0xff;
		})
	);
	m_ext_gc_view[1](0x09, 0x0a).rw(FUNC(pvga1a_vga_device::address_offset_r), FUNC(pvga1a_vga_device::address_offset_w));
	m_ext_gc_view[1](0x0b, 0x0b).rw(FUNC(pvga1a_vga_device::memory_size_r), FUNC(pvga1a_vga_device::memory_size_w));
	m_ext_gc_view[1](0x0c, 0x0c).rw(FUNC(pvga1a_vga_device::video_select_r), FUNC(pvga1a_vga_device::video_select_w));
	m_ext_gc_view[1](0x0d, 0x0d).rw(FUNC(pvga1a_vga_device::crtc_lock_r), FUNC(pvga1a_vga_device::crtc_lock_w));
	m_ext_gc_view[1](0x0e, 0x0e).rw(FUNC(pvga1a_vga_device::video_control_r), FUNC(pvga1a_vga_device::video_control_w));
	map(0x0f, 0x0f).rw(FUNC(pvga1a_vga_device::ext_gc_status_r), FUNC(pvga1a_vga_device::ext_gc_unlock_w));
}

/*
 * [0x09] PR0A Address Offset A
 * [0x0a] PR0B Address Offset B
 *
 * -xxx xxxx bank selects, in 4KB units
 */
u8 pvga1a_vga_device::address_offset_r(offs_t offset)
{
	if (!offset)
	{
		LOGBANK("PR0A read Address Offset A\n");
		return svga.bank_w & 0x7f;
	}
	// Address Offset B, TBD
	LOGBANK("PR0A read Address Offset B\n");
	return 0;
}

void pvga1a_vga_device::address_offset_w(offs_t offset, u8 data)
{
	if (!offset)
	{
		LOG("PR0A write Address Offset A %02x\n", data);
		svga.bank_w = data & 0x7f;
	}
	else
	{
		LOG("PR0B write Address Offset B %02x\n", data);
		// ...
	}
}

/*
 * [0x0b] PR1 Memory Size
 *
 * xx-- ---- Memory Size
 * 11-- ---- 1MB
 * 10-- ---- 512KB
 * 0x-- ---- 256KB
 * --xx ---- Memory Map Select
 * ---- x--- Enable PR0B
 * ---- -x-- Enable 16-bit memory bus
 * ---- --x- Enable 16-bit BIOS ROM reads (MD1)
 * ---- ---x BIOS ROM mapped (MD0)
 */
u8 pvga1a_vga_device::memory_size_r(offs_t offset)
{
	LOG("PR1 Memory Size R\n");
	return 0xc0 | (m_memory_size & 0x3f);
}

void pvga1a_vga_device::memory_size_w(offs_t offset, u8 data)
{
	LOG("PR1 Memory Size W %02x\n", data);
	m_memory_size = data;
}

/*
 * [0x0c] PR2 Video Select
 *
 * x--- ---- M24 Mode Enable
 * -x-- ---- 6845 Compatiblity Mode
 * --x- -x-- Character Map Select
 * ---- -1-- \- also enables special underline effect (?)
 * ---x x--- Character Clock Period Control
 * ---0 0--- VGA 8/9 dots
 * ---0 1--- 7 dots
 * ---1 0--- 9 dots
 * ---1 1--- 10 dots
 * ---- --x- external clock select 3
 * ---- ---x Set horizontal sync timing (0) doubled?
 */
u8 pvga1a_vga_device::video_select_r(offs_t offset)
{
	LOG("PR2 Video Select R\n");
	return m_video_select;
}

void pvga1a_vga_device::video_select_w(offs_t offset, u8 data)
{
	LOG("PR2 Video Select W %02x\n", data);
	m_video_select = data;
}

/*
 * [0x0d] PR3 CRT Control [locks groups in CRTC]
 *
 * x--- ---- Lock VSYNC polarity
 * -x-- ---- Lock HSYNC polarity
 * --x- ---- Lock horizontal timing (group 0 & 4)
 * ---x ---- bit 9 of CRTC Start Memory Address
 * ---- x--- bit 8 of CRTC Start Memory Address
 * ---- -x-- CRT Control cursor start, cursor stop, preset row scan, maximum scan line x2 (??)
 * ---- --x- Lock vertical display enable end (group 1)
 * ---- ---x Lock vertical total/retrace (group 2 & 3)
 */
u8 pvga1a_vga_device::crtc_lock_r(offs_t offset)
{
	LOG("PR3 CRTC lock R\n");
	return m_crtc_lock;
}

void pvga1a_vga_device::crtc_lock_w(offs_t offset, u8 data)
{
	LOG("PR3 CRTC lock W\n", data);
	m_crtc_lock = data;
}

/*
 * [0x0e] PR4 Video Control
 *
 * x--- ---- BLNKN (0) enables external Video DAC
 * -x-- ---- Tristate HSYNC, VSYNC, BLNKN
 * --x- ---- Tristate VID7-VID0
 * ---x ---- Tristate Memory Control outputs
 * ---- x--- Disable CGA (unaffected by POR)
 * ---- -x-- Lock palette and overscan regs
 * ---- --x- Enable EGA compatible mode
 * ---- ---x Enable 640x400x8bpp
 */
u8 pvga1a_vga_device::video_control_r(offs_t offset)
{
	LOG("PR4 Video Control R\n");
	return m_video_control;
}

void pvga1a_vga_device::video_control_w(offs_t offset, u8 data)
{
	LOG("PR4 Video Control W %02x\n", data);
	m_video_control = data;
	svga.rgb8_en = BIT(data, 0);
}

/*
 * [0x0f] PR5 Lock/Status
 *
 * xxxx ---- MD7/MD4 config reads
 * ---- -xxx lock register
 * ---- -101 unlock, any other value locks r/w to the extensions
 */
u8 pvga1a_vga_device::ext_gc_status_r(offs_t offset)
{
	return m_ext_gc_unlock ? 0x05 : 0x00;
}

void pvga1a_vga_device::ext_gc_unlock_w(offs_t offset, u8 data)
{
	m_ext_gc_unlock = (data & 0x7) == 5;
	LOGLOCKED("PR5 %s state (%02x)\n", m_ext_gc_unlock ? "unlock" : "lock", data);
	m_ext_gc_view.select(m_ext_gc_unlock);
}

/**************************************
 *
 * Western Digital WD90C00
 *
 *************************************/

wd90c00_vga_device::wd90c00_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: pvga1a_vga_device(mconfig, WD90C00, tag, owner, clock)
	, m_ext_crtc_view(*this, "ext_crtc_view")
{
	m_crtc_space_config = address_space_config("crtc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(wd90c00_vga_device::crtc_map), this));
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(wd90c00_vga_device::gc_map), this));
}

void wd90c00_vga_device::device_reset()
{
	pvga1a_vga_device::device_reset();

	m_pr10_scratch = 0;
	m_ext_crtc_write_unlock = false;
	m_ext_crtc_view.select(0);
}

void wd90c00_vga_device::crtc_map(address_map &map)
{
	pvga1a_vga_device::crtc_map(map);
	map(0x29, 0x29).rw(FUNC(wd90c00_vga_device::ext_crtc_status_r), FUNC(wd90c00_vga_device::ext_crtc_unlock_w));
	map(0x2a, 0x3f).view(m_ext_crtc_view);
	m_ext_crtc_view[0](0x2a, 0x3f).lr8(
		NAME([this] (offs_t offset) {
			LOGLOCKED("Attempt to R ext. register offset %02x while locked\n", offset + 0x2a);
			return 0xff;
		})
	);
//	m_ext_crtc_view[1](0x2a, 0x2a) PR11 EGA Switches
	m_ext_crtc_view[1](0x2b, 0x2b).ram(); // PR12 scratch pad
//	m_ext_crtc_view[1](0x2c, 0x2c) PR13 Interlace H/2 Start
//	m_ext_crtc_view[1](0x2d, 0x2d) PR14 Interlace H/2 End
//	m_ext_crtc_view[1](0x2e, 0x2e) PR15 Misc Control 1
//	m_ext_crtc_view[1](0x2f, 0x2f) PR16 Misc Control 2
//	m_ext_crtc_view[1](0x30, 0x30) PR17 Misc Control 3
//	m_ext_crtc_view[1](0x31, 0x3f) <reserved>
}

/*
 * [0x29] PR10 Unlock PR11/PR17
 *
 * x--- x--- Read lock
 * 1--- 0--- Unlocks, any other write locks reading
 * -xxx ---- Scratch Pad
 * ---- -xxx Write lock
 * ---- -101 Unlocks, any other write locks writing
 */
u8 wd90c00_vga_device::ext_crtc_status_r(offs_t offset)
{
	return m_pr10_scratch | (m_ext_crtc_write_unlock ? 0x05 : 0x00);
}

void wd90c00_vga_device::ext_crtc_unlock_w(offs_t offset, u8 data)
{
	m_ext_crtc_write_unlock = (data & 0x7) == 5;
	LOGLOCKED("PR10 %s state (%02x)\n", m_ext_crtc_write_unlock ? "unlock" : "lock", data);
	// TODO: read unlock
	//m_ext_crtc_read_unlock = (data & 0x88) == 0x80;
	m_ext_crtc_view.select(m_ext_crtc_write_unlock);
	m_pr10_scratch = data & 0x70;
}
