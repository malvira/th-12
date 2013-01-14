/* This code is for the whit DHT22 sensor */
/* The blue sensor has slightly different byte format */
/* but the humidity numbers are too bad from it to use for anything */
/* could make an ok temp sensor but you are better off rolling your own with a thermistor */
/* opamp and LUT */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "contiki.h"
#include "th-12.h"
#include "dht.h"

#include "mc1322x.h"

/* debug */
/* commented out since it can conflict with the debug setting in th-12.c */
#define DEBUG DEBUG_NONE
#include "net/uip-debug.h" 

#define setdo(x) GPIO->PAD_DIR_SET.x=1
#define setdi(x) GPIO->PAD_DIR_RESET.x=1

#define MAX_SAMPLES 64
uint16_t dht_time[MAX_SAMPLES]; /* dht11 returns 40 pulses, allocate a little extra just in case */
uint8_t dht_idx;                /* current index into the results buffer */

#define DHT_BYTES 5             /* number of bytes returned by dht */
#define DHT_BITS DHT_BYTES * 8  /* number of bits returned by dht */
#define DHT_THRESH 83           /* threshold pulse width between high and low value */
uint8_t dht[DHT_BYTES];

/* capture both rising and falling edges */
/* toggle IPS accordingly so that we get an interrupt on each edge (sometime after the capture) */
/* we save the time on the rising edge */
/* and compute the difference after the falling edge */ 

void tmr1_isr(void) {
	if(TMR1->SCTRLbits.IEF == 1) {
		if ( GPIO->DATA.TMR1 == 1) {
			/* rising edge */
			TMR1->SCTRLbits.IPS = 1; /* pin is high, trigger interrupt on falling edge */
			dht_time[dht_idx] = *TMR1_CAPT; /* save rising edge time */
		} else {
			/* falling edge */			
			TMR1->SCTRLbits.IPS = 0; /* pin is low, trigger interrupt on rising edge */
			/* compute the delta T and increment the pointer */
			dht_time[dht_idx] = (uint16_t)(*TMR1_CAPT - dht_time[dht_idx]);
			if(dht_idx++ >= MAX_SAMPLES) { dht_idx = 0; }
		}
	}
	TMR1->SCTRLbits.IEF = 0;
}

void tmr1_init(void) {

	/* this is necessary for the TMR1 capt to work... not sure why */
	*TMR0_CTRL = (1 << 13); /* set count mode to count rising edges of primary source */

	*TMR_ENBL = 0; /* disable timers */
	TMR1->CTRLbits = (struct TMR_CTRL) {
		.COUNT_MODE = 1,           /* count rising edges of primary source (should be 24MHz) */
		.PRIMARY_CNT_SOURCE = 0xc, /* primary source divided by 16 */
		.SECONDARY_CNT_SOURCE = 1, /* set to tmr 1, but not used */
		.ONCE = 0,                 /* continuous */
		.LENGTH = 0,               /* roll over counter at end */
		.DIR = 0,                  /* count up */
		.CO_INIT = 0,              /* no co-channels */
		.OUTPUT_MODE = 0,          /* assert output flag while running */
	};
	TMR1->SCTRLbits = (struct TMR_SCTRL) {
		.OEN = 0,                  /* pin is input */
		.OPS = 0,                  /* don't invert */
		.VAL = 0,                  /* default value when forcing (not used) */
		.EEOF = 0,                 /* disable force */
		.MSTR = 0,                 /* no master mode */
		.CAPTURE_MODE = 1,         /* load capture on rising edges (if IPS = 0)*/
		.IPS = 0,                  /* don't invert */
		.IEFIE = 1,                /* enable interrupts */
		.TOFIE = 0,                /* no timer overflow interrupt */
		.TCFIE = 0,                /* no compare interrupt */
	};
	TMR1->CSCTRL = 0;                  /* disable all compares */
	TMR1->CSCTRLbits.FILT_EN = 1;      /* enable glitch filters */
	TMR1->LOAD = 0;                    /* load in 0 */
	TMR1->COMP1 = 0XFFFF;              /* set compare to max */
	TMR1->CNTR = 0;                    /* start counter at 0 */
	*TMR_ENBL = 0x2; /* enable only timer 1 */
}

/* signals the dht to send data back */
/* waits for the result */
/* posts a dht_done event with a dht result struct in data */

struct etimer et_dht;

