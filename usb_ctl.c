 /*
 *	Copyright (C) Compaq Computer Corporation, 1998, 1999
 *  Copyright (C) Extenex Corporation, 2001
 *
 *  usb_ctl.c
 *
 *  SA1100 USB controller core driver.
 *
 *  This file provides interrupt routing and overall coordination
 *  of the three endpoints in usb_ep0, usb_receive (1),  and usb_send (2).
 *
 *  Please see linux/Documentation/arm/SA1100/SA1100_USB for details.
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/tqueue.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include "sa1100_usb.h"
#include "usb_ctl.h"
#include "psfreedom_devices.h"

//////////////////////////////////////////////////////////////////////////////
// Globals
//////////////////////////////////////////////////////////////////////////////
static const char pszctl[] = "usbctl: ";
struct usb_info_t usbd_info;  /* global to ep0, usb_recv, usb_send */
static int tx_pktsize;
static int rx_pktsize;
static int timer_added = 0;
static void * desc_buf;
static int reset_repeat = 0;

#define USB_BUFSIZ 4096

/* The port1 configuration descriptor. dynamically loaded from procfs */
//u8 *port1_config_desc; // OJO
unsigned int port1_config_desc_size = 3840; // OJO

/* global write struct to keep write
   ..state around across interrupts */
static struct {
		unsigned char *p;
		int bytes_left;
} wr;

static void udc_int_hndlr(int irq, void *dev_id, struct pt_regs *regs)
{
	__u32 status = Ser0UDCSR;
	
	if (start_time==0) {
		start_time = jiffies;
	}	
	
	PRINTKD("[%lu]Status %d Mask %d\n", (jiffies-start_time)*10, status, Ser0UDCCR);
	
	/* ReSeT Interrupt Request - UDC has been reset */
	if (status & UDCSR_RSTIR)
	{
		Ser0UDCSR |= UDCSR_RSTIR;
		if (Ser0UDCSR & UDCSR_RSTIR)
			return;
		
		PRINTKD("[%lu]status limpio %d\n", (jiffies-start_time)*10, Ser0UDCSR);

		Ser0UDCCR = 0;

		if (reset_repeat == 0) {
			/* starting 20ms or so reset sequence now... */
			ep0_reset();  // just set state to idle
			ep1_reset();  // flush dma, clear false stall
			ep2_reset();  // flush dma, clear false stall

			reset_repeat = 1;

			// mask reset ints, they flood during sequence, enable 
			// suspend and resume
			//	Ser0UDCCR |= UDCCR_REM;    // mask reset //26-10-10 errata 29
	//		Ser0UDCCR &= ~(UDCCR_SUSIM | UDCCR_RESIM); // enable suspend and resume
		}

		return;		// <-- no reason to continue if resetting
	}
	
	reset_repeat = 0;
	// else we have done 	something other than reset, so be sure reset enabled
//26-10-10	UDC_clear( Ser0UDCCR, UDCCR_REM );	// OJO parece que en B5 esto debe ser siempre 1
	
	// /* RESume Interrupt Request */
	if ( status & UDCSR_RESIR )
	{
		UDC_clear(Ser0UDCCR, UDCCR_EIM | UDCCR_RIM | UDCCR_TIM | UDCCR_SUSIM | UDCCR_RESIM);
	}

	/* SUSpend Interrupt Request */
	if ( status & UDCSR_SUSIR )
	{
		ep0_reset();  // just set state to idle
		ep1_reset();  // flush dma, clear false stall
		ep2_reset();  // flush dma, clear false stall
	
		UDC_set( Ser0UDCCR, UDCCR_SUSIM);				
		UDC_clear(Ser0UDCCR, UDCCR_EIM | UDCCR_RIM | UDCCR_TIM | UDCCR_RESIM);
		printk("[%lu]Suspended: Status %d Port %d Address %d\n", (jiffies-start_time)*10, status, currentPort, Ser0UDCAR);
		machine_state=INIT;
		hub_interrupt_queued = 0;		
		currentPort = 0;
	}

	UDC_flip(Ser0UDCSR, status); // clear all pending sources

	if (status & UDCSR_RIR)
		ep1_int_hndlr(status);

	if (status & UDCSR_TIR)	
		ep2_int_hndlr(status);
	
	if (status & UDCSR_EIR) // OJO En el codigo original es el primero que se procesa
		ep0_int_hndlr();
		
	if (Ser0UDCAR != portAddress[currentPort]) {
		Ser0UDCAR = portAddress[currentPort];
		PRINTKI("[%lu]Apply address %d - %d\n", (jiffies-start_time)*10, portAddress[currentPort], Ser0UDCAR);			
	}
		
}

//////////////////////////////////////////////////////////////////////////////
// Public Interface
//////////////////////////////////////////////////////////////////////////////

