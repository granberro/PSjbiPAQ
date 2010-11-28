/*
 * hub.c -- PS3 Jailbreak exploit
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 3, as published by the Free Software Foundation.
 *
 * This code is based in part on:
 *
 * PSFreedom
 * Copyright (C) Youness Alaoui (KaKaRoTo)
 *
 * PSGroove
 * USB MIDI Gadget Driver, Copyright (C) 2006 Thumtronics Pty Ltd.
 * Gadget Zero driver, Copyright (C) 2003-2004 David Brownell.
 * USB Audio driver, Copyright (C) 2002 by Takashi Iwai.
 * USB MIDI driver, Copyright (C) 2002-2005 Clemens Ladisch.
 * 
 */

#include "hub.h"
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/io.h>

// 1 == lots of trace noise,  0 = only "important' stuff
#define VERBOSITY 1

#if VERBOSITY
#define PRINTKD(fmt, args...) if (debug) { printk( fmt , ## args) ; }
#define PRINTKI(fmt, args...) if (info) { printk( fmt , ## args) ; }
#else
#define PRINTKD(fmt, args...)
#define PRINTKI(fmt, args...)
#endif

static int machine_state;
static int hub_interrupt_queued = 0;
static unsigned long start_time;
static int currentPort = 0;
/* The address of all ports (0 == hub) */
static u8 portAddress[7];
static u16 port_status[6] = { 0, 0, 0, 0, 0, 0 };
static u16 port_change[6] = { 0, 0, 0, 0, 0, 0 };
static int switch_to_port_delayed = -1;
static void * port_changed_buf;

static int debug = 0;
static int info = 1;
static int addr_delay = 300;
static int eventa = DEVICE4_READY;
static int eventd = 0;
static int tr = 0;
static int tf = 1;

/*
 * DESCRIPTORS ...
 */

/* B.1  Device Descriptor */
static const usb_device_descriptor hub_device_desc = {
  .bLength =		USB_DT_DEVICE_SIZE,
  .bDescriptorType =	USB_DT_DEVICE,
  .bcdUSB =	 cpu_to_le16(0x0200)	,
  .bDeviceClass =	USB_CLASS_HUB,
  .bDeviceSubClass =	0x00,
  .bDeviceProtocol =	0x01,
  .bMaxPacketSize0 = 0x08,
  .idVendor =		cpu_to_le16(DRIVER_VENDOR_NUM),
  .idProduct =		cpu_to_le16(DRIVER_PRODUCT_NUM),
  .bcdDevice =		cpu_to_le16(0x0100),
  .iManufacturer =	0,
  .iProduct =		0,
  .iSerialNumber = 0x00,
  .bNumConfigurations =	1
}; 

/* Hub Configuration Descriptor */
static const usb_config_descriptor hub_config_desc = {
  .bLength =		USB_DT_CONFIG_SIZE,
  .bDescriptorType =	USB_DT_CONFIG,
  .wTotalLength =   cpu_to_le16(0x0019),
  .bNumInterfaces =	1,
  .bConfigurationValue =  0x01,
  .iConfiguration =	0,
  .bmAttributes =	0xe0,
  .MaxPower =		0x32
};

/* Hub Interface Descriptor */
static const usb_interface_descriptor hub_interface_desc = {
  .bLength =		USB_DT_INTERFACE_SIZE,
  .bDescriptorType =	USB_DT_INTERFACE,
  .bInterfaceNumber =	0,
  .bAlternateSetting = 0x00,
  .bNumEndpoints =	1, 
  .bInterfaceClass =	USB_CLASS_HUB,
  .bInterfaceSubClass =	0,
  .bInterfaceProtocol = 0,
  .iInterface =		0
};

/* Hub endpoint Descriptor 1 (in)*/
/*static const usb_endpoint_descriptor hub_endpoint_desc = {
  .bLength =		USB_DT_ENDPOINT_SIZE,
  .bDescriptorType =	USB_DT_ENDPOINT,
  .bEndpointAddress =	0x81,
  .bmAttributes =	0x03,
  .wMaxPacketSize =	cpu_to_le16(0x08), // 0x0100;
  .bInterval =		0x0c // 12 	// frames -> 32 ms
};*/

/* Hub endpoint Descriptor 1 ( ep2in)*/
static const usb_endpoint_descriptor hub_endpoint_1 = {
  .bLength =		USB_DT_ENDPOINT_SIZE,
  .bDescriptorType =	USB_DT_ENDPOINT,
  .bEndpointAddress =	USB_EP_ADDRESS( 2, USB_IN ), // USB_EP_ADDRESS( 1, USB_OUT )
  .bmAttributes =	USB_EP_INT, 
  .wMaxPacketSize =	cpu_to_le16(1), // cpu_to_le16(8) ojo
  .bInterval =		12	// frames -> 32 ms
  //.bInterval =		24	// jbc
};

