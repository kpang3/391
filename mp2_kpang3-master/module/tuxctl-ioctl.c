/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)


#define LONG_SIZE 	4	// number of bytes in a long
#define TWO_BYTES	16	// num of bits in two bytes
#define EMPTY 		0
//  byte masks
#define LOW_BYTE1 	0xF
#define HIGH_BYTE1 	0xF0
#define LOW_BYTE2 	0xF00
#define HIGH_BYTE2 	0xF000


/************************ Local Variables *************************/
static unsigned long button_packet;
static int busy;
static unsigned long led_status;
static unsigned char hex_display_data[16] =
{
	0xE7, 0x06, 0xCB, 0x8F, 0x2E, 0xAD, 0xED, 0x86, 0xEF, 0xAE, 0xEE, 0x6D, 0xE1, 0x4F, 0xE9, 0xE8

};

/************************ Local Helper Functions *************************/
static void tux_initializer(struct tty_struct* tty);
static void led_handler(struct tty_struct* tty, unsigned long arg);

/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

	int right, left, down, up;

	unsigned long led_temp;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

	switch (a) {
		case MTCP_BIOC_EVENT:
			// change packet to correspond to active high
			b = ~b & LOW_BYTE1;
			c = ~c & LOW_BYTE1;
			// put packet into form CBAS RDLU 
			right = (c >> 3); // because right is bit 4
			left = (c>>1) & 1; // because left bit is bit 2
			down = (c>>2) & 1; // because down is bit 3
			up = c & 1; // because up is bit 1
			button_packet = (b<<4|right<<3|left<<2|down<<1|up);

		//case MTCP_LED_SET:

		case MTCP_ACK:
			busy = 0;
			//copy_to_user();

		case MTCP_RESET:
			// save current LED state
			led_temp = led_status;
			// re-initialize tux
			tux_initializer(tty);
			/*  your code must internally keep track of the state of 
			 * the device. -- restore LEDs displayed before reset
			 */
			led_handler((void *) tty, led_temp);
			// busy reset to LOW?????
			busy = 0;

		//case MTCP_POLL:

	}
    /*printk("packet : %x %x %x\n", a, b, c); */
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	
	/* Takes no arguments. Initializes any variables associated with the driver and returns 0.
	 * We recommend that you make use of MTCP BIOC ON and MTCP LED SET, in which case you must handle MTCP ACK
	 * and MTCP BIOC EVENT. You must also handle MTCP RESET packets sent to the computer by attempting to restore the
	 * controller state (value displayed on LEDs, button interrupt generation, etc.)
	 */
	case TUX_INIT:
		// call helper fxn so it can be used for reset as well
		tux_initializer(tty);
		led_status = 0x040F0000;// set LEDs to 00.00
		led_handler(tty,led_status);
		return 0;

	/* 
	 * Takes a pointer to a 32-bit integer. Returns -EINVAL error if this pointer is not valid. 
	 * Otherwise, sets the bits of the low byte corresponding to the currently pressed buttons.
	 */
	case TUX_BUTTONS:
		// check if pointer is valid
		if (arg == 0)
			return -EINVAL;
		// else set bits of low byte
		//const char* button_status = MTCP_POLL; // poll buttons for current status
		
		copy_to_user((unsigned long *)arg,&button_packet,LONG_SIZE); //(to, from, bytes);
		return 0;


	/* 
	* This ioctl should return 0
	*/
	case TUX_SET_LED:
		// make sure driver is ready for new command
		if (!busy){ // busy HIGH
			led_handler(tty,arg);

		}
		// led_handler(tty,arg);
		return 0;

	case TUX_LED_ACK:
	case TUX_LED_REQUEST:
	case TUX_READ_LED:
	default:
	    return -EINVAL;
    }
}

/* tux_initializer
 * DESCRIPTION: initializes variables associated with driver
 *   INPUTS: struct tty_struct* tty
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: sends MTCP_BIOC_ON and LED_USER to be processed by driver
 */
void tux_initializer(struct tty_struct* tty){

		// MTCP_BIOC_ON: opcode for button interrupt on
		// MTCP_LED_USR: opcode to set LEDs to user value
		char init_buf[3];
		init_buf[0] = MTCP_BIOC_ON;
		init_buf[1] = MTCP_LED_USR; // MTCP_ACK, MTCP_BIOC_EVENT}; <-- how do i make use? TLDR-> handle packet takes care of this
		init_buf[2] = MTCP_DBG_OFF;
		// set busy for interrupt
		busy = 1;
		tuxctl_ldisc_put(tty, init_buf, 3);
		
		return;
}

/* led_handler
 * DESCRIPTION: 
 *   INPUTS: struct tty_struct* tty, unsigned long arg
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: 
 */
void led_handler(struct tty_struct* tty, unsigned long arg){
	
	char led_buf[6];
	int hex0, hex1, hex2, hex3;

	hex0 = LOW_BYTE1 & arg;
	hex1 = HIGH_BYTE1 & arg;
	hex2 = LOW_BYTE2 & arg;
	hex3 = HIGH_BYTE2 & arg;
	
	//save current state
	led_status = arg;
	//first packet is MTCP_LED_SET
	led_buf[0] = MTCP_LED_SET;
	// second packet is which LED's to set
	// int set_LEDs = (arg>>TWO_BYTES) & LOW_BYTE1
	led_buf[1] = LOW_BYTE1; // set all LED's to 1
	// // load hex display data into packets 2-6
	led_buf[2] = hex_display_data[hex0]; // third packet is LED0
	led_buf[3] = hex_display_data[hex1>>4]; // fourth packet is LED1
	led_buf[4] = hex_display_data[hex2>>8]; // fifth packet is LED2
	led_buf[5] = hex_display_data[hex3>>12]; // convert int values to hex bits
	
	// turn dp on in the third LED
	led_buf[4] |= 0x10; // mapped to bit 4

	// led_buf[2]= 0x2e;
	// led_buf[3]= 0x2e;
	// led_buf[4]= 0x2e;
	// led_buf[5]= 0x2e;
	

	// send LED command to driver
	busy = 1; // set busy HIGH
	tuxctl_ldisc_put(tty, led_buf, 6); // four bytes being sent
	//busy = 1; 
	

	return;

}

