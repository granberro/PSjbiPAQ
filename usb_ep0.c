/*
 * Copyright (C) Extenex Corporation 2001
 * Much folklore gleaned from original code:
 *    Copyright (C) Compaq Computer Corporation, 1998, 1999
 *
 *  usb_ep0.c - SA1100 USB controller driver.
 *              Endpoint zero management
 *
 *  Please see:
 *    linux/Documentation/arm/SA1100/SA1100_USB
 *  for details. (Especially since Intel docs are full of
 *  errors about ep0 operation.) ward.willats@extenex.com.
 *
 * Intel also has a "Universal Serial Bus Client Device
 * Validation for the StrongARM SA-1100 Microprocessor"
 * document, which has flow charts and assembler test driver,
 * but be careful, since it is just for validation and not
 * a "real world" solution.
 *
 * A summary of three types of data-returning setups:
 *
 * 1. Setup request <= 8 bytes. That is, requests that can
 *    be fullfilled in one write to the FIFO. DE is set
 *    with IPR in queue_and_start_write(). (I don't know
 *    if there really are any of these!)
 *
 * 2. Setup requests > 8 bytes (requiring more than one
 *    IN to get back to the host), and we have at least
 *    as much or more data than the host requested. In
 *    this case we pump out everything we've got, and
 *    when the final interrupt comes in due to the UDC
 *    clearing the last IPR, we just set DE.
 *
 * 3. Setup requests > 8 bytes, but we don't have enough
 *    data to satisfy the request. In this case, we send
 *    everything we've got, and when the final interrupt
 *    comes in due to the UDC clearing the last IPR
 *    we write nothing to the FIFO and set both IPR and DE
 *    so the UDC sends an empty packet and forces the host
 *    to perform short packet retirement instead of stalling
 *    out.
 *
 */

#include <linux/delay.h>

#define STATUS_STR(s) (                                         \
      s==INIT?"INIT":                                           \
      s==HUB_READY?"HUB_READY":                                 \
      s==DEVICE1_WAIT_READY?"DEVICE1_WAIT_READY":               \
      s==DEVICE1_READY?"DEVICE1_READY":                         \
      s==DEVICE1_WAIT_DISCONNECT?"DEVICE1_WAIT_DISCONNECT":     \
      s==DEVICE1_DISCONNECTED?"DEVICE1_DISCONNECTED":           \
      s==DEVICE2_WAIT_READY?"DEVICE2_WAIT_READY":               \
      s==DEVICE2_READY?"DEVICE2_READY":                         \
      s==DEVICE2_WAIT_DISCONNECT?"DEVICE2_WAIT_DISCONNECT":     \
      s==DEVICE2_DISCONNECTED?"DEVICE2_DISCONNECTED":           \
      s==DEVICE3_WAIT_READY?"DEVICE3_WAIT_READY":               \
      s==DEVICE3_READY?"DEVICE3_READY":                         \
      s==DEVICE3_WAIT_DISCONNECT?"DEVICE3_WAIT_DISCONNECT":     \
      s==DEVICE3_DISCONNECTED?"DEVICE3_DISCONNECTED":           \
      s==DEVICE4_WAIT_READY?"DEVICE4_WAIT_READY":               \
      s==DEVICE4_READY?"DEVICE4_READY":                         \
      s==DEVICE4_WAIT_DISCONNECT?"DEVICE4_WAIT_DISCONNECT":     \
      s==DEVICE4_DISCONNECTED?"DEVICE4_DISCONNECTED":           \
      s==DEVICE5_WAIT_READY?"DEVICE5_WAIT_READY":               \
      s==DEVICE5_CHALLENGED?"DEVICE5_CHALLENGED":               \
      s==DEVICE5_READY?"DEVICE5_READY":                         \
      s==DEVICE5_WAIT_DISCONNECT?"DEVICE5_WAIT_DISCONNECT":     \
      s==DEVICE5_DISCONNECTED?"DEVICE5_DISCONNECTED":           \
      s==DONE?"DONE":                                           \
      "UNKNOWN_STATE")

/* User-friendly string for the request */
#define REQUEST_STR(r) (                        \
      r==0x8006?"GET_DESCRIPTOR":               \
      r==0xa006?"GET_HUB_DESCRIPTOR":           \
      r==0x0009?"SET_CONFIGURATION":            \
      r==0x2303?"SET_PORT_FEATURE":             \
      r==0xa300?"GET_PORT_STATUS":              \
      r==0x2301?"CLEAR_PORT_FEATURE":           \
      r==0x010B?"SET_INTERFACE":                \
	  r==0x0005?"SET_ADDRESS":					\
	  r==0xa000?"GET_HUB_STATUS":    		    \
      "UNKNOWN")

#ifndef MIN
#define MIN( a, b ) ((a)<(b)?(a):(b))
#endif

/***************************************************************************
Inline Helpers
***************************************************************************/

/* Data extraction from usb_request_t fields */
enum { kTargetDevice=0, kTargetInterface=1, kTargetEndpoint=2 };

// static inline int windex_to_ep_num( __u16 w ) { return (int) ( w & 0x000F); }
inline int type_code_from_request( __u8 by ) { return (( by >> 4 ) & 3); }

#if VERBOSITY
/* "pcs" == "print control status" */
static inline void pcs( void )
{
	 __u32 foo = Ser0UDCCS0;

	 printk( "%8.8X: %s %s %s %s %s %s\n" , 
			 foo,
			 foo & UDCCS0_SE ? "SE" : "",
			 foo & UDCCS0_OPR ? "OPR" : "",
			 foo & UDCCS0_IPR ? "IPR" : "",
			 foo & UDCCS0_SST ? "SST" : "",
			 foo & UDCCS0_DE ? "DE" : "",
			 foo & UDCCS0_SO ? "SO" : ""
	 );
}
#else
static inline void pcs( void ){}
#endif

