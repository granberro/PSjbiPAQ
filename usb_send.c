/*
 * Generic xmit layer for the SA1100 USB client function
 * Copyright (c) 2001 by Nicolas Pitre
 *
 * This code was loosely inspired by the original version which was
 * Copyright (c) Compaq Computer Corporation, 1998-1999
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This is still work in progress...
 *
 * Please see linux/Documentation/arm/SA1100/SA1100_USB for details.
 * 15/03/2001 - ep2_start now sets UDCAR to overcome something that is hardware
 * 		bug, I think. green@iXcelerator.com
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <asm/hardware.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include "usb_ctl.h"

//#define SA1100_USB_DMA_WORKAROUND
#ifdef SA1100_USB_DMA_WORKAROUND
typedef struct {
  volatile u_long ddar;
  volatile u_long set_dcsr;
  volatile u_long clr_dcsr;
  volatile u_long rd_dcsr;
  volatile dma_addr_t dbsa;
  volatile u_long dbta;
  volatile dma_addr_t dbsb;
  volatile u_long dbtb;
} tx_dma_regs_t;
#endif

static char *ep2_buf;
static int ep2_len;
static int ep2_curdmalen;
static int ep2_remain;
static usb_callback_t ep2_callback;
static dma_addr_t ep2_dma;
static dma_addr_t ep2_curdmapos;
static dma_regs_t *dmachn_tx;

#ifdef SA1100_USB_DMA_WORKAROUND
static tx_dma_regs_t *tx_dma_regs;
#endif

/* set feature stall executing, async */
void ep2_stall( void )
{
	UDC_set( Ser0UDCCS2, UDCCS2_FST );  /* force stall at UDC */
}

#ifdef SA1100_USB_DMA_WORKAROUND
/* The SA1100 USB transmit fifo seems to only work reliably when the
   DMA that feeds it runs in a quiet system.  Or something like that.
   As a workaround, we wait for the DMA to finish before doing
   anything else.  We program the DMA ourselves instead of using
   sa1100_dma_queue_buffer since it is simpler that way.
                                                - mvo@zagadka.de
*/

static void ep2_do_dma (void)
{
  int b_active = tx_dma_regs->rd_dcsr & DCSR_BIU;

  if (b_active)
    {
      tx_dma_regs->clr_dcsr = DCSR_STRTB | DCSR_RUN;
      tx_dma_regs->dbsb = ep2_curdmapos;
      tx_dma_regs->dbtb = ep2_curdmalen;
      tx_dma_regs->set_dcsr = DCSR_STRTB | DCSR_RUN;
      while (!(tx_dma_regs->rd_dcsr & DCSR_DONEB))
	;
    }
  else
    {
      tx_dma_regs->clr_dcsr = DCSR_STRTA | DCSR_RUN;
      tx_dma_regs->dbsa = ep2_curdmapos;
      tx_dma_regs->dbta = ep2_curdmalen;
      tx_dma_regs->set_dcsr = DCSR_STRTA | DCSR_RUN;
      while (!(tx_dma_regs->rd_dcsr & DCSR_DONEA))
	;
    }
}
#endif

