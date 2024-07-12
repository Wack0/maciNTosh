/*
 * Driver for HID devices ported from CoreBoot
 *
 * Copyright (C) 2014 BALATON Zoltan
 *
 * This file was part of the libpayload project.
 *
 * Copyright (C) 2008-2010 coresystems GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>
#include <stdio.h>
#include "usb.h"
#include "adb_kbd.h"

#ifdef CONFIG_DEBUG_USB
static const char *boot_protos[3] = { "(none)", "keyboard", "mouse" };
#endif
typedef enum { hid_proto_boot = 0, hid_proto_report = 1 } hid_proto;
enum { GET_REPORT = 0x1, GET_IDLE = 0x2, GET_PROTOCOL = 0x3, SET_REPORT =
		0x9, SET_IDLE = 0xa, SET_PROTOCOL = 0xb
};

typedef union {
	struct {
		u8 modifiers;
		u8 repeats;
		u8 keys[6];
	};
	u8 buffer[8];
} usb_hid_keyboard_event_t;

typedef struct {
	void* queue;
	hid_descriptor_t *descriptor;

	usb_hid_keyboard_event_t previous;
	int lastkeypress;
	int repeat_delay;
} usbhid_inst_t;

#define HID_INST(dev) ((usbhid_inst_t*)(dev)->data)

static void
usb_hid_destroy (usbdev_t *dev)
{
	if (HID_INST(dev)->queue) {
		int i;
		for (i = 0; i <= dev->num_endp; i++) {
			if (dev->endpoints[i].endpoint == 0)
				continue;
			if (dev->endpoints[i].type != INTERRUPT)
				continue;
			if (dev->endpoints[i].direction != IN)
				continue;
			break;
		}
		dev->controller->destroy_intr_queue(
				&dev->endpoints[i], HID_INST(dev)->queue);
		HID_INST(dev)->queue = NULL;
	}
	free (dev->data);
}

#define MOD_SHIFT    (1 << 0)
#define MOD_ALT      (1 << 1)
#define MOD_CTRL     (1 << 2)

#define KEYBOARD_REPEAT_MS	30
#define INITIAL_REPEAT_DELAY	10
#define REPEAT_DELAY		 2

static void
usb_hid_poll (usbdev_t *dev)
{
	usb_hid_keyboard_event_t current;
	const u8 *buf;

	while ((buf=dev->controller->poll_intr_queue (HID_INST(dev)->queue))) {
		memcpy(&current.buffer, buf, 8);
		//usb_hid_process_keyboard_event(HID_INST(dev), &current);
		KBDOnEvent((PUSB_KBD_REPORT)&current);
		HID_INST(dev)->previous = current;
	}
}

static void
usb_hid_set_idle (usbdev_t *dev, interface_descriptor_t *interface, u16 duration)
{
	dev_req_t dr;
	dr.data_dir = host_to_device;
	dr.req_type = class_type;
	dr.req_recp = iface_recp;
	dr.bRequest = SET_IDLE;
	dr.wValue = __cpu_to_le16((duration >> 2) << 8);
	dr.wIndex = __cpu_to_le16(interface->bInterfaceNumber);
	dr.wLength = 0;
	dev->controller->control (dev, OUT, sizeof (dev_req_t), &dr, 0, 0);
}

static void
usb_hid_set_protocol (usbdev_t *dev, interface_descriptor_t *interface, hid_proto proto)
{
	dev_req_t dr;
	dr.data_dir = host_to_device;
	dr.req_type = class_type;
	dr.req_recp = iface_recp;
	dr.bRequest = SET_PROTOCOL;
	dr.wValue = __cpu_to_le16(proto);
	dr.wIndex = __cpu_to_le16(interface->bInterfaceNumber);
	dr.wLength = 0;
	dev->controller->control (dev, OUT, sizeof (dev_req_t), &dr, 0, 0);
}

void
usb_hid_init (usbdev_t *dev)
{
	configuration_descriptor_t *cd = (configuration_descriptor_t*)dev->configuration;
	interface_descriptor_t *interface = (interface_descriptor_t*)(((char *) cd) + cd->bLength);

	if (interface->bInterfaceSubClass == hid_subclass_boot) {
		u8 countrycode = 0;
		usb_debug ("  supports boot interface..\n");
		usb_debug ("  it's a %s\n",
			boot_protos[interface->bInterfaceProtocol]);
		switch (interface->bInterfaceProtocol) {
		case hid_boot_proto_keyboard:
			dev->data = malloc (sizeof (usbhid_inst_t));
			if (!dev->data) {
				//printk("Not enough memory for USB HID device.\n");
				return;
                        }
			memset(&HID_INST(dev)->previous, 0x00,
			       sizeof(HID_INST(dev)->previous));
			usb_debug ("  configuring...\n");
			usb_hid_set_protocol(dev, interface, hid_proto_boot);
			usb_hid_set_idle(dev, interface, KEYBOARD_REPEAT_MS);
			usb_debug ("  activating...\n");
#if 0
			HID_INST (dev)->descriptor =
				(hid_descriptor_t *)
					get_descriptor(dev, gen_bmRequestType
					(device_to_host, standard_type, iface_recp),
					0x21, 0, 0);
			countrycode = HID_INST(dev)->descriptor->bCountryCode;
#endif
#if 0
			/* 35 countries defined: */
			if (countrycode > 35)
				countrycode = 0;
			usb_debug ("  Keyboard has %s layout (country code %02x)\n",
					countries[countrycode][0], countrycode);

			/* Set keyboard layout accordingly */
			usb_hid_set_layout(countries[countrycode][1]);
#endif

			// only add here, because we only support boot-keyboard HID devices
			dev->destroy = usb_hid_destroy;
			dev->poll = usb_hid_poll;
			int i;
			for (i = 0; i <= dev->num_endp; i++) {
				if (dev->endpoints[i].endpoint == 0)
					continue;
				if (dev->endpoints[i].type != INTERRUPT)
					continue;
				if (dev->endpoints[i].direction != IN)
					continue;
				break;
			}
			usb_debug ("  found endpoint %x for interrupt-in\n", i);
			/* 20 buffers of 8 bytes, for every 10 msecs */
			HID_INST(dev)->queue = dev->controller->create_intr_queue (&dev->endpoints[i], 8, 20, 10);
			//keycount = 0;
			usb_debug ("  configuration done.\n");
			break;
		default:
			usb_debug("NOTICE: HID interface protocol %d%s not supported.\n",
				 interface->bInterfaceProtocol,
				 (interface->bInterfaceProtocol == hid_boot_proto_mouse ?
				 " (USB mouse)" : ""));
			break;
		}
	}
}