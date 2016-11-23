/*
 * Agent proxy for android   
 * 	usb client specific stubs for the agent-proxy
 *
 * This file is based in part on 
 * 		Agent-Proxy and Android adb 
 *
 * Copyright (C) 2011 Sevencore, Inc.
 * Author: Joohyun Kyong <joohyun0115@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libusb-1.0/libusb.h> 

#include "android-agent-proxy.h"

static libusb_context *ctx = NULL;

struct usb_handle
{
	struct usb_handle            *prev;
	struct usb_handle            *next;

	struct libusb_device         *dev;
	struct libusb_device_handle  *devh;
	int                   interface;
	uint8_t               dev_bus;
	uint8_t               dev_addr;

	int                   zero_mask;
	unsigned char         end_point_address[2];
	char                  serial[128];

};

struct usb_handle android_uh;


#ifdef USBDEBUGLOG
int usb_debug = 1;
#else
int usb_debug = 0;
#endif

static struct usb_handle handle_list = {
	.prev = &handle_list,
	.next = &handle_list,
};

#define KGDB_CLASS              0xff
#define KGDB_SUBCLASS           0x50
#define KGDB_PROTOCOL           0x1

void usb_cleanup()
{
	libusb_exit(ctx);
}

void report_bulk_libusb_error(int r)
{
	switch (r) {
		case LIBUSB_ERROR_TIMEOUT:
			printf("Transfer timeout\n");
			break;

		case LIBUSB_ERROR_PIPE:
			printf("Control request is not supported\n");
			break;

		case LIBUSB_ERROR_OVERFLOW:
			printf("Device offered more data\n");
			break;

		case LIBUSB_ERROR_NO_DEVICE :
			printf("Device was disconnected\n");
			break;

		default:
			printf("Error %d during transfer\n", r);
			break;
	};
}

static int usb_bulk_write(struct usb_handle *uh, const void *data, int len)
{
	int r = 0;
	int transferred = 0;

	r = libusb_bulk_transfer(uh->devh, uh->end_point_address[1], (void *)data, len,
			&transferred, 0);

	if (r != 0) {
		if (usb_debug) {
			printf("usb_bulk_write(): ");
			report_bulk_libusb_error(r);
		}
		return r;
	}

	return (transferred);
}

static int usb_bulk_read(struct usb_handle *uh, void *data, int len)
{
	int r = 0;
	int transferred = 0;

	r = libusb_bulk_transfer(uh->devh, uh->end_point_address[0], data, len,
			&transferred, 100);

	if (r != 0) {
		if (usb_debug) {
			printf("usb_bulk_read(): ");
			report_bulk_libusb_error(r);
		}
		return r;
	}

	return (transferred);

}

int usb_close(struct usb_handle *h)
{
	printf("usb_close(): closing transport %p\n", h);

	h->next->prev = h->prev;
	h->prev->next = h->next;
	h->prev = NULL;
	h->next = NULL;

	libusb_release_interface(h->devh, h->interface);
	libusb_close(h->devh);
	libusb_unref_device(h->dev);


	free(h);

	return (0);
}

void usb_kick(struct usb_handle *h)
{
	printf("usb_cick(): kicking transport %p\n", h);

	h->next->prev = h->prev;
	h->prev->next = h->next;
	h->prev = NULL;
	h->next = NULL;

	libusb_release_interface(h->devh, h->interface);
	libusb_close(h->devh);
	libusb_unref_device(h->dev);
	free(h);
}

int is_kgdb_interface(int vid, int pid, int usb_class, int usb_subclass, int usb_protocol)
{ 
	if (usb_class == KGDB_CLASS && usb_subclass == KGDB_SUBCLASS &&
			usb_protocol == KGDB_PROTOCOL) {
		return 1;
	}

	return 0;
}

int check_usb_interface(const struct libusb_interface *interface, struct libusb_device_descriptor *desc, struct usb_handle *uh)
{    
	int e;

	if (interface->num_altsetting == 0) {
		if (usb_debug)
			printf("check_usb_interface(): No interface settings\n");
		return -1;
	}

	struct libusb_interface_descriptor *idesc = 
		(struct libusb_interface_descriptor *)&interface->altsetting[0];

	if (idesc->bNumEndpoints != 2) {
		if (usb_debug)
			printf("check_usb_interface(): Interface have not 2 endpoints, ignoring\n");
		return -1;
	}

	for (e = 0; e < (int) idesc->bNumEndpoints; e++) {
		struct libusb_endpoint_descriptor *edesc = 
			(struct libusb_endpoint_descriptor *)&idesc->endpoint[e];

		if (edesc->bmAttributes != LIBUSB_TRANSFER_TYPE_BULK) {
			if (usb_debug)
				printf("check_usb_interface(): Endpoint (%u) is not bulk (%u), ignoring\n",
					edesc->bmAttributes, LIBUSB_TRANSFER_TYPE_BULK);
			return -1;
		}

		if (edesc->bEndpointAddress & LIBUSB_ENDPOINT_IN)
			uh->end_point_address[0] = edesc->bEndpointAddress;
		else
			uh->end_point_address[1] = edesc->bEndpointAddress;

		/* aproto 01 needs 0 termination */
		if (idesc->bInterfaceProtocol == 0x01) {
			uh->zero_mask = edesc->wMaxPacketSize - 1;
			if (usb_debug)
				printf("check_usb_interface(): Forced Android interface protocol v.1\n");
		}
	}

	if (usb_debug)
		printf("check_usb_interface(): Device: %04x:%04x "
			"iclass: %x, isclass: %x, iproto: %x ep: %x/%x-> ",
			desc->idVendor, desc->idProduct, idesc->bInterfaceClass,
			idesc->bInterfaceSubClass, idesc->bInterfaceProtocol,
			uh->end_point_address[0], uh->end_point_address[1]);

	if (!is_kgdb_interface(desc->idVendor, desc->idProduct,
				idesc->bInterfaceClass, idesc->bInterfaceSubClass,
				idesc->bInterfaceProtocol))
	{
		if (usb_debug)
			printf("not matches\n");
		return -1;
	}

	if (usb_debug)
		printf("matches\n");
	return 1;
}

