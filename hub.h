/*
 * hub.h -- USB HUB definitions.
 *
 * Copyright (C) Youness Alaoui (KaKaRoTo)
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 3, as published by the Free Software Foundation.
 *
 * This file holds USB constants and structures defined
 * by the USB Device Class Definition for HUB Devices.
 *
 * This code is based in part on:
 *
 * USB MIDI Gadget Driver, Copyright (C) 2006 Thumtronics Pty Ltd.
 */

#ifndef __LINUX_USB_HUB_H
#define __LINUX_USB_HUB_H

#include <linux/timer.h>
#include <linux/types.h>

/* 11.23.2.1  Class-Specific AC Interface Descriptor */
typedef struct {
	__u8  bLength;			/* 8+n */
	__u8  bDescriptorType;		/* USB_DT_CS_HUB */
	__u8  bNbrPorts;		/* n */
	__u16 wHubCharacteristics;	/* hub characteristics */
	__u8  bPwrOn2PwrGood;		/* ? */
	__u8  bHubContrCurrent;		/* ? */
	__u8  DeviceRemovable;		/* [n/8] */
	__u8  PortPwrCtrlMask;		/* [n/8] */
} __attribute__ ((packed)) usb_hub_header_descriptor;

static struct timer_list state_machine_timer;
#define msecs_to_jiffies(ms) (((ms)*HZ+999)/1000)
#define SET_TIMER(ms)  PRINTKI( "[%lu]Setting timer to %d ms\n", (jiffies-start_time)*10, ms );  \
mod_timer (&state_machine_timer, jiffies + msecs_to_jiffies(ms))

#define USB_DT_HUB_HEADER_SIZE(n)	(sizeof(struct usb_hub_header_descriptor))
#define USB_DT_CS_HUB 0x29
#define PORT_STAT_CONNECTION	0x0001
#define PORT_STAT_ENABLE	0x0002
#define PORT_STAT_RESET		0x0010
#define PORT_STAT_POWER		0x0100
#define PORT_STAT_LOW_SPEED	0x0200
#define PORT_STAT_HIGH_SPEED	0x0400
#define PORT_STAT_C_CONNECTION	0x0001
#define PORT_STAT_C_RESET	0x0010

/* Taking first HUB vendor/product ids from http://www.linux-usb.org/usb.ids
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define DRIVER_VENDOR_NUM	0xaaaa		/* Atmel Corp */
#define DRIVER_PRODUCT_NUM	0xcccc		/* 4-Port Hub */
#define USB_CLASS_HUB 0x09
#define usb_device_descriptor device_desc_t
#define USB_DT_DEVICE USB_DESC_DEVICE
#define USB_DT_DEVICE_SIZE sizeof(device_desc_t)
#define usb_config_descriptor config_desc_t
#define USB_DT_CONFIG_SIZE sizeof(config_desc_t)
#define USB_DT_CONFIG USB_DESC_CONFIG
#define USB_DT_STRING			0x03
#define usb_interface_descriptor intf_desc_t
#define USB_DT_INTERFACE_SIZE sizeof(intf_desc_t)
#define USB_DT_INTERFACE USB_DESC_INTERFACE 
#define usb_endpoint_descriptor ep_desc_t
#define USB_DT_ENDPOINT_SIZE sizeof(ep_desc_t)
#define USB_DT_ENDPOINT USB_DESC_ENDPOINT

enum { 
  INIT,
  HUB_READY,
  DEVICE1_WAIT_READY,
  DEVICE1_READY,
  DEVICE1_WAIT_DISCONNECT,
  DEVICE1_DISCONNECTED,
  DEVICE2_WAIT_READY,
  DEVICE2_READY,
  DEVICE2_WAIT_DISCONNECT,
  DEVICE2_DISCONNECTED,
  DEVICE3_WAIT_READY,
  DEVICE3_READY,
  DEVICE3_WAIT_DISCONNECT,
  DEVICE3_DISCONNECTED,
  DEVICE4_WAIT_READY,
  DEVICE4_READY,
  DEVICE4_WAIT_DISCONNECT,
  DEVICE4_DISCONNECTED,
  DEVICE5_WAIT_READY,
  DEVICE5_CHALLENGED,
  DEVICE5_READY,
  DEVICE5_WAIT_DISCONNECT,
  DEVICE5_DISCONNECTED,
  DONE,
};

static void hub_port_changed(void);
static void hub_disconnect_port (unsigned int port);
static void jig_response_send (void);
static void hub_interrupt_complete(int flag, int size);
static void state_machine_timeout(unsigned long data);

struct hub_port {
  u16 status;
  u16 change;
};
#endif /* __LINUX_USB_HUB_H */