/***************************************************************************
Globals
***************************************************************************/
static const char pszep0[] = "usbep0: ";
static int last_port_reset = 0;

/* pointer to current setup handler */
static void (*current_handler)(void) = sh_setup_begin;

/***************************************************************************
Public Interface
***************************************************************************/

/* reset received from HUB (or controller just went nuts and reset by itself!)
  so udc core has been reset, track this state here  */
void ep0_reset(void)
{
	 /* reset state machine */
	 wr.p = NULL;
	 wr.bytes_left = 0;
	 portAddress[currentPort] = 0;
	 current_handler = sh_setup_begin;
}

/* handle interrupt for endpoint zero */
void ep0_int_hndlr( void )
{
	 PRINTKD( "[%lu]In  /\\(%d)\t", (jiffies-start_time)*10, Ser0UDCAR);

	 if (debug)
		pcs();

	/* if not in setup begin, we are returning data.
		execute a common preamble to both write handlers
	*/
	if ( current_handler != sh_setup_begin ) {
		common_write_preamble();
	}
	// else {
		// // If ep0 is idle state, process delayed port change
		// if (switch_to_port_delayed >= 0) {
			// PRINTKI( "[%lu]Setting timer to 0 ms\n", (jiffies-start_time)*10);
			// state_machine_timeout(0);
		// }
	// }

	(*current_handler)();

	 PRINTKD( "[%lu]Out \\/(%d)\t" , (jiffies-start_time)*10, Ser0UDCAR );
	 if (debug)
		pcs();
}

/***************************************************************************
Setup Handlers
***************************************************************************/
/*
 * sh_setup_begin()
 * This setup handler is the "idle" state of endpoint zero. It looks for OPR
 * (OUT packet ready) to see if a setup request has been been received from the
 * host. Requests without a return data phase are immediately handled. Otherwise,
 * in the case of GET_XXXX the handler may be set to one of the sh_write_xxxx
 * data pumpers if more than 8 bytes need to get back to the host.
 *
 */
