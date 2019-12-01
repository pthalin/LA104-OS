/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2015 Robin Kreis <r.kreis@uni-bremen.de>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/tools.h>
#include <libopencm3/stm32/st_usbfs.h>
#include <libopencm3/usb/usbd.h>
#include "../../usb/usb_private.h"
#include "st_usbfs_core.h"

/* TODO - can't these be inside the impls, not globals from the core? */
uint8_t st_usbfs_force_nak[8];
struct _usbd_device st_usbfs_dev;

void st_usbfs_set_address(usbd_device *dev, uint8_t addr)
{
	(void)dev;
	/* Set device address and enable. */
	SET_REG(USB_DADDR_REG, (addr & USB_DADDR_ADDR) | USB_DADDR_EF);
}

/**
 * Set the receive buffer size for a given USB endpoint.
 *
 * @param ep Index of endpoint to configure.
 * @param size Size in bytes of the RX buffer.
 */
void st_usbfs_set_ep_rx_bufsize(usbd_device *dev, uint8_t ep, uint32_t size)
{
	(void)dev;
	if (size > 62) {
		if (size & 0x1f) {
			size -= 32;
		}
		USB_SET_EP_RX_COUNT(ep, (size << 5) | 0x8000);
	} else {
		if (size & 1) {
			size++;
		}
		USB_SET_EP_RX_COUNT(ep, size << 10);
	}
}

void st_usbfs_ep_setup(usbd_device *dev, uint8_t addr, uint8_t type,
		uint16_t max_size,
		void (*callback) (usbd_device *usbd_dev,
		uint8_t ep))
{
	/* Translate USB standard type codes to STM32. */
	const uint16_t typelookup[] = {
		[USB_ENDPOINT_ATTR_CONTROL] = USB_EP_TYPE_CONTROL,
		[USB_ENDPOINT_ATTR_ISOCHRONOUS] = USB_EP_TYPE_ISO,
		[USB_ENDPOINT_ATTR_BULK] = USB_EP_TYPE_BULK,
		[USB_ENDPOINT_ATTR_INTERRUPT] = USB_EP_TYPE_INTERRUPT,
	};
	uint8_t dir = addr & 0x80;
	addr &= 0x7f;

	/* Assign address. */
	USB_SET_EP_ADDR(addr, addr);
	USB_SET_EP_TYPE(addr, typelookup[type]);

	if (dir || (addr == 0)) {
		USB_SET_EP_TX_ADDR(addr, dev->pm_top);
		if (callback) {
			dev->user_callback_ctr[addr][USB_TRANSACTION_IN] =
			    (void *)callback;
		}
		USB_CLR_EP_TX_DTOG(addr);
		USB_SET_EP_TX_STAT(addr, USB_EP_TX_STAT_NAK);
		dev->pm_top += max_size;
	}

	if (!dir) {
		USB_SET_EP_RX_ADDR(addr, dev->pm_top);
		st_usbfs_set_ep_rx_bufsize(dev, addr, max_size);
		if (callback) {
			dev->user_callback_ctr[addr][USB_TRANSACTION_OUT] =
			    (void *)callback;
		}
		USB_CLR_EP_RX_DTOG(addr);
		USB_SET_EP_RX_STAT(addr, USB_EP_RX_STAT_VALID);
		dev->pm_top += max_size;
	}
}

void st_usbfs_endpoints_reset(usbd_device *dev)
{
	int i;

	/* Reset all endpoints. */
	for (i = 1; i < 8; i++) {
		USB_SET_EP_TX_STAT(i, USB_EP_TX_STAT_DISABLED);
		USB_SET_EP_RX_STAT(i, USB_EP_RX_STAT_DISABLED);
	}
	dev->pm_top = USBD_PM_TOP + (2 * dev->desc->bMaxPacketSize0);
}

void st_usbfs_ep_stall_set(usbd_device *dev, uint8_t addr,
				   uint8_t stall)
{
	(void)dev;
	if (addr == 0) {
		USB_SET_EP_TX_STAT(addr, stall ? USB_EP_TX_STAT_STALL :
				   USB_EP_TX_STAT_NAK);
	}

	if (addr & 0x80) {
		addr &= 0x7F;

		USB_SET_EP_TX_STAT(addr, stall ? USB_EP_TX_STAT_STALL :
				   USB_EP_TX_STAT_NAK);

		/* Reset to DATA0 if clearing stall condition. */
		if (!stall) {
			USB_CLR_EP_TX_DTOG(addr);
		}
	} else {
		/* Reset to DATA0 if clearing stall condition. */
		if (!stall) {
			USB_CLR_EP_RX_DTOG(addr);
		}

		USB_SET_EP_RX_STAT(addr, stall ? USB_EP_RX_STAT_STALL :
				   USB_EP_RX_STAT_VALID);
	}
}

