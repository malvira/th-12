#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#include "contiki.h"
#include "th-12.h"
#include "sen.h"

#include "mc1322x.h"

#define setdo(x) GPIO->PAD_DIR_SET.x=1
#define setdi(x) GPIO->PAD_DIR_RESET.x=1

struct etimer et_sen;
struct etimer et_count;

volatile uint16_t time1 = 0;
volatile uint16_t last = 0;
volatile uint8_t started = 0;
volatile uint16_t checksum = 0;

/* tmr 1 will be configured to measure 24MHz/16 = 0.666 uS*/
/* and will measure the length of positive pulses */
/* capture will start on rising edge and stop on falling edge */
/* the time will be loaded into the current index of the results array */
/* and the index will be autoincremented */
/* code external to this will analyze this raw data and reset the results pointer */

#define MAX_SAMPLES 64
#define BIT_OFFSET 2 /* bits to offset from the raw data */
uint16_t dht_time[MAX_SAMPLES]; /* dht11 returns 40 pulses, allocate a little extra just in case */
uint8_t idx; /* current index into the results buffer */

#define DHT_BYTES 5 /* number of bytes returned by dht */
#define DHT_BITS 40 /* number of bits returned by dht */
uint8_t dht[DHT_BYTES];

/* trigger and interrupt on every rising edge with IEF and IEFIE and IPS = 0 */
/* setup the timer to capture the value only on falling edges */
/* when the ISR is triggered (b/c of the rising edge) */
/* store the previous result and reset the counter */

void tmr1_isr(void) {
	if(TMR1->SCTRLbits.IEF == 1) {
		if ( GPIO->DATA.TMR1 == 1) {
			/* rising edge */
			TMR1->SCTRLbits.IPS = 1; /* pin is high, trigger interrupt on falling edge */
			dht_time[idx] = *TMR1_CAPT; /* save rising edge time */
		} else {
			/* falling edge */			
			TMR1->SCTRLbits.IPS = 0; /* pin is low, trigger interrupt on rising edge */
			/* compute the delta T and increment the pointer */
			dht_time[idx] = (uint16_t)(*TMR1_CAPT - dht_time[idx]);
			if(idx++ >= MAX_SAMPLES) { idx = 0; }
		}
	}
	TMR1->SCTRLbits.IEF = 0;
}

/* 		if(started) { */
/* 			if((last > 65)&&(last < 80)) { */
/* 				sys.sen_data[sys.sen_index] <<= 1; */
/* 				sys.sen_data[sys.sen_index] |= (time1 > 70); */
/* 				sys.sen_debug[sys.sen_din] = (time1 > 70); */
/* 				if((sys.sen_bcount%8)==7) { */
/* 					sys.sen_index++; */
/* 				} */
/* 				if(sys.sen_bcount == 38) { */
/* 					int ind; */
/* 					checksum=0; */
/* 					for(ind=0;ind<4;ind++) { */
/* 						checksum += sys.sen_data[ind]; */
/* 					} */
/* 					checksum &= 0xff; */
/* 					sys.sen_index = 0; */
/* 				} */
/* 				sys.sen_debug[99]=sys.sen_bcount; */
/* 				sys.sen_bcount++; */
/* 			} else { */
/* 				sys.sen_debug[sys.sen_din] = time1; */
/* 			} */
/* 		} else { */
/* 			if((last > 100)&&(time1 > 100)) { */
/* 				started = 1; */
/* 				sys.sen_debug[sys.sen_din] = time1; */
/* 			} */
/* 		} */
/* 		last = time1; */
/* 		sys.sen_din++; */

/* 		//clear the edge trigger flags */
/* 		clear_bit(*TMR(1,SCTRL),IEF); */
/* 	} else { */
/* 	//Didn't trigger the interrupt */
/* 		return; */
	
/* } */


void tmr1_init(void) {
	TMR1->ENBLbits.ENBL1 = 0;
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
	TMR1->ENBLbits.ENBL1 = 1;          /* enable TMR1 */
}

