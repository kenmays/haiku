#include "rdna4_nbio.h"
#include "driver.h"
#include <OS.h>

/*
 * NBIO v6.3.1 / Navi4 doorbell routing.
 *
 * Current Linux uses the nbif_4_10 GDC doorbell register aliases on
 * NBIO 7.11.4+.  The field encoding is unchanged from the generated
 * NBIO 6.3.x register description.
 */
static const uint32 RCC_DOORBELL_APER_EN = 0x00c0;
static const uint32 GDC_ENTRY0 = 0x4f0aeb;
static const uint32 GDC_ENTRY1 = 0x4f0aed;
static const uint32 GDC_ENTRY2 = 0x4f0aef;
static const uint32 GDC_ENTRY3 = 0x4f0af1;
static const uint32 GDC_ENTRY4 = 0x4f0af3;

static inline volatile uint32* R(rdna4_device& d, uint32 reg)
{
	return (volatile uint32*)(d.mmio + ((uint64)reg << 2));
}
static inline uint32 rd(rdna4_device& d, uint32 reg) { return *R(d, reg); }
static inline void wr(rdna4_device& d, uint32 reg, uint32 value)
{
	*R(d, reg) = value;
	(void)*R(d, reg);
}

static inline uint32 field(uint32 value, uint32 mask, uint32 shift, uint32 data)
{
	return (value & ~mask) | ((data << shift) & mask);
}

static uint32 range_value(uint32 value, uint32 enable, uint32 awid,
	uint32 offset, uint32 size, uint32 awaddr)
{
	value = field(value, 0x00000001, 0, enable);
	value = field(value, 0x0000003e, 1, awid);
	value = field(value, 0x0001ff80, 7, offset);
	value = field(value, 0x01fe0000, 17, size);
	value = field(value, 0xf0000000, 28, awaddr);
	return value;
}

status_t
rdna4_nbio_init(rdna4_device& d)
{
	if (d.mmio == NULL)
		return B_NO_INIT;

	/* Permit the PCIe doorbell BAR to reach the GDC client decoder. */
	uint32 aper = rd(d, RCC_DOORBELL_APER_EN);
	aper |= 1u;
	wr(d, RCC_DOORBELL_APER_EN, aper);

	/* GC doorbell clients: compute/MES/GFX and the graphics-side client. */
	wr(d, GDC_ENTRY0, 0x30000007u);
	wr(d, GDC_ENTRY3, 0x3000000du);

	/* SDMA0/1 use client port 2, AWID 0xe and address nibble 3. */
	uint32 sdma = range_value(rd(d, GDC_ENTRY2), 1, 0xe, 0x100, 20, 3);
	wr(d, GDC_ENTRY2, sdma);

	/* IH ring doorbell is client port 1, AWID 0 and address nibble 0. */
	uint32 ih = range_value(rd(d, GDC_ENTRY1), 1, 0, 0x1a0, 2, 0);
	wr(d, GDC_ENTRY1, ih);

	/* VCN5 ring 0 uses the 0x310 doorbell assignment. */
	uint32 vcn = range_value(rd(d, GDC_ENTRY4), 1, 4, 0x310, 8, 4);
	wr(d, GDC_ENTRY4, vcn);

	return B_OK;
}

void
rdna4_nbio_uninit(rdna4_device& d)
{
	if (d.mmio == NULL)
		return;
	uint32 aper = rd(d, RCC_DOORBELL_APER_EN);
	aper &= ~1u;
	wr(d, RCC_DOORBELL_APER_EN, aper);
}