static void ep2_start(void)
{
	if (!ep2_len)
		return;
	
	ep2_curdmalen = tx_pktsize;
	if (ep2_curdmalen > ep2_remain)
		ep2_curdmalen = ep2_remain;
	
	/* must do this _before_ queue buffer.. */
	UDC_flip( Ser0UDCCS2,UDCCS2_TPC );  /* stop NAKing IN tokens */
	UDC_write( Ser0UDCIMP, ep2_curdmalen-1 );

	/* Remove if never seen...8Mar01ww */
	{
		 int massive_attack = 20;
		 while ( Ser0UDCIMP != ep2_curdmalen-1 && massive_attack-- ) {
			  printk( "usbsnd: Oh no you don't! Let me spin..." );
			  udelay( 500 );
			  printk( "and try again...\n" );
			  UDC_write( Ser0UDCIMP, ep2_curdmalen-1 );
		 }
		 if ( massive_attack != 20 ) {
			  if ( Ser0UDCIMP != ep2_curdmalen-1 )
				   printk( "usbsnd: Massive attack FAILED :-( %d\n",
						   20 - massive_attack );
			  else
				   printk( "usbsnd: Massive attack WORKED :-) %d\n",
						   20 - massive_attack );
		 }
	}
	/* End remove if never seen... 8Mar01ww */

	Ser0UDCAR = portAddress[currentPort]; // fighting stupid silicon bug

	// was this:
	// sa1100_dma_queue_buffer(dmachn_tx, NULL, ep2_curdmapos, ep2_curdmalen);
#ifdef SA1100_USB_DMA_WORKAROUND
	ep2_do_dma ();
#else
	sa1100_start_dma(dmachn_tx, ep2_curdmapos, ep2_curdmalen);
#endif
}

static void ep2_done(int flag)
{
	int size = ep2_len - ep2_remain;
	if (ep2_len) {
		pci_unmap_single(NULL, ep2_dma, ep2_len, PCI_DMA_TODEVICE);
		ep2_len = 0;
		if (ep2_callback)
			ep2_callback(flag, size);
	}
}

int ep2_init(dma_regs_t *chn)
{
	dmachn_tx = chn;

#ifdef SA1100_USB_DMA_WORKAROUND
	tx_dma_regs = (tx_dma_regs_t *)dmachn_tx;
#endif
	sa1100_clear_dma(dmachn_tx);
	ep2_done(-EAGAIN);
	return 0;
}

void ep2_reset(void)
{
	if (currentPort) {
		tx_pktsize = 8; // OJO
	}
	else
	{
		tx_pktsize = 1;
	}
	
	UDC_clear(Ser0UDCCS2, UDCCS2_FST);
	sa1100_clear_dma(dmachn_tx);
	ep2_done(-EINTR);
}

void ep2_int_hndlr()
{
	int status = Ser0UDCCS2;

	if (Ser0UDCAR != portAddress[currentPort]) // check for stupid silicon bug.
		Ser0UDCAR = portAddress[currentPort];

	//UDC_flip(Ser0UDCCS2, UDCCS2_SST);
	UDC_flip(Ser0UDCCS2, UDCCS2_SST | UDCCS2_TPC);

	if (status & UDCCS2_TPC) {
		sa1100_clear_dma(dmachn_tx);

		if (status & (UDCCS2_TPE | UDCCS2_TUR)) {
			printk("usb_send: transmit error %x\n", status);
			ep2_done(-EIO);
		} else {
#if 1 // 22Feb01ww/Oleg
			ep2_curdmapos += ep2_curdmalen;
			ep2_remain -= ep2_curdmalen;
#else
			ep2_curdmapos += Ser0UDCIMP + 1; // this is workaround
			ep2_remain -= Ser0UDCIMP + 1;    // for case when setting of Ser0UDCIMP was failed
#endif

			if (ep2_remain != 0) {
				ep2_start();
			} else {
				ep2_done(0);
			}
		}
	} else {
		PRINTKD("usb_send: Not TPC: UDCCS2 = %x\n", status);
	}
}

int sa1100_usb_send(char *buf, int len, usb_callback_t callback)
{
	int flags;
	
	if (usbd_info.state != USB_STATE_CONFIGURED)
		return -ENODEV;

	if (ep2_len)
		return -EBUSY;

	local_irq_save(flags);
	ep2_buf = buf;
	ep2_len = len;
	ep2_dma = pci_map_single(NULL, buf, len, PCI_DMA_TODEVICE);
	ep2_callback = callback;
	ep2_remain = len;
	ep2_curdmapos = ep2_dma;
	ep2_start();
	local_irq_restore(flags);
	return 0;
}