static void sh_setup_begin( void )
{
	unsigned char status_buf[2];  /* returned in GET_STATUS */
	unsigned char status_buf2[4];  /* returned in GET_STATUS_HUB */	 
	usb_dev_request_t req;
	int request_type;
	u16 status;
	u16 change;
	int n;
	__u32 address;
	__u32 cs_reg_in = Ser0UDCCS0;
	
	if (cs_reg_in & UDCCS0_SST) {
		PRINTKD( "[%lu]setup begin: sent stall. Continuing\n", (jiffies-start_time)*10);
		set_cs_bits( UDCCS0_SST );
	}

	if ( cs_reg_in & UDCCS0_SE ) {
		PRINTKD( "[%lu]setup begin: Early term of setup. Continuing\n", (jiffies-start_time)*10);
		set_cs_bits( UDCCS0_SSE );  		 /* clear setup end */
	}

	/* Handle iddle status events and delayed actions */
	if ((cs_reg_in & UDCCS0_OPR) == 0 ) {
		// Set address woodoo
		if (Ser0UDCAR != portAddress[currentPort]) {
			Ser0UDCAR = portAddress[currentPort];
			PRINTKD("[%lu]Apply address %d - %d\n", (jiffies-start_time)*10, portAddress[currentPort], Ser0UDCAR);			
		}
		
		// EP2 Complete (TPC means packet sent) . Ep2 interrupt does not work fine
		if (Ser0UDCCS2 & UDCCS2_TPC || hub_interrupt_queued) {
			ep2_int_hndlr();
		}		
		
		// Port reset, send change
		if (last_port_reset) {
			hub_port_changed();
			last_port_reset = 0;
			expected_port_reset = 0;
		}
		goto sh_sb_end;
	}

	/* read the setup request */
	n = read_fifo( &req );
	if ( n != sizeof( req ) ) {
		printk( "[%lu]%ssetup begin: fifo READ ERROR wanted %d bytes got %d. Stalling out...\n", 
			(jiffies-start_time)*10, pszep0, sizeof( req ), n );
		/* force stall, serviced out */
		set_cs_bits( UDCCS0_FST | UDCCS0_SO  );
		goto sh_sb_end;
	}

// #if VERBOSITY
	 // {
	 // unsigned char * pdb = (unsigned char *) &req;
	 // PRINTKD( "[%lu]%2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X ", (jiffies-start_time)*10,
			 // pdb[0], pdb[1], pdb[2], pdb[3], pdb[4], pdb[5], pdb[6], pdb[7]
		  // );
	 // if (debug)
		// preq( &req );
	 // }
// #endif	

	/* Is it a standard or class request ? (not vendor or reserved request) */
	request_type = type_code_from_request( req.bmRequestType );

	if ( request_type != 0 && request_type != 2) {
		printk( "[%lu]setup begin: unsupported bmRequestType: %d ignored\n", (jiffies-start_time)*10, request_type );
		set_cs_bits( UDCCS0_DE | UDCCS0_SO );
		goto sh_sb_end;
	}

	PRINTKI("[%lu]%s Setup called %s (%d - %d) ->  (req=%d) (%d:%d)\n", (jiffies-start_time)*10, 
			STATUS_STR(machine_state),REQUEST_STR(((req.bmRequestType << 8) | req.bRequest)), 
			req.wValue, req.wIndex, req.wLength, currentPort, Ser0UDCAR);

	// Device setup
	if (currentPort) {
		switch( req.bRequest ) {
		case GET_DESCRIPTOR:
			get_device_descriptor(&req);
			break;
		case SET_CONFIGURATION:
			if (currentPort == 5) {
				printk( "[%lu]%sPendiente jig_set_config\n", (jiffies-start_time)*10, pszep0);
				//jig_set_config(dev, 0);				
			}
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		case GET_CONFIGURATION:
		case GET_STATUS:
		case SET_INTERFACE:
			if (currentPort == 5) {
				printk( "[%lu]SET INTERFACE ON JIG\n", (jiffies-start_time)*10);
			}
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		case SET_ADDRESS:
			address = (__u32) (req.wValue & 0x7F);
			portAddress[currentPort] = address;
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			//Ser0UDCAR = address;
			if (addr_delay) {
				udelay(addr_delay);
			}
			break;
		}
/*		case GET_INTERFACE:
			if (ctrl->bRequestType == (USB_DIR_IN|USB_RECIP_INTERFACE)) {
				goto unknown;
		}
		*(u8 *)req->buf = 0;
		break;
	default:
unknown:
		DBG(dev, "unknown control req%02x.%02x v%04x i%04x l%d\n",
		ctrl->bRequestType, ctrl->bRequest,
		w_value, w_index, w_length);		*/
		goto sh_sb_end;
	}

	switch(request_type) {
	case STANDARD_REQUEST:
		/* Handle it */
		switch( req.bRequest ) {
			/* This first bunch have no data phase */
		case SET_ADDRESS:
			address = (__u32) (req.wValue & 0x7F);
			/* when SO and DE sent, UDC will enter status phase and ack,
						..propagating new address to udc core. Next control transfer
						..will be on the new address. You can't see the change in a
						..read back of CAR until then. (about 250us later, on my box).
						..The original Intel driver sets S0 and DE and code to check
						..that address has propagated here. I tried this, but it
						..would only work sometimes! The rest of the time it would
						..never propagate and we'd spin forever. So now I just set
						..it and pray...
					*/
			portAddress[currentPort] = address;
			Ser0UDCAR = address;
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		case SET_CONFIGURATION:
			if ( req.wValue == 1 ) {
				usbd_info.state = USB_STATE_CONFIGURED;
				hub_interrupt_queued = 0;
				PRINTKI("[%lu]reset config\n", (jiffies-start_time)*10);
				//Ser0UDCOMP = 7; OJO
				//Ser0UDCIMP = 7; OJO

			} else if ( req.wValue == 0 ) {
				printk( "[%lu]%ssetup phase: Unknown "
					"\"set configuration\" data %d\n", (jiffies-start_time)*10, pszep0, req.wValue );
			}
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		case CLEAR_FEATURE:
			// /* could check data length, direction...26Jan01ww */
			// if ( req.wValue == 0 ) { /* clearing ENDPOINT_HALT/STALL */
				// int ep = windex_to_ep_num( req.wIndex );
				// if ( ep == 1 ) {
					// printk( "[%lu]%sclear feature \"endpoint halt\" "
						// " on receiver\n", (jiffies-start_time)*10, pszep0 );
					// ep1_reset();
				// }
				// else if ( ep == 2 ) {
					// printk( "[%lu]%sclear feature \"endpoint halt\" "
						// "on xmitter\n", (jiffies-start_time)*10, pszep0 );
					// ep2_reset();
				// } else {
					// printk( "[%lu]%sclear feature \"endpoint halt\" "
						// "on unsupported ep # %d\n", (jiffies-start_time)*10,	pszep0, ep );
				// }
			// } else {
				// printk( "[%lu]%sUnsupported feature selector (%d) in clear feature. Ignored.\n", 
					// (jiffies-start_time)*10,	pszep0, req.wValue );
			// }
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		case SET_FEATURE:
			// if ( req.wValue == 0 ) { /* setting ENDPOINT_HALT/STALL */
				// int ep = windex_to_ep_num( req.wValue );
				// if ( ep == 1 ) {
					// printk( "[%lu]%set feature \"endpoint halt\" "
						// "on receiver\n", (jiffies-start_time)*10, pszep0 );
					// ep1_stall();
				// }
				// else if ( ep == 2 ) {
					// printk( "[%lu]%sset feature \"endpoint halt\" "
						// " on xmitter\n", (jiffies-start_time)*10, pszep0 );
					// ep2_stall();
				// } else {
					// printk( "[%lu]%sset feature \"endpoint halt\" "
						// "on unsupported ep # %d\n", (jiffies-start_time)*10,	pszep0, ep );
				// }
			// }
			// else {
				// printk( "[%lu]%sUnsupported feature selector (%d) in set feature\n", 
						// (jiffies-start_time)*10, pszep0, req.wValue );
				// set_cs_bits( UDCCS0_SST );
				// goto sh_sb_end;
			// }
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		case GET_STATUS:
			/* return status bit flags */
			status_buf[0] = status_buf[1] = 0;

			switch( req.bmRequestType & 0x0f ) {
			case kTargetDevice:
				status_buf[0] |= 1;
				break;
			case kTargetInterface:
				break;
			case kTargetEndpoint:
				/* return stalled bit */
				// n = windex_to_ep_num( req.wIndex );
				// if ( n == 1 )
					// status_buf[0] |= (Ser0UDCCS1 & UDCCS1_FST) >> 4;
				// else if ( n == 2 )
					// status_buf[0] |= (Ser0UDCCS2 & UDCCS2_FST) >> 5;
				// else {
					// printk( "[%lu]%sUnknown endpoint (%d) "
						// "in GET_STATUS\n", (jiffies-start_time)*10, pszep0, n );
				// }
				break;
			default:
				printk( "[%lu]%sUnknown target (%d) in GET_STATUS\n", (jiffies-start_time)*10, pszep0, n );
				/* fall thru */
				break;
			}
			queue_and_start_write(status_buf, req.wLength, sizeof(status_buf));
			break;
		case GET_DESCRIPTOR:
			get_hub_descriptor( &req );
			break;
		case GET_CONFIGURATION:
			status_buf[0] = (usbd_info.state ==  USB_STATE_CONFIGURED) 	? 1 : 0;
			queue_and_start_write( status_buf, req.wLength, 1 );			
			break;
		case GET_INTERFACE:
			printk( "[%lu]%sfixme: get interface not supported\n", (jiffies-start_time)*10, pszep0 );
			queue_and_start_write( NULL, req.wLength, 0 );			
			break;
		case SET_INTERFACE:
			printk( "[%lu]%sfixme: set interface not supported\n", (jiffies-start_time)*10, pszep0 );
			set_cs_bits( UDCCS0_DE | UDCCS0_SO );
			break;
		default :
			printk("[%lu]%sunknown request 0x%x\n", (jiffies-start_time)*10, pszep0, req.bRequest);
			break;
		} /* switch( bRequest ) */
		break;
	case CLASS_REQUEST:
		/* Handle it */
		switch( req.bRequest ) {
		case GET_DESCRIPTOR:
			get_device_descriptor(&req);
			break;
		case CLEAR_FEATURE: // 0x01
			switch (req.bmRequestType & 0x1f) {
			case 0: //USB_RECIP_DEVICE
				set_cs_bits( UDCCS0_DE | UDCCS0_SO );
				break;
			case 3: //USB_RECIP_OTHER
				if (req.wIndex == 0 || req.wIndex > 6) {
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					printk( "[%lu]%s: clear feature invalid port  %02x\n", (jiffies-start_time)*10, pszep0, req.wIndex);					
					break;
				}
				switch(req.wValue) {
				case 0: /* PORT_CONNECTION */
				case 1: /* PORT_ENABLE */
				case 2: /* PORT_SUSPEND */
				case 3: /* PORT_OVER_CURRENT */
				case 4: /* PORT_RESET */
				case 8: /* PORT_POWER */
				case 9: /* PORT_LOW_SPEED */
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					break;
				case 16: // C_PORT_CONNECTION
					PRINTKI( "[%lu]ClearPortFeature C_PORT_CONNECTION called\n", (jiffies-start_time)*10);
					port_change[req.wIndex-1] &= ~PORT_STAT_C_CONNECTION;					
					switch (machine_state) {
					case DEVICE1_WAIT_DISCONNECT:
						machine_state = DEVICE1_DISCONNECTED;
						SET_TIMER (200);
						break;
					case DEVICE2_WAIT_DISCONNECT:
						machine_state = DEVICE2_DISCONNECTED;
						SET_TIMER (110);
						break;
					case DEVICE3_WAIT_DISCONNECT:
						machine_state = DEVICE3_DISCONNECTED;
						SET_TIMER (450);
						break;
					case DEVICE4_WAIT_DISCONNECT:
						machine_state = DEVICE4_DISCONNECTED;
						SET_TIMER (200);
						break;
					case DEVICE5_WAIT_DISCONNECT:
						machine_state = DEVICE5_DISCONNECTED;
						SET_TIMER (200);
						break;
					default:
						break;
					}
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					break;
				case 20: // C_PORT_RESET
					PRINTKI( "[%lu]ClearPortFeature C_PORT_RESET called\n", (jiffies-start_time)*10);
					port_change[req.wIndex-1] &= ~PORT_STAT_C_RESET;
					switch (machine_state) {
					case DEVICE1_WAIT_READY:
						if (req.wIndex == 1)
							switch_to_port_delayed = req.wIndex;
						break;
					case DEVICE2_WAIT_READY:
						if (req.wIndex == 2)
							switch_to_port_delayed = req.wIndex;
						break;
					case DEVICE3_WAIT_READY:
						if (req.wIndex == 3)
							switch_to_port_delayed = req.wIndex;
						break;
					case DEVICE4_WAIT_READY:
						if (req.wIndex == 4)
							switch_to_port_delayed = req.wIndex;
						break;
					case DEVICE5_WAIT_READY:
						if (req.wIndex == 5)
							switch_to_port_delayed = req.wIndex;
						break;
					default:
						break;
					}
					/* Delay switching the port because we first need to response
									to this request with the proper address */
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					if (switch_to_port_delayed >= 0) {
						SET_TIMER (0);
					}					
					break;
				}					
				break;
			}
			break;
		case GET_STATUS:
			switch (req.bmRequestType & 0x1f) {			
			case 0: //USB_RECIP_DEVICE
				status = 0;
				change = 0;
				break;
			case 3: //USB_RECIP_OTHER
			    status = port_status[req.wIndex - 1];
				change = port_change[req.wIndex - 1];
				break;
			}
			// Stop requesting device5 status
			if (req.wIndex==5) {
				device5_sleeps = 0;
			}						
			status = cpu_to_le16 (status);
			change = cpu_to_le16 (change);
			PRINTKI( "[%lu]GetHub/PortStatus: transmiting status %d change %d\n", (jiffies-start_time)*10, status+1024, change);			
			memcpy(status_buf2, &status, sizeof(u16));
			memcpy(status_buf2 + sizeof(u16), &change, sizeof(u16));
			queue_and_start_write( status_buf2, req.wLength,	sizeof( status_buf2) );
			break;
		case SET_FEATURE: // 0x03
			switch (req.bmRequestType & 0x1f) {
				/* SET_HUB_FEATURE */
			case 0: //USB_RECIP_DEVICE
				switch (req.wValue) {
				case 0: /* C_HUB_LOCAL_POWER */
				case 1: /* C_HUB_OVER_CURRENT */
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					break;
				default:
					printk( "[%lu]%s: set hub feature %02x not supported\n", (jiffies-start_time)*10, pszep0, req.wValue);
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );					
					break;
				} //switch (req.wValue)
				break;
			case 3: //USB_RECIP_OTHER
				/* SET_PORT_FEATURE */
				if (req.wIndex == 0 || req.wIndex > 6) {					
					printk( "[%lu]%s: invalid port  %02x\n", (jiffies-start_time)*10, pszep0, req.wIndex);
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					break;
				}
				switch (req.wValue) {
				case 4: /* PORT_RESET */
					PRINTKI( "[%lu]SetPortFeature PORT_RESET called (%d %d)\n", (jiffies-start_time)*10, expected_port_reset, req.wIndex);
					port_change[req.wIndex-1] |= PORT_STAT_C_RESET;					
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					// There seem to be port resets to other port
					if (expected_port_reset == req.wIndex) {
						last_port_reset = req.wIndex;
					}
					break;
				case 8: /* PORT_POWER */
					PRINTKI( "[%lu]SetPortFeature PORT_POWER called\n", (jiffies-start_time)*10);
					port_status[req.wIndex-1] |= PORT_STAT_POWER;
					if (machine_state == INIT && req.wIndex == 6) {
						machine_state = HUB_READY;
						SET_TIMER (15);
					}					
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					break;
				case 0: /* PORT_CONNECTION */
				case 1: /* PORT_ENABLE */
				case 2: /* PORT_SUSPEND */
				case 3: /* PORT_OVER_CURRENT */
				case 9: /* PORT_LOW_SPEED */
				case 16: /* C_PORT_CONNECTION */
				case 17: /* C_PORT_ENABLE */
				case 18: /* C_PORT_SUSPEND */
				case 19: /* C_PORT_OVER_CURRENT */
				case 20: /* C_PORT_RESET */
				case 21: /* PORT_TEST */
				case 22: /* PORT_INDICATOR */
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );
					break;
				default:
					printk( "[%lu]%s: set port feature %02x not supported\n", (jiffies-start_time)*10, pszep0, req.wValue);
					set_cs_bits( UDCCS0_DE | UDCCS0_SO );					
					break;
				} // (req.wValue)
				break;
			} //(req.bmRequestType & 0x1f)
			break;
		} //( req.bRequest )
		break;
	} //(request_type)
	
	/* Enable the timer if it's not already enabled */
	if (timer_added == 0) {
    	add_timer (&state_machine_timer);
  		timer_added = 1;
	}