PROCESS(sen_process, "sen");
PROCESS_THREAD(sen_process, ev, data)
{
	PROCESS_BEGIN();

	tmr1_init();	
	enable_irq(TMR);	
	       	
	printf("SEN\n");
	
	etimer_set(&et_sen, 2 * CLOCK_SECOND);
	etimer_set(&et_count, 1);
	while(1) {
		PROCESS_WAIT_EVENT();
		if(etimer_expired(&et_sen))
		{
			/* int y; */
			sys.sen_tcount=0;
			etimer_restart(&et_count);
			PRINTF("idx %d\n\r", idx);
			idx = 0;
			gpio_reset(TMR1);
			PRINTF("pulling low to init comms\n");
			PRINTF("count: %d \n", sys.sen_tcount);
			while(sys.sen_tcount<2)
			{
				PROCESS_PAUSE();
				if(etimer_expired(&et_count))
				{
					sys.sen_tcount++;
					etimer_reset(&et_count);
				}
			}
			PRINTF("high impedance to start listening\n");
			SEN_PU();
			sys.sen_tcount=0;
			//etimer_restart(&et_count);
//			printf("waiting....\n");
			while(sys.sen_tcount<2)
			{
				PROCESS_PAUSE();
				if(etimer_expired(&et_count))
				{
					sys.sen_tcount++;
					etimer_reset(&et_count);
				}
			}
		/*	while(sys.sen_done==0)
			{
				PROCESS_PAUSE();
			}*/

			/* XXX TODO this is a hack, should just expire and pull it high */
			while (idx < 40) { PROCESS_PAUSE(); }
			
			PRINTF("transmission over, pulling high again \n");
			SEN_OUT();
			gpio_set(TMR1);
			PRINTF("\n checksum: %d = %d ? \n", checksum, sys.sen_data[4]);
			PRINTF("data:  ");


			{
				uint8_t i;
				uint8_t val;
				for (i = 0; i < DHT_BYTES; i++) {
					dht[i] = 0;
				}
				for (i = 0; i < MAX_SAMPLES; i ++) {
					PRINTF("%d ", dht_time[i]);					
				}
				PRINTF("\n\r");
				for (i = BIT_OFFSET; i < DHT_BITS + BIT_OFFSET; i ++) {
					if(((i-BIT_OFFSET) % 8) == 0 && (i-BIT_OFFSET > 7)) { PRINTF(" = 0x%02x\n\r", dht[((i-BIT_OFFSET)/8) - 1]); }
					if (dht_time[i] < 65) { val = 0; } else { val = 1; } 
					PRINTF("%d", val);
					dht[(i-BIT_OFFSET)/8] <<= 1;
					dht[(i-BIT_OFFSET)/8] |= val;
				}				
				PRINTF(" = 0x%02x\n\r", dht[4]);
				PRINTF("%02x %02x %02x %02x %02x\n\r", dht[0], dht[1], dht[2], dht[3], dht[4]);
				PRINTF("sum = %04x\n\r", dht[0] + dht[1] + dht[2] + dht[3]);
			}
					


			/* int x; */
			/* for(x=0;x<5;x++) { */
			/* 	printf(" %0d ",sys.sen_data[x]); */
			/* 	sys.sen_data[x]=0; */
			/* } */
			/* printf("\n"); */
//			printf("debug: ");
//			for(y=0;y<100;y++) {
//				printf(" %d ", sys.sen_debug[y]);
//				sys.sen_debug[y]=0;
//			}
//			printf("\n");
			checksum = 0;
			sys.sen_din=0;
			time1=0;
			sys.sen_done=0;
			sys.sen_index=0;
			sys.sen_bcount=0;
			started=0;

			etimer_reset(&et_sen);
		}
	}
	PROCESS_END();
}	
				

void sen_init(void)
{
	setdo(KBI1);
	GPIO->FUNC_SEL.KBI1=3;
	gpio_set(KBI1);

	setdo(TMR1);
	GPIO->FUNC_SEL.TMR1=3;
	gpio_set(TMR1);

	process_start(&sen_process, NULL);
}