uint8_t st_usbfs_ep_stall_get(usbd_device *dev, uint8_t addr)
{
	(void)dev;
	if (addr & 0x80) {
		if ((*USB_EP_REG(addr & 0x7F) & USB_EP_TX_STAT) ==
		    USB_EP_TX_STAT_STALL) {
			return 1;
		}
	} else {
		if ((*USB_EP_REG(addr) & USB_EP_RX_STAT) ==
		    USB_EP_RX_STAT_STALL) {
			return 1;
		}
	}
	return 0;
}

void st_usbfs_ep_nak_set(usbd_device *dev, uint8_t addr, uint8_t nak)
{
	(void)dev;
	/* It does not make sense to force NAK on IN endpoints. */
	if (addr & 0x80) {
		return;
	}

	st_usbfs_force_nak[addr] = nak;

	if (nak) {
		USB_SET_EP_RX_STAT(addr, USB_EP_RX_STAT_NAK);
	} else {
		USB_SET_EP_RX_STAT(addr, USB_EP_RX_STAT_VALID);
	}
}

uint16_t st_usbfs_ep_write_packet(usbd_device *dev, uint8_t addr,
				     const void *buf, uint16_t len)
{
	(void)dev;
	addr &= 0x7F;

	if ((*USB_EP_REG(addr) & USB_EP_TX_STAT) == USB_EP_TX_STAT_VALID) {
		return 0;
	}

	st_usbfs_copy_to_pm(USB_GET_EP_TX_BUFF(addr), buf, len);
	USB_SET_EP_TX_COUNT(addr, len);
	USB_SET_EP_TX_STAT(addr, USB_EP_TX_STAT_VALID);

	return len;
}

uint16_t st_usbfs_ep_read_packet(usbd_device *dev, uint8_t addr,
					 void *buf, uint16_t len)
{
	(void)dev;
	if ((*USB_EP_REG(addr) & USB_EP_RX_STAT) == USB_EP_RX_STAT_VALID) {
		return 0;
	}

	len = MIN(USB_GET_EP_RX_COUNT(addr) & 0x3ff, len);
	st_usbfs_copy_from_pm(buf, USB_GET_EP_RX_BUFF(addr), len);
	USB_CLR_EP_RX_CTR(addr);

	if (!st_usbfs_force_nak[addr]) {
		USB_SET_EP_RX_STAT(addr, USB_EP_RX_STAT_VALID);
	}

	return len;
}

void st_usbfs_poll(usbd_device *dev)
{
	uint16_t istr = *USB_ISTR_REG;

	if (istr & USB_ISTR_RESET) {
		USB_CLR_ISTR_RESET();
		dev->pm_top = USBD_PM_TOP;
		_usbd_reset(dev);
		return;
	}

	if (istr & USB_ISTR_CTR) {
		uint8_t ep = istr & USB_ISTR_EP_ID;
		bool out = (istr & USB_ISTR_DIR) ? true : false;
		uint8_t type;
		uint16_t epreg = *USB_EP_REG(ep);

		/* If DIR is set in USB_ISTR, we must check if CTR_TX is set in
		 * the EP reg and if control_state.state is LAST_DATA_IN we
		 * must deal with the IN transaction first. */
		if ((ep == 0x00) && out && (epreg & USB_EP_TX_CTR) &&
			dev->control_state.state == LAST_DATA_IN) {
			type = USB_TRANSACTION_IN;
		} else if (out) { /* OUT or SETUP transaction */
			type = (epreg & USB_EP_SETUP) ? USB_TRANSACTION_SETUP : USB_TRANSACTION_OUT;
		} else { /* IN transaction */
			type = USB_TRANSACTION_IN;
			USB_CLR_EP_TX_CTR(ep);
		}

		if (dev->user_callback_ctr[ep][type]) {
			dev->user_callback_ctr[ep][type] (dev, ep);
		} else {
			USB_CLR_EP_RX_CTR(ep);
		}
	}

	if (istr & USB_ISTR_SUSP) {
		USB_CLR_ISTR_SUSP();
		if (dev->user_callback_suspend) {
			dev->user_callback_suspend();
		}
	}

	if (istr & USB_ISTR_WKUP) {
		USB_CLR_ISTR_WKUP();
		if (dev->user_callback_resume) {
			dev->user_callback_resume();
		}
	}

	if (istr & USB_ISTR_SOF) {
		USB_CLR_ISTR_SOF();
		if (dev->user_callback_sof) {
			dev->user_callback_sof();
		}
	}

	if (dev->user_callback_sof) {
		*USB_CNTR_REG |= USB_CNTR_SOFM;
	} else {
		*USB_CNTR_REG &= ~USB_CNTR_SOFM;
	}
}