sh_sb_end:
	return;
}

static void sa1100_set_address(__u32 address)
{
	Ser0UDCAR = address;

	if (address) {
		set_cs_bits( UDCCS0_DE | UDCCS0_SO );
	}
}

/*
 * common_wrtie_preamble()
 * Called before execution of sh_write() or sh_write_with_empty_packet()
 * Handles common abort conditions.
 *
 */
static void common_write_preamble( void )
{
	 /* If "setup end" has been set, the usb controller has
		..terminated a setup transaction before we set DE. This
		..happens during enumeration with some hosts. For example,
		..the host will ask for our device descriptor and specify
		..a return of 64 bytes. When we hand back the first 8, the
		..host will know our max packet size and turn around and
		..issue a new setup immediately. This causes the UDC to auto-ack
		..the new setup and set SE. We must then "unload" (process)
		..the new setup, which is what will happen after this preamble
		..is finished executing.
	 */
	 __u32 cs_reg_in = Ser0UDCCS0;

	 if ( cs_reg_in & UDCCS0_SE ) {
		  PRINTKD( "[%lu]write_preamble(): Early termination of setup\n", (jiffies-start_time)*10);
 		  wr.bytes_left=0;
		  wr.p = NULL;
 		  Ser0UDCCS0 =  UDCCS0_SSE;  		 /* clear setup end */
		  current_handler = sh_setup_begin;
	 }

	 if ( cs_reg_in & UDCCS0_SST ) {
		  printk( "[%lu]write_preamble(): UDC sent stall\n", (jiffies-start_time)*10);
 		  wr.bytes_left=0;
		  wr.p = NULL;
 		  Ser0UDCCS0 = UDCCS0_SST;  		 /* clear setup end */
		  current_handler = sh_setup_begin;
	 }

	 if ( cs_reg_in & UDCCS0_OPR ) {
		PRINTKD( "[%lu]write_preamble(): see OPR. Stopping write to handle new SETUP\n", 
					(jiffies-start_time)*10);
		wr.bytes_left=0;
		wr.p = NULL;
 
		/* very rarely, you can get OPR and leftover IPR. Try to clear */
		UDC_clear( Ser0UDCCS0, UDCCS0_IPR );
		current_handler = sh_setup_begin;
	 }
}