/* Start running. Must have called usb_open (above) first */
int sa1100_usb_start( void ) {
	desc_t pd;
	
	usbd_info.state = USB_STATE_SUSPENDED;
	
	/* start UDC internal machinery running */
	udc_enable();
	
	udelay(100);

	/* clear stall - receiver seems to start stalled? 19Jan01ww */
	/* also clear other stuff just to be thurough 22Feb01ww */
	UDC_clear(Ser0UDCCS1, UDCCS1_FST | UDCCS1_RPE | UDCCS1_RPC );
	UDC_clear(Ser0UDCCS2, UDCCS2_FST | UDCCS2_TPE | UDCCS2_TPC );

	/* mask everything */
	UDC_write(Ser0UDCCR , 0xFC);

	usb_get_hub_descriptor(&pd);
	rx_pktsize = __le16_to_cpu( pd.b.ep1.wMaxPacketSize );	
	tx_pktsize = __le16_to_cpu( pd.b.ep2.wMaxPacketSize );
	
	/* flush DMA and fire through some -EAGAINs */
	ep1_init( usbd_info.dmach_rx );
	ep2_init( usbd_info.dmach_tx );

	/* clear all top-level sources */
	//printk("[%lu]status %d\n", (jiffies-start_time)*10, Ser0UDCSR);
	Ser0UDCSR = UDCSR_RSTIR | UDCSR_RESIR | UDCSR_EIR | UDCSR_RIR | UDCSR_TIR | UDCSR_SUSIR ;
	//printk("[%lu]status %d\n", (jiffies-start_time)*10, Ser0UDCSR);
	
	/* EXERIMENT - a short line in the spec says toggling this
	..bit diddles the internal state machine in the udc to
	..expect a suspend */
	Ser0UDCCR  |= UDCCR_RESIM; 
	/* END EXPERIMENT 10Feb01ww */

	UDC_write( Ser0UDCCR, UDCCR_SUSIM );
	
	return 0;
}

/* Stop USB core from running */
int sa1100_usb_stop( void )
{
	/* mask everything */
	Ser0UDCCR = 0xFC;
	ep1_reset();
	ep2_reset();
	udc_disable();

	return 0;
}

/*====================================================
 * Descriptor Manipulation.
 * Use these between open() and start() above to setup
 * the descriptors for your device.
 *
 */

/* get pointer to descriptor for hub */
void usb_get_hub_descriptor(desc_t * desc) {
	if (currentPort)
	{	
		printk( "[%lu]%sHub descriptor requested for port %d\n", (jiffies-start_time)*10, pszctl, currentPort );
	}
	else
	{
		desc->dev = hub_device_desc;
		desc->b.cfg = hub_config_desc; 
		desc->b.intf = hub_interface_desc;
		desc->b.ep1 = hub_endpoint_1;
		desc->b.ep2 = hub_endpoint_2;
	}

	return;
}

//////////////////////////////////////////////////////////////////////////////
// Private Helpers
//////////////////////////////////////////////////////////////////////////////

/* disable the UDC at the source */
static void udc_disable(void)
{
	UDC_set( Ser0UDCCR, UDCCR_UDD);
}

/*  enable the udc at the source */
static void udc_enable(void)
{
	UDC_clear(Ser0UDCCR, UDCCR_UDD);
}

// HACK DEBUG  3Mar01ww
// Well, maybe not, it really seems to help!  08Mar01ww
// static void core_kicker( void )
// {
	 // __u32 car = Ser0UDCAR;
	 // __u32 imp = Ser0UDCIMP;
	 // __u32 omp = Ser0UDCOMP;

	 // UDC_set( Ser0UDCCR, UDCCR_UDD );
	 // udelay( 300 );
	 // UDC_clear(Ser0UDCCR, UDCCR_UDD);

	 // Ser0UDCAR = car;
	 // Ser0UDCIMP = imp;
	 // Ser0UDCOMP = omp;
// }

//////////////////////////////////////////////////////////////////////////////
// Module Initialization and Shutdown
//////////////////////////////////////////////////////////////////////////////
/*
 * usbctl_init()
 * Module load time. Allocate dma and interrupt resources. Setup /proc fs
 * entry. Leave UDC disabled.
 */
int usbctl_init( void )
{
	int retval = 0;
	
	udc_disable();

	memset( &usbd_info, 0, sizeof( usbd_info ) );

	if (!desc_buf) {
		desc_buf = kmalloc(USB_BUFSIZ, GFP_ATOMIC);
	}
	
	if (!port_changed_buf) {
		port_changed_buf = kmalloc(1, GFP_ATOMIC);
	}

	/* setup rx dma */
	retval = sa1100_request_dma(DMA_Ser0UDCRd, "USB receive", NULL, NULL, &usbd_info.dmach_rx);
	if (retval) {
		printk("[%lu]%sunable to register for rx dma rc=%d\n", (jiffies-start_time)*10, pszctl, retval );
		goto err_rx_dma;
	}

	/* setup tx dma */
	retval = sa1100_request_dma(DMA_Ser0UDCWr, "USB transmit", NULL, NULL, &usbd_info.dmach_tx);
	if (retval) {
		printk("[%lu]%sunable to register for tx dma rc=%d\n", (jiffies-start_time)*10,pszctl,retval);
		goto err_tx_dma;
	}

	/* now allocate the IRQ. */
	retval = request_irq(IRQ_Ser0UDC, udc_int_hndlr, SA_INTERRUPT, "SA USB core", NULL);
	if (retval) {
		printk("[%lu]%sCouldn't request USB irq rc=%d\n", (jiffies-start_time)*10,pszctl, retval);
		goto err_irq;
	}

	return 0;

err_irq:
	sa1100_free_dma(usbd_info.dmach_tx);
	usbd_info.dmach_tx = 0;

err_tx_dma:
	sa1100_free_dma(usbd_info.dmach_rx);
	usbd_info.dmach_rx = 0;
err_rx_dma:
	return retval;
}

/*
 * usbctl_exit()
 * Release DMA and interrupt resources
 */
void usbctl_exit( void )
{
	udc_disable();
    sa1100_free_dma(usbd_info.dmach_rx);
    sa1100_free_dma(usbd_info.dmach_tx);
	
	if (desc_buf) {
		kfree(desc_buf);
	}

	if (port_changed_buf) {
		kfree(port_changed_buf);
	}	
	
	free_irq(IRQ_Ser0UDC, NULL);
}