void (*dht_result)(dht_result_t d);

void register_dht_result( void (*dht_result_cb) ) {
	dht_result = dht_result_cb;
}

PROCESS(read_dht, "read dht");
PROCESS_THREAD(read_dht, ev, data)
{
	dht_result_t d;
	uint8_t bit_offset;
	PROCESS_BEGIN();
	
	PRINTF("pulling low to start dht\n\r");
	dht_idx = 0;

	/* keep pin low for at least 18ms */
	gpio_reset(TMR1);
	etimer_set(&et_dht, 0.01 * CLOCK_SECOND);
	while (!etimer_expired(&et_dht)) { PROCESS_PAUSE(); }

	/* pull high */
	/* PRINTF("pulling high\n\r"); */
	/* gpio_set(TMR1); */
	/* etimer_set(&et_dht, 0.01 * CLOCK_SECOND); */
	/* while (!etimer_expired(&et_dht)) { PROCESS_PAUSE(); } */

	PRINTF("set high impedance to start listening\n\r");
	DHT_PU();

	/* wait for the DHT to finish */
	etimer_set(&et_dht, 0.05 * CLOCK_SECOND);
	while (!etimer_expired(&et_dht)) { PROCESS_PAUSE(); }
	
	PRINTF("transmission over, pulling high again\n\r");
	DHT_OUT();
	gpio_set(TMR1);
	PRINTF("data:  ");
	{
		uint8_t i;
		uint8_t val;

		/* initialize the dht results */
		for (i = 0; i < DHT_BYTES; i++) {
			dht[i] = 0;
		}

#ifdef DEBUG_FULL
		/* print the raw timing data */
		for (i = 0; i < MAX_SAMPLES; i ++) {
			PRINTF("%d ", dht_time[i]);					
		}
		PRINTF("\n\r");
#endif

		/* find the first pulse */
		/* while running data looks like: data: 55400 122 40 ... */
		/* on power up though, we get: data:  122 40 40 40 */
		if(dht_time[0] > 5000) {
			bit_offset = 2;
		} else {
			bit_offset = 1;
		}

		/* convert pulse width encoded data in to bits */
		/* and pack them into bytes */
		for (i = bit_offset; i < DHT_BITS + bit_offset; i ++) {
#ifdef DEBUG_FULL
			/* print packed bytes */
			if(((i-bit_offset) % 8) == 0 && (i-bit_offset > 7)) { PRINTF(" = 0x%02x\n\r", dht[((i-bit_offset)/8) - 1]); }
#endif
			
			/* threshold times into bits */
			if (dht_time[i] < DHT_THRESH) { val = 0; } else { val = 1; } 

			PRINTF("%d", val);

			/* pack */
			dht[(i-bit_offset)/8] <<= 1;
			dht[(i-bit_offset)/8] |= val;
		}				
		PRINTF(" = 0x%02x\n\r", dht[4]);
		PRINTF("%02x %02x %02x %02x %02x\n\r", dht[0], dht[1], dht[2], dht[3], dht[4]);
		PRINTF("sum = %04x\n\r", dht[0] + dht[1] + dht[2] + dht[3]);
		
		if( dht_result && (dht[4] == (uint8_t)(dht[0] + dht[1] + dht[2] + dht[3]))) { 
		  int16_t temp;
		  uint16_t t;
		  d.ok = 1;
		  d.rh = dht[0] << 8 | dht[1];

		  t = (dht[2] & 0x7f) << 8 | dht[3];
		  if (dht[2] & 0x80) {
		    temp = -1 * t;
		  } else {
		    temp = t;
		  }
		  d.t = temp;
		  dht_result(d);
		}

		PROCESS_EXIT();
	}
	
	PROCESS_END();
}	
				

void dht_init(void)
{
	/* power on dht */
	setdo(KBI1);
	GPIO->FUNC_SEL.KBI1=3;
	gpio_set(KBI1);

	/* set data pin */
	setdo(TMR1);
	GPIO->FUNC_SEL.TMR1=3;
	gpio_set(TMR1);

	tmr1_init();	
	enable_irq(TMR);	
}

void dht_uninit(void)
{
	CRM->WU_CNTLbits.EXT_OUT_POL = 0; /* drive KBI0-3 low during sleep */
	gpio_reset(KBI1);
	setdo(TMR1);
	gpio_set(TMR1);
}