/*
 * sh_write()
 * This is the setup handler when we are in the data return phase of
 * a setup request and have as much (or more) data than the host
 * requested. If we enter this routine and bytes left is zero, the
 * last data packet has gone (int is because IPR was just cleared)
 * so we just set DE and reset. Otheriwse, we write another packet
 * and set IPR.
 */
static void sh_write()
{
	 //PRINTKD( "[%lu]W\n", (jiffies-start_time)*10);

	 if ( Ser0UDCCS0 & UDCCS0_IPR ) {
		  PRINTKD( "[%lu]sh_write(): IPR set, exiting\n", (jiffies-start_time)*10);
		  return;
	 }

	 /* If bytes left is zero, we are coming in on the
		..interrupt after the last packet went out. And
		..we know we don't have to empty packet this transfer
		..so just set DE and we are done */

	 if ( 0 == wr.bytes_left ) {
		/* that's it, so data end  */
		set_de();
		wr.p = NULL;  				/* be anal */
		current_handler = sh_setup_begin;
	} else {
		  /* Otherwise, more data to go */
		  write_fifo();
		  set_ipr();
	}
	 
	udelay(300);
}
/*
 * sh_write_with_empty_packet()
 * This is the setup handler when we don't have enough data to
 * satisfy the host's request. After we send everything we've got
 * we must send an empty packet (by setting IPR and DE) so the
 * host can perform "short packet retirement" and not stall.
 *
 */
