/*
 *	Copyright (C)  Compaq Computer Corporation, 1998, 1999
 *	Copyright (C)  Extenex Corporation 2001
 *
 *  usb_ctl.h
 *
 *  PRIVATE interface used to share info among components of the SA-1100 USB
 *  core: usb_ctl, usb_ep0, usb_recv and usb_send. Clients of the USB core
 *  should use sa1100_usb.h.
 *
 */

#ifndef _USB_CTL_H
#define _USB_CTL_H

#include <asm/dma.h>  /* dmach_t */

/*
 * These states correspond to those in the USB specification v1.0
 * in chapter 8, Device Framework.
 */
enum { USB_STATE_NOTATTACHED=0, USB_STATE_ATTACHED=1,USB_STATE_POWERED=2,
	   USB_STATE_DEFAULT=3, USB_STATE_ADDRESS=4, USB_STATE_CONFIGURED=5,
	   USB_STATE_SUSPENDED=6};

struct usb_stats_t {
	 unsigned long ep0_fifo_write_failures;
	 unsigned long ep0_bytes_written;
	 unsigned long ep0_fifo_read_failures;
	 unsigned long ep0_bytes_read;
};

struct usb_info_t
{
	 char * client_name;
	 dma_regs_t *dmach_tx, *dmach_rx;
	 int state;
	 unsigned char address;
	 struct usb_stats_t stats;
};

/* in usb_ctl.c */
extern struct usb_info_t usbd_info;

typedef enum ep0_state {
    EP0_STATE_IDLE      = 0,
    EP0_STATE_IN        = 1,
    EP0_STATE_OUT       = 2
} ep0_state;

/*================================================
 * USB Protocol Stuff
 */

/* Request Codes   */
enum { GET_STATUS=0,         CLEAR_FEATURE=1,     SET_FEATURE=3,
	   SET_ADDRESS=5,        GET_DESCRIPTOR=6,	  SET_DESCRIPTOR=7,
	   GET_CONFIGURATION=8,  SET_CONFIGURATION=9, GET_INTERFACE=10,
	   SET_INTERFACE=11 };

// request types
enum { STANDARD_REQUEST=0, CLASS_REQUEST=2};

/* USB Device Requests */
typedef struct
{
    __u8 bmRequestType;
    __u8 bRequest;
    __u16 wValue;
    __u16 wIndex;
    __u16 wLength;
} usb_dev_request_t  __attribute__ ((packed));

/*
 * Function Prototypes
 */
enum { kError=-1, kEvSuspend=0, kEvReset=1,
	   kEvResume=2, kEvAddress=3, kEvConfig=4, kEvDeConfig=5 };
int usbctl_next_state_on_event( int event );
static void udc_int_hndlr(int, void *, struct pt_regs *);
static void udc_disable(void);
static void udc_enable(void);

/* endpoint zero */
void ep0_reset(void);
void ep0_int_hndlr(void);
/* "setup handlers" -- the main functions dispatched to by the
   .. isr. These represent the major "modes" of endpoint 0 operaton */
static void sh_setup_begin(void);				/* setup begin (idle) */
static void sh_write( void );      				/* writing data */
static void sh_write_with_empty_packet( void ); /* empty packet at end of xfer*/
/* called before both sh_write routines above */
static void common_write_preamble( void );

/* other subroutines */
static __u32  queue_and_start_write( void * p, int req, int act );
static void write_fifo( void );
static int read_fifo( usb_dev_request_t * p );
static void get_hub_descriptor( usb_dev_request_t * pReq );
static void get_device_descriptor( usb_dev_request_t * pReq );

enum { true = 1, false = 0 };
typedef int bool;

/* some voodo helpers  01Mar01ww */
static void set_cs_bits( __u32 set_bits );
static void set_de( void );
static void set_ipr( void );
static void set_ipr_and_de( void );
static bool clear_opr( void );

/* receiver */
int  ep1_recv(void);
int  ep1_init(dma_regs_t *chn);
void ep1_int_hndlr(int status);
void ep1_reset(void);
void ep1_stall(void);

/* xmitter */
void ep2_reset(void);
int  ep2_init(dma_regs_t *chn);
void ep2_int_hndlr(int status);
void ep2_stall(void);

#define UDC_write(reg, val) { \
	int i = 10000; \
	do { \
	  	(reg) = (val); \
		if (i-- <= 0) { \
			printk( "%s [%d]: write %#x to %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
	} while((reg) != (val)); \
}

#define UDC_set(reg, val) { \
	int i = 10000; \
	do { \
		(reg) |= (val); \
		if (i-- <= 0) { \
			printk( "%s [%d]: set %#x of %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
	} while(!((reg) & (val))); \
}

#define UDC_clear(reg, val) { \
	int i = 10000; \
	do { \
		(reg) &= ~(val); \
		if (i-- <= 0) { \
			printk( "%s [%d]: clear %#x of %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
	} while((reg) & (val)); \
}

#define UDC_wait_clear(reg, val) { \
	int i = 10000; \
	do { \
		if (i-- <= 0) { \
			printk( "%s [%d]: wait clear %#x of %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
		udelay(300); \
	} while((reg) & (val)); \
}

#define UDC_flip(reg, val) { \
	int i = 10000; \
	(reg) = (val); \
	do { \
		(reg) = (val); \
		if (i-- <= 0) { \
			printk( "%s [%d]: flip %#x of %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
	} while(((reg) & (val))); \
}

#define CHECK_ADDRESS { if ( Ser0UDCAR == 1 ) { printk("%s:%d I lost my address!!!\n",__FUNCTION__, __LINE__);}}
#endif /* _USB_CTL_H */
