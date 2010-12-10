/*
 * psjbipaq.c -- PS3 Jailbreak exploit Gadget Driver for IPAQ H36xx-H38xx
 *
 * Copyright (C) Jail Breaker aka graNBerro
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 3, as published by the Free Software Foundation.
 *
 * This code is based in part on:
 *
 * Psfreedom Copyright (C) Youness Alaoui (KaKaRoTo)
 *
 * PSGroove
 * USB MIDI Gadget Driver, Copyright (C) 2006 Thumtronics Pty Ltd.
 * Gadget Zero driver, Copyright (C) 2003-2004 David Brownell.
 * USB Audio driver, Copyright (C) 2002 by Takashi Iwai.
 * USB MIDI driver, Copyright (C) 2002-2005 Clemens Ladisch.
 *
 */

#include <linux/module.h>  
#include <linux/kernel.h>
#include "usb_ctl.h"
#include "hub.c"
#include "usb_ctl.c"
#include "usb_send.c"
#include "usb_recv.c"
#include "usb_ep0.c"

static void state_machine_timeout(unsigned long data)
{
	int flags;

	if (eventa && eventa==machine_state) {
		debug = 1;		
	}	

	if (eventd && eventd==machine_state) {
		info = 0;
		debug = 0;
	}	
	
	// Device retry already satisfied
	if (device_retry == -1) {
		device_retry = 0;
		return;
	}	
	
	PRINTKI( "[%lu]Timer fired, status is %s.\n", (jiffies-start_time)*10, STATUS_STR (machine_state ));
	
	local_irq_save(flags);
	
	/* We need to delay switching the address because otherwise we will respond
	to the request (that triggered the port switch) with address 0. So we need
	to reply with the hub's address, THEN switch to 0. */
	if (switch_to_port_delayed >= 0) {
		switch_to_port (switch_to_port_delayed);
	}
	
	switch_to_port_delayed = -1;

	switch (machine_state) {
	case HUB_READY:
		machine_state = DEVICE1_WAIT_READY;
		hub_connect_port (1);
		break;
	case DEVICE1_READY:
		machine_state = DEVICE2_WAIT_READY;
		hub_connect_port (2);
		break;
	case DEVICE2_READY:
		machine_state = DEVICE3_WAIT_READY;
		hub_connect_port (3);
		break;
	case DEVICE3_READY:
		machine_state = DEVICE2_WAIT_DISCONNECT;
		hub_disconnect_port (2);
		break;
	case DEVICE2_DISCONNECTED:
		machine_state = DEVICE4_WAIT_READY;
		hub_connect_port (4);
		break;
	case DEVICE4_READY:
		device_retry = 5;
		machine_state = DEVICE5_WAIT_READY;
		hub_connect_port (5);
		break;
	case DEVICE5_CHALLENGED:
		// Unmask EP2 interrupts
		Ser0UDCCR = 0;
		jig_response_send ();
		break;
	case DEVICE5_READY:
		device_retry = 3;
		machine_state = DEVICE3_WAIT_DISCONNECT;
		hub_disconnect_port (3);
		break;
	case DEVICE3_DISCONNECTED:
		machine_state = DEVICE5_WAIT_DISCONNECT;
		hub_disconnect_port (5);
		break;
	case DEVICE5_DISCONNECTED:
		machine_state = DEVICE4_WAIT_DISCONNECT;
		hub_disconnect_port (4);
		break;
	case DEVICE4_DISCONNECTED:
		machine_state = DEVICE1_WAIT_DISCONNECT;
		hub_disconnect_port (1);
		break;
	case DEVICE1_DISCONNECTED:
		machine_state = DONE;
		printk("[%lu]It worked!!.\n", (jiffies-start_time)*10);
		del_timer (&state_machine_timer);
		timer_added = 0;
		break;
	default:
		break;
	}
	local_irq_restore(flags);
}

int init_module(void)
{
	int result;

	start_time = 0;
	result = usbctl_init();
	
	if (result)	{
		usbctl_exit();
		return result;
	}
	
	machine_state = INIT;
	state_machine_timer.function = state_machine_timeout;
	
	result = sa1100_usb_start();
	
	if (result)	{
		usbctl_exit();
		return result;
	}	

	return result;
}

void cleanup_module(void)
{
	ipaq_led_off (YELLOW_LED);
	ipaq_led_off (GREEN_LED);

	sa1100_usb_stop();
	usbctl_exit();
	printk("------------- PSJBiPAQ Closed ------------\n");
}  

MODULE_AUTHOR("graNBerro");
MODULE_LICENSE("GPL v3");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Enable debug mode");
MODULE_PARM(info, "i");
MODULE_PARM_DESC(info, "Enable info mode");
MODULE_PARM(port_delay, "i");
MODULE_PARM_DESC(port_delay, "port delay");
MODULE_PARM(eventa, "i");
MODULE_PARM_DESC(eventa, "event activate info");
MODULE_PARM(eventd, "i");
MODULE_PARM_DESC(eventd, "event deactivate info");