static void sh_write_with_empty_packet( void )
{
	PRINTKD( "[%lu]WE\n", (jiffies-start_time)*10);

	if ( Ser0UDCCS0 & UDCCS0_IPR ) {
		PRINTKD( "[%lu]sh_write_empty(): IPR set, exiting\n", (jiffies-start_time)*10);
		return;
	}

	/* If bytes left is zero, we are coming in on the
		..interrupt after the last packet went out.
		..we must do short packet suff, so set DE and IPR
	*/
	if ( 0 == wr.bytes_left ) {
		wr.p = NULL;
		current_handler = sh_setup_begin;
		PRINTKD( "[%lu]sh_write empty() Sent empty packet \n", (jiffies-start_time)*10);
		set_ipr_and_de();
	}
	else {
		write_fifo();				/* send data */
		set_ipr();				/* flag a packet is ready */
	}

	//Ser0UDCCS0 = 0;
	udelay(300); // Ojo funciona en Ubuntu
}

/***************************************************************************
Other Private Subroutines
***************************************************************************/
/*
 * queue_and_start_write()
 * p == data to send
 * req == bytes host requested
 * act == bytes we actually have
 *
 * Called from sh_setup_begin() to begin a data return phase. Sets up the
 * global "wr"-ite structure and load the outbound FIFO with data.
 * If can't send all the data, set appropriate handler for next interrupt.
 *
 */
static void  queue_and_start_write( void * in, int req, int act )
{
	__u32 cs_reg_bits = UDCCS0_IPR;
	unsigned char * p = (unsigned char*) in;

	PRINTKD( "[%lu]Qr=%d a=%d %d\n", (jiffies-start_time)*10, req, act, Ser0UDCCS0);

	/* thou shalt not enter data phase until the serviced OUT is clear */
	if ( ! clear_opr() ) {
		printk( "[%lu]%sSO did not clear OPR\n", (jiffies-start_time)*10, pszep0 );
		set_cs_bits ( UDCCS0_DE | UDCCS0_SO );
		return ;
	}
	
	if (0 != wr.bytes_left) {
		printk( "[%lu]¿Chungo1? fifo already contains %d bytes\n", (jiffies-start_time)*10, wr.bytes_left);
	}	 
	
	wr.p = p;
	wr.bytes_left = MIN( act, req );

	write_fifo();
	
	if ( 0 == wr.bytes_left ) {
		cs_reg_bits |= UDCCS0_DE;	/* out in 1 so data end */
		wr.p = NULL;  				/* be anal */
	}	
	else if ( act < req ) {   /* we are going to short-change host */
		current_handler = sh_write_with_empty_packet; /* so need nul to not stall */
	}
	else { /* we have as much or more than requested */
		current_handler = sh_write;
	}

	set_cs_bits( cs_reg_bits ); /* note: IPR was set uncondtionally at start of routine */
	
	udelay(300);
}
/*
 * write_fifo()
 * Stick bytes in the 8 bytes endpoint zero FIFO.
 * This version uses a variety of tricks to make sure the bytes
 * are written correctly. 1. The count register is checked to
 * see if the byte went in, and the write is attempted again
 * if not. 2. An overall counter is used to break out so we
 * don't hang in those (rare) cases where the UDC reverses
 * direction of the FIFO underneath us without notification
 * (in response to host aborting a setup transaction early).
 *
 */
static void write_fifo( void )
{
	int bytes_this_time = MIN( wr.bytes_left, 8 );
	int bytes_written = 0;
	int i=0;	
	
	PRINTKD( "[%lu]WF=%d: ", (jiffies-start_time)*10, bytes_this_time);

	while( bytes_this_time-- ) {
		 PRINTKD( "%2.2X ", *wr.p );
		 i = 0;
		 do {
			// Early termination (SETUP END) stop sending
			if (Ser0UDCCS0 & UDCCS0_SE) {
				PRINTKD( "[%lu]write_fifo(): Early termination of setup\n", (jiffies-start_time)*10);
				return;
			}
				
			Ser0UDCD0 = *wr.p;
			udelay( 20 );  /* voodo 28Feb01ww */			  
			i++;
		 } while( Ser0UDCWC == bytes_written && i < 10 );
		 if ( i == 10 ) {
			printk( "[%lu]Write_fifo: write failure byte %d. CCR %d CSR %d CS0 %d\n", (jiffies-start_time)*10, bytes_written+1,
				Ser0UDCCR, Ser0UDCSR, Ser0UDCCS0);
			hub_interrupt_queued = 0;
		 }

		 wr.p++;
		 bytes_written++;
	}
	wr.bytes_left -= bytes_written;

	/* following propagation voodo so maybe caller writing IPR in
	   ..a moment might actually get it to stick 28Feb01ww */
	//udelay( 300 ); // ojo

	PRINTKD( "L=%d WCR=%d\n", wr.bytes_left, Ser0UDCWC);
}
/*
 * read_fifo()
 * Read 1-8 bytes out of FIFO and put in request.
 * Called to do the initial read of setup requests
 * from the host. Return number of bytes read.
 *
 * Like write fifo above, this driver uses multiple
 * reads checked agains the count register with an
 * overall timeout.
 *
 */
static int read_fifo( usb_dev_request_t * request )
{
	int bytes_read = 0;
	int fifo_count;
	int i;

	unsigned char * pOut = (unsigned char*) request;

	fifo_count = ( Ser0UDCWC & 0xFF );

	//PRINTKD( "[%lu]RF=%d ", (jiffies-start_time)*10, fifo_count );

	while( fifo_count-- ) {
		 i = 0;
		 do {
			*pOut = (unsigned char) Ser0UDCD0;
			udelay( 10 );
		 } while( ( Ser0UDCWC & 0xFF ) != fifo_count && i < 10 );
		 if ( i == 10 ) {
			  printk( "[%lu]%sread_fifo(): read failure\n", (jiffies-start_time)*10, pszep0 );
		 }
		 pOut++;
		 bytes_read++;
	}

	//PRINTKD( "fc=%d\n", bytes_read );
	return bytes_read;
}