int check_usb_interfaces(struct libusb_config_descriptor *config,struct libusb_device_descriptor *desc, struct usb_handle *uh)
{  
	int i;

	for (i = 0; i < (int)config->bNumInterfaces; ++i) {
		if (check_usb_interface(&config->interface[i], desc, uh) != -1) {
			/* found some interface and saved information about it */
			if (usb_debug)
				printf("check_usb_interfaces(): Interface %d of %04x:%04x "
					"matches Android device\n", i, desc->idVendor,
					desc->idProduct);

			return  i;
		}
	}

	return -1;
}

int register_device(struct usb_handle *uh, const char *serial)
{
	if (usb_debug)
		printf("register_device(): Registering %p [%s] as USB transport\n",
			uh, serial);

	struct usb_handle *usb= NULL;

	usb = calloc(1, sizeof(struct usb_handle));
	memcpy(usb, uh, sizeof(struct usb_handle));
	strcpy(usb->serial, uh->serial);

	usb->next = &handle_list;
	usb->prev = handle_list.prev;
	usb->prev->next = usb;
	usb->next->prev = usb;

	return (1);
}

int already_registered(struct usb_handle *uh)
{
	struct usb_handle *usb= NULL;
	int exists = 0;


	for (usb = handle_list.next; usb != &handle_list; usb = usb->next) {
		if ((usb->dev_bus == uh->dev_bus) &&
				(usb->dev_addr == uh->dev_addr))
		{
			exists = 1;
			break;
		}
	}

	return exists;
}

