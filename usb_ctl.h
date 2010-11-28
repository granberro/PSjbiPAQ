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
#include <asm/byteorder.h>
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
		if ((reg) == (val)) \
			break; \
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
		if ((reg) & (val)) \
			break; \
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
		if (!((reg) & (val))) \
			break; \
		if (i-- <= 0) { \
			printk( "%s [%d]: clear %#x of %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
	} while((reg) & (val)); \
}

#define UDC_flip(reg, val) { \
	int i = 10000; \
	(reg) = (val); \
	do { \
		(reg) = (val); \
		if (!((reg) & (val))) \
			break; \
		if (i-- <= 0) { \
			printk( "%s [%d]: flip %#x of %p (%#x) failed\n", \
				__FUNCTION__, __LINE__, (val), &(reg), (reg)); \
			break; \
		} \
	} while(((reg) & (val))); \
}

typedef void (*usb_callback_t)(int flag, int size);

// Start UDC running
int sa1100_usb_start( void );

// Immediately stop udc, fire off completion routines w/-EINTR
int sa1100_usb_stop( void ) ;

/* in usb_send.c */
int sa1100_usb_send(char *buf, int len, usb_callback_t callback);
void sa1100_usb_send_reset(void);

/* in usb_recev.c */
int sa1100_usb_recv(char *buf, int len, usb_callback_t callback);
void sa1100_usb_recv_reset(void);

//////////////////////////////////////////////////////////////////////////////
// Descriptor Management
//////////////////////////////////////////////////////////////////////////////

#define DescriptorHeader \
	__u8 bLength;        \
	__u8 bDescriptorType


// --- Device Descriptor -------------------

typedef struct {
	 DescriptorHeader;
	 __u16 bcdUSB;		   	/* USB specification revision number in BCD */
	 __u8  bDeviceClass;	/* USB class for entire device */
	 __u8  bDeviceSubClass; /* USB subclass information for entire device */
	 __u8  bDeviceProtocol; /* USB protocol information for entire device */
	 __u8  bMaxPacketSize0; /* Max packet size for endpoint zero */
	 __u16 idVendor;        /* USB vendor ID */
	 __u16 idProduct;       /* USB product ID */
	 __u16 bcdDevice;       /* vendor assigned device release number */
	 __u8  iManufacturer;	/* index of manufacturer string */
	 __u8  iProduct;        /* index of string that describes product */
	 __u8  iSerialNumber;	/* index of string containing device serial number */
	 __u8  bNumConfigurations; /* number fo configurations */
} __attribute__ ((packed)) device_desc_t;

// --- Configuration Descriptor ------------

typedef struct {
	 DescriptorHeader;
	 __u16 wTotalLength;	    /* total # of bytes returned in the cfg buf 4 this cfg */
	 __u8  bNumInterfaces;      /* number of interfaces in this cfg */
	 __u8  bConfigurationValue; /* used to uniquely ID this cfg */
	 __u8  iConfiguration;      /* index of string describing configuration */
	 __u8  bmAttributes;        /* bitmap of attributes for ths cfg */
	 __u8  MaxPower;		    /* power draw in 2ma units */
} __attribute__ ((packed)) config_desc_t;

// bmAttributes:
enum { USB_CONFIG_REMOTEWAKE=0x20, USB_CONFIG_SELFPOWERED=0x40,
	   USB_CONFIG_BUSPOWERED=0x80 };
// MaxPower:
#define USB_POWER( x)  ((x)>>1) /* convert mA to descriptor units of A for MaxPower */

// --- Interface Descriptor ---------------

typedef struct {
	 DescriptorHeader;
	 __u8  bInterfaceNumber;   /* Index uniquely identfying this interface */
	 __u8  bAlternateSetting;  /* ids an alternate setting for this interface */
	 __u8  bNumEndpoints;      /* number of endpoints in this interface */
	 __u8  bInterfaceClass;    /* USB class info applying to this interface */
	 __u8  bInterfaceSubClass; /* USB subclass info applying to this interface */
	 __u8  bInterfaceProtocol; /* USB protocol info applying to this interface */
	 __u8  iInterface;         /* index of string describing interface */
} __attribute__ ((packed)) intf_desc_t;

// --- Endpoint  Descriptor ---------------

typedef struct {
	 DescriptorHeader;
	 __u8  bEndpointAddress;  /* 0..3 ep num, bit 7: 0 = 0ut 1= in */
	 __u8  bmAttributes;      /* 0..1 = 0: ctrl, 1: isoc, 2: bulk 3: intr */
	 __u16 wMaxPacketSize;    /* data payload size for this ep in this cfg */
	 __u8  bInterval;         /* polling interval for this ep in this cfg */
} __attribute__ ((packed)) ep_desc_t;

// bEndpointAddress:
enum { USB_OUT= 0, USB_IN=1 };
#define USB_EP_ADDRESS(a,d) (((a)&0xf) | ((d) << 7))
// bmAttributes:
enum { USB_EP_CNTRL=0, USB_EP_BULK=2, USB_EP_INT=3 };

// --- String Descriptor -------------------

typedef struct {
	 DescriptorHeader;
	 __u16 bString[1];		  /* unicode string .. actaully 'n' __u16s */
} __attribute__ ((packed)) string_desc_t;

/*=======================================================
 * Handy helpers when working with above
 *
 */
// these are x86-style 16 bit "words" ...
#define make_word_c( w ) __constant_cpu_to_le16(w)
#define make_word( w )   __cpu_to_le16(w)

// descriptor types
enum { USB_DESC_DEVICE=1, USB_DESC_CONFIG=2, USB_DESC_STRING=3,
	   USB_DESC_INTERFACE=4, USB_DESC_ENDPOINT=5 };

/*=======================================================
 * Default descriptor layout for SA-1100 and SA-1110 UDC
 */

/* "config descriptor buffer" - that is, one config,
   ..one interface and 2 endpoints */
struct cdb {
	 config_desc_t cfg;
	 intf_desc_t   intf;
	 ep_desc_t     ep1;
	 ep_desc_t     ep2;
} __attribute__ ((packed));


/* all SA device descriptors */
typedef struct {
	 device_desc_t dev;   /* device descriptor */
	 struct cdb b;        /* bundle of descriptors for this cfg */
} __attribute__ ((packed)) desc_t;



void usb_get_hub_descriptor(desc_t * desc);
static void sa1100_set_address(__u32 address);
static inline void enable_resume_mask_suspend( void );
static inline void enable_suspend_mask_resume(void);
static void core_kicker(void);

#endif /* _USB_CTL_H */