/*
 * get_hub_descriptor()
 * Called from sh_setup_begin to handle data return
 * for a GET_DESCRIPTOR setup request for the hub
 */
static void get_hub_descriptor( usb_dev_request_t * pReq ) {
	int value = 0;
	int type = pReq->wValue >> 8;
	int idx  = pReq->wValue & 0xFF;

	switch( type ) {
	case USB_DESC_DEVICE:
		value = min(pReq->wLength, (u16)hub_device_desc.bLength);
		memcpy(desc_buf, &hub_device_desc, value);
		break;
	case USB_DESC_CONFIG:
		value = min(pReq->wLength, (u16) hub_config_desc.wTotalLength);
		memcpy(desc_buf, &hub_config_desc, value);
		break;
	case USB_DESC_STRING:
		printk( "[%lu]%sChungo. Desc string\n", (jiffies-start_time)*10, pszep0);
		break;
	case USB_DESC_INTERFACE:
		printk( "[%lu]%sChungo Desc interface\n", (jiffies-start_time)*10, pszep0);
		// if ( idx == hub_interface_desc.bInterfaceNumber ) {
			// value = min(pReq->wLength, (u16) hub_interface_desc.bLength);
			// memcpy(desc_buf, &hub_interface_desc, value);
			// queue_and_start_write( hub_interface_desc, pReq->wLength, value);			
		// }
		break;
	case USB_DESC_ENDPOINT: /* correct? 21Feb01ww */
		printk( "[%lu]%sChungo Desc endpoint %d\n", (jiffies-start_time)*10, pszep0, idx);
		// if ( idx == 1 ) {
			// value = min(pReq->wLength, (u16) hub_endpoint_1.bLength);
			// memcpy(desc_buf, &hub_endpoint_1, value);
		// }
		// else if ( idx == 2 ) {
			// value = min(pReq->wLength, (u16) hub_endpoint_2.bLength);
			// memcpy(desc_buf, &hub_endpoint_2, value);
		// }
		// else
			// printk("[%lu]%sunkown endpoint index %d Stall.\n", (jiffies-start_time)*10, pszep0, idx );
			// cs_bits = ( UDCCS0_DE | UDCCS0_SO | UDCCS0_FST );
		// }
		break;
	default :
			printk("[%lu]%sunknown descriptor type %d. Stall.\n", (jiffies-start_time)*10, pszep0, type );
			set_cs_bits ( UDCCS0_DE | UDCCS0_SO | UDCCS0_FST );
			return;
		break;
	}
	if (value > 0) {
		queue_and_start_write(desc_buf, pReq->wLength, value);
	}
	else {
		set_cs_bits ( UDCCS0_DE | UDCCS0_SO);
	}
}

/*
 * get_device_descriptor(usb_dev_request_t * pReq)
 * Called from sh_setup_begin to handle data return
 * for a GET_DESCRIPTOR setup request for the hub
 */
static void get_device_descriptor(usb_dev_request_t * pReq) {
	int value = -EOPNOTSUPP;
	int type = pReq->wValue >> 8;
	int idx  = pReq->wValue & 0xFF;
	
	switch (type) {
	case USB_DT_DEVICE:
		switch (currentPort) {
		case 0:
			value = min(pReq->wLength, (u16) sizeof(hub_header_desc));
			memcpy(desc_buf, hub_header_desc, value);
			break;
		case 1:
			value = min(pReq->wLength, (u16) sizeof(port1_device_desc));
			memcpy(desc_buf, port1_device_desc, value);
			break;
		case 2:
			value = min(pReq->wLength, (u16) sizeof(port2_device_desc));
			memcpy(desc_buf, port2_device_desc, value);
			break;
		case 3:
			value = min(pReq->wLength, (u16) sizeof(port3_device_desc));
			memcpy(desc_buf, port3_device_desc, value);
			break;
		case 4:
			value = min(pReq->wLength, (u16) sizeof(port4_device_desc));
			memcpy(desc_buf, port4_device_desc, value);
			break;
		case 5:
			value = min(pReq->wLength, (u16) sizeof(port5_device_desc));
			memcpy(desc_buf, port5_device_desc, value);
			break;
		default:
			value = -EINVAL;
			break;
		}
		break;
	case USB_DT_CONFIG:
		value = 0;
		switch (currentPort) {
		case 1:
			if (idx < PORT1_NUM_CONFIGS) {
				if (pReq->wLength == 8) {
					value = sizeof(port1_short_config_desc);
					memcpy(desc_buf, port1_short_config_desc, value);
				} else {
					value = port1_config_desc_size;
					memcpy(desc_buf, port1_config_desc, value);
				}
				if (idx == (PORT1_NUM_CONFIGS-1) && pReq->wLength > 8) {
					machine_state = DEVICE1_READY;
					switch_to_port_delayed = 0;
					SET_TIMER (100); // log 90 jb 100
				}
			}
			PRINTKD( "[%lu]Device Req type %d, idx %d reqlen %d serve %d\n", (jiffies-start_time)*10, type, idx, pReq->wLength, value);
			break;
		case 2:
			value = sizeof(port2_config_desc);
			memcpy(desc_buf, port2_config_desc, value);
			if (pReq->wLength > 8) {
				machine_state = DEVICE2_READY;
				switch_to_port_delayed = 0;
				SET_TIMER (10); // log 0 jb 150
			}
			break;
		case 3:
			value = sizeof(port3_config_desc);
			memcpy(desc_buf, port3_config_desc, value);
			if (idx == 1 && pReq->wLength > 8) {
				machine_state = DEVICE3_READY;
				switch_to_port_delayed = 0;
				SET_TIMER (70); //log 60 jb 80
			}
			break;
		case 4:
			if (idx == 0) {
				value = sizeof(port4_config_desc_1);
				memcpy(desc_buf, port4_config_desc_1, value);
			} else if (idx == 1) {
				if (pReq->wLength == 8) {
					value = sizeof(port4_short_config_desc_2);
					memcpy(desc_buf, port4_short_config_desc_2, value);
				} else {
					value = sizeof(port4_config_desc_2);
					memcpy(desc_buf, port4_config_desc_2, value);
				}
			} else if (idx == 2) {
				value = sizeof(port4_config_desc_3);
				memcpy(desc_buf, port4_config_desc_3, value);
				if (pReq->wLength > 8) {
					machine_state = DEVICE4_READY;
					switch_to_port_delayed = 0;
					SET_TIMER (10); // log 0 jb 180
				}
			}
			break;
		case 5:
			value = sizeof(port5_config_desc);
			memcpy(desc_buf, port5_config_desc, value);
			break;
		default:
			value = -EINVAL;
			printk( "[%lu]Chungo currentPort 0\n", (jiffies-start_time)*10);
			break;
		}
		if (value >= 0)
			value = min(pReq->wLength, (u16)value);
		break;
	case USB_DT_STRING:
		value = 0;
		PRINTKI( "[%lu]String Req type %d, idx %d reqlen %d\n", (jiffies-start_time)*10, type, idx, pReq->wLength);
		break;
	case 0x29: // HUB descriptor (always to port 0 we'll assume)
		if (currentPort) {
			printk( "[%lu]Error hub_descriptor request for port %d\n", (jiffies-start_time)*10, currentPort);
		}
		else {
			value = min(pReq->wLength, (u16) sizeof(hub_header_desc));
			memcpy(desc_buf, hub_header_desc, value);
		}
		break;		
	}
	
	if (value > 0) {
		queue_and_start_write(desc_buf, pReq->wLength, value);
	}
	else {
		set_cs_bits( UDCCS0_DE | UDCCS0_SO);
	}
}