static const usb_endpoint_descriptor hub_endpoint_2 = {
  .bLength =		USB_DT_ENDPOINT_SIZE,
  .bDescriptorType =	USB_DT_ENDPOINT,
  .bEndpointAddress =	USB_EP_ADDRESS( 1, USB_OUT),  // USB_EP_ADDRESS( 2, USB_IN )
  .bmAttributes =	USB_EP_INT, 
  .wMaxPacketSize =	cpu_to_le16(8),
  .bInterval =		12	// frames -> 32 ms
};

static const u8 hub_header_desc[] = {
	0x09, 0x29, 0x06, 0xa9, 0x00, 0x32, 0x64, 0x00, 0xff
};

static void switch_to_port (unsigned int port)
{
	if (currentPort == port) {
		return;
	}
	
	currentPort = port;
	sa1100_set_address (portAddress[port]);
	PRINTKI( "[%lu]Switching to port %d. Address is %d (Ser0UDCAR=%d)\n", (jiffies-start_time)*10, port, portAddress[port], Ser0UDCAR);
}

static void hub_connect_port (unsigned int port)
{
	if (port == 0 || port > 6) {
		return;
	}
	//PRINTKD( "[%lu]Hub: Connect port %d\n", (jiffies-start_time)*10, port );
	switch_to_port (0);

	/* Here, we must enable the port directly, otherwise we might loose time
	with the host asking for the status a few more times, and waiting for it to
	be enabled, etc.. and we might miss the 5seconds window in which we need
	to connect the JIG */
	port_status[port-1] |= PORT_STAT_CONNECTION;
	port_status[port-1] |= PORT_STAT_ENABLE;
	port_change[port-1] |= PORT_STAT_C_CONNECTION;

	hub_port_changed ();
}

static void hub_port_changed ()
{
	u8 data = 0;
	int i;
	
	for (i = 0; i < 6; i++) {
		if (port_change[i] != 0)
			data |= 1 << (i+1);
	}

	if (data != 0) {
		int err = 0;

		if (hub_interrupt_queued) {
			printk( "hub_interrupt_transmit: Already queued a request\n");
			printk("[%lu]hub_interrupt_transmit: Already queued a request\n", (jiffies-start_time)*10);
			return;
		}
		PRINTKI( "[%lu]Hub:Transmitting interrupt byte 0x%X\n", (jiffies-start_time)*10, data);
		hub_interrupt_queued = 1;
		memcpy (port_changed_buf, &data, 1);
		err = sa1100_usb_send(port_changed_buf, 1, hub_interrupt_complete);
		if (err) {
			printk( "retcode %d\n", err);
		}
		// Unmask EP2 interrupts
		Ser0UDCCR = 0;
	} else {
		if (hub_interrupt_queued)	{
			printk( "hub_interrupt_transmit: pendiente usb_ep_dequeue\n");
		}
	}
	
}

static void hub_interrupt_complete(int flag, int size) {
	int flags;
	
	local_irq_save(flags);
	PRINTKI( "[%lu]Hub_interrupt_complete (status %d)\n",(jiffies-start_time)*10, flag);
	if (flag == 0)
	{
		//printk( "hub_interrupt_complete: ¿queda pendiente?\n");
		hub_interrupt_queued = 0;
		local_irq_restore(flags);
		return;
	}
	else
	{
		printk( "hub_interrupt_complete con error?: flag %d, size %d\n", flag, size);
	}
		
	local_irq_restore(flags);

	// Mask EP2 interrupts
	Ser0UDCCR = UDCCR_TIM;
}

static void hub_disconnect_port (unsigned int port)
{
  if (port == 0 || port > 6)
    return;

  switch_to_port (0);
  port_status[port-1] &= ~PORT_STAT_CONNECTION;
  port_status[port-1] &= ~PORT_STAT_ENABLE;
  port_change[port-1] |= PORT_STAT_C_CONNECTION;
  hub_port_changed ();
}

/* Send the challenge response */
static void jig_response_send (void)
{
printk( "jig_response_send pendiente\n");
/*  struct usb_ep *ep = dev->in_ep;

  if (!ep)
    return;

  if (!req)
    req = alloc_ep_req(ep, 8);

  if (!req) {
    ERROR(dev, "hub_interrupt_transmit: alloc_ep_request failed\n");
    return;
  }

  req->complete = jig_response_complete;

  memcpy (req->buf, jig_response + dev->response_len, 8);
  req->length = 8;

  usb_ep_queue(ep, req, GFP_ATOMIC); */
}