int check_device(struct libusb_device *dev) 
{    
	int found = -1;
	char serial[256] = {0};

	struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config = NULL;

	int r = libusb_get_device_descriptor(dev, &desc);

	if (r != LIBUSB_SUCCESS) {
		printf("check_device(): Failed to get device descriptor\n");
		return -1;
	}

	if ((desc.idVendor == 0) && (desc.idProduct == 0))
		return -1;

	if (usb_debug)
		printf("check_device(): Probing usb device %04x:%04x\n",
			desc.idVendor, desc.idProduct);

	if (!is_kgdb_interface (desc.idVendor, desc.idProduct,
				KGDB_CLASS, KGDB_SUBCLASS, KGDB_PROTOCOL)) {
		printf("check_device(): Ignored due unknown vendor id\n");
		return -1;
	}

	android_uh.dev_bus = libusb_get_bus_number(dev);
	android_uh.dev_addr = libusb_get_device_address(dev);

	if (already_registered(&android_uh)) {
		printf("check_device(): Device (bus: %d, address: %d) "
				"is already registered\n", android_uh.dev_bus, android_uh.dev_addr);
		return -1;
	}

	if (usb_debug)
		printf("check_device(): Device bus: %d, address: %d\n",
			android_uh.dev_bus, android_uh.dev_addr);

	r = libusb_get_active_config_descriptor(dev, &config);

	if (r != 0) {
		if (r == LIBUSB_ERROR_NOT_FOUND) {
			printf("check_device(): Device %4x:%4x is unconfigured\n", 
					desc.idVendor, desc.idProduct);
			return -1;
		}

		printf("check_device(): Failed to get configuration for %4x:%4x\n",
				desc.idVendor, desc.idProduct);
		return -1;
	}

	if (config == NULL) {
		printf("check_device(): Sanity check failed after "
				"getting active config\n");
		return -1;
	}

	if (config->interface != NULL) {
		found = check_usb_interfaces(config, &desc, &android_uh);
	}

	/* not needed anymore */
	libusb_free_config_descriptor(config);

	r = libusb_open(dev, &android_uh.devh);
	android_uh.dev = dev;

	if (r != 0) {
		switch (r) {
			case LIBUSB_ERROR_NO_MEM:
				printf("check_device(): Memory allocation problem\n");
				break;

			case LIBUSB_ERROR_ACCESS:
				printf("check_device(): Permissions problem, "
						"current user priveleges are messed up?\n");
				break;

			case LIBUSB_ERROR_NO_DEVICE:
				printf("check_device(): Device disconected, bad cable?\n");
				break;

			default:
				printf("check_device(): libusb triggered error %d\n", r);
		}
		// skip rest
		found = -1;
	}

	if (found >= 0) {
		printf("check_device(): Device matches Android interface\n");
		// read the device's serial number
		memset(serial, 0, sizeof(serial));
		android_uh.interface = found;

		r = libusb_claim_interface(android_uh.devh, android_uh.interface);

		if (r < 0) {
			printf("check_device(): Failed to claim interface %d\n",
					android_uh.interface);

			goto fail;
		}

		return 0;
	}

	return 1;

fail:
	libusb_close(android_uh.devh);
	android_uh.devh = NULL;
	return -1;	
}

int check_device_connected(struct usb_handle *uh)
{
	int r = libusb_kernel_driver_active(uh->devh, uh->interface);

	if (r == LIBUSB_ERROR_NO_DEVICE)
		return 0;

	if (r < 0)
		return -1;

	return 1;
}

void kick_disconnected()
{
	struct usb_handle *usb= NULL;

	for (usb = handle_list.next; usb != &handle_list; usb = usb->next) {

		if (check_device_connected(usb) == 0) {
			printf("kick_disconnected(): Transport %p is not online anymore\n",
					usb);

			usb_kick(usb);
		}
	}
}

int  scan_usb_devices()
{
	int ret = -1;   
	struct libusb_device **devs= NULL;
	struct libusb_device *dev= NULL;
	ssize_t cnt = libusb_get_device_list(ctx, &devs);

	if (cnt < 0) {
		printf("scan_usb_devices(): Failed to get device list (error: %d)\n", (int)cnt);

		return ret;
	}

	int i = 0;

	while ((dev = devs[i++]) != NULL) {
		if (!check_device(dev)) {
			ret = 0;
			break;
		}
	}

	libusb_free_device_list(devs, 1);

	return ret;
}


void kgdbagent_usb_close()
{

	libusb_detach_kernel_driver(android_uh.devh, android_uh.interface);
	libusb_release_interface(android_uh.devh, android_uh.interface);
	libusb_close(android_uh.devh);
	android_uh.devh = NULL;
}

int  usb_portopen(struct port_st *port)
{
	int r = libusb_init(&ctx);

	if (r != LIBUSB_SUCCESS) {
		printf("Failed to init libusb\n");
	}

	/* initial device scan */
	return scan_usb_devices();
}


/* 
 * TCP specific routine for shutting down comunications
 */
void usb_portclose(struct port_st *port)
{
	kgdbagent_usb_close();

	port->sock = -1;
}

/* 
 * TCP specific routine for reading
 */

int usb_portread(struct port_st *port, char *buf, int size, int opts)
{
	opts = 0;

	return usb_bulk_read(&android_uh, buf, size);
}

/* 
 * TCP specific routine for reading
 */

int usb_portwrite(struct port_st *port, char *buf, int size, int opts)
{
	opts = 0;

	/* GDB CONTINUE ISSUE */
	if (!strcmp(buf, "$c#63"))
		return size;

	return usb_bulk_write(&android_uh, buf, size);
}