/* some voodo I am adding, since the vanilla macros just aren't doing it  1Mar01ww */

#define ABORT_BITS ( UDCCS0_SST | UDCCS0_SE )
#define OK_TO_WRITE (!( Ser0UDCCS0 & ABORT_BITS ))
#define BOTH_BITS (UDCCS0_IPR | UDCCS0_DE)

static void set_cs_bits( __u32 bits )
{
	 if ( bits & ( UDCCS0_SO | UDCCS0_SSE | UDCCS0_FST ) )
		Ser0UDCCS0 = bits;
	 else if ( (bits & BOTH_BITS) == BOTH_BITS )
		set_ipr_and_de();
	 else if ( bits & UDCCS0_IPR )
		set_ipr();
	 else if ( bits & UDCCS0_DE )
		set_de();
}

static void set_de( void )
{
	int i = 1;

	while( 1 ) {
		if ( OK_TO_WRITE ) {
			Ser0UDCCS0 |= UDCCS0_DE;
		} else {
			PRINTKI( "[%lu]%sQuitting set DE because SST or SE set\n", (jiffies-start_time)*10, pszep0 );
			break;
		}
		if ( Ser0UDCCS0 & UDCCS0_DE )
			break;
		udelay( i );
		if ( ++i == 50  ) {
			printk( "[%lu]Dangnabbbit! Cannot set DE! (DE=%8.8X CCS0=%8.8X)\n", (jiffies-start_time)*10,
					   UDCCS0_DE, Ser0UDCCS0 );
			break;
		}
	}
}

static void set_ipr( void )
{
	int i = 1;
	while( 1 ) {
		if ( OK_TO_WRITE ) {
			Ser0UDCCS0 |= UDCCS0_IPR;
		} else {
			PRINTKI( "[%lu]Quitting set IPR because SST or SE set (%d)\n", (jiffies-start_time)*10, Ser0UDCCS0);
			debug=0;
			break;
		}
		if ( Ser0UDCCS0 & UDCCS0_IPR )
			break;
		udelay( i );
		if ( ++i == 50  ) {
			printk( "[%lu]Dangnabbbit! Cannot set IPR! (IPR=%8.8X CCS0=%8.8X)\n", (jiffies-start_time)*10,
					UDCCS0_IPR, Ser0UDCCS0 );
			break;
		}
	}
}

static void set_ipr_and_de( void )
{
	int i = 1;
	while( 1 ) {
		if ( OK_TO_WRITE ) {
			Ser0UDCCS0 |= BOTH_BITS;
		} else {
			PRINTKI( "[%lu]%sQuitting set IPR/DE because SST or SE set (%d)\n", (jiffies-start_time)*10, pszep0, Ser0UDCCS0);
			break;
		}
		if ( (Ser0UDCCS0 & BOTH_BITS) == BOTH_BITS)
			break;
			
		udelay( i );
		if ( ++i == 50  ) {
			printk( "[%lu]Dangnabbbit! Cannot set DE/IPR! (DE=%8.8X IPR=%8.8X CCS0=%8.8X)\n", (jiffies-start_time)*10,
				UDCCS0_DE, UDCCS0_IPR, Ser0UDCCS0 );
			break;
		}
	}
}

static bool clear_opr( void )
{
	int i = 10000;
	bool is_clear;
	do {
		Ser0UDCCS0 = UDCCS0_SO;
		is_clear  = ! ( Ser0UDCCS0 & UDCCS0_OPR );
		if ( i-- <= 0 ) {
			printk( "[%lu]clear_opr(): failed\n", (jiffies-start_time)*10);
			break;
		}
	} while( ! is_clear );
	return is_clear;
}