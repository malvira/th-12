#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "contiki.h"
#include "th-12.h"
#include "sen.h"

#include "mc1322x.h"

#define setdo(x) GPIO->PAD_DIR_SET.x=1
#define setdi(x) GPIO->PAD_DIR_RESET.x=1

#define IEF 11


struct etimer et_sen;
struct etimer et_count;

volatile uint16_t time1 = 0;
volatile uint16_t last = 0;
volatile uint8_t started = 0;
volatile uint16_t checksum = 0;

void tmr1_isr(void) {
	if((bit_is_set(*TMR(1,SCTRL),IEF))) {
		time1 = *TMR1_CAPT;
		*TMR1_CNTR = 0;
		*TMR1_CAPT = 0;

		if(started) {
			if((last > 65)&&(last < 80)) {
				sys.sen_data[sys.sen_index] <<= 1;
				sys.sen_data[sys.sen_index] |= (time1 > 70);
				sys.sen_debug[sys.sen_din] = (time1 > 70);
				if((sys.sen_bcount%8)==7) {
					sys.sen_index++;
				}
				if(sys.sen_bcount == 38) {
					int ind;
					checksum=0;
					for(ind=0;ind<4;ind++) {
						checksum += sys.sen_data[ind];
					}
					checksum &= 0xff;
					sys.sen_index = 0;
				}
				sys.sen_debug[99]=sys.sen_bcount;
				sys.sen_bcount++;
			} else {
				sys.sen_debug[sys.sen_din] = time1;
			}
		} else {
			if((last > 100)&&(time1 > 100)) {
				started = 1;
				sys.sen_debug[sys.sen_din] = time1;
			}
		}
		last = time1;
		sys.sen_din++;

		//clear the edge trigger flags
		clear_bit(*TMR(1,SCTRL),IEF);
	} else {
	//Didn't trigger the interrupt
		return;
	}
}

void foo(void) {
	TMR1->ENBLbits.ENBL1 = 0;
	TMR1->CTRLbits = (struct TMR_CTRL) {
		.COUNT_MODE = 1,
		.PRIMARY_CNT_SOURCE = 0xc,
		.SECONDARY_CNT_SOURCE = 1,
		.ONCE = 0,
		.LENGTH = 0,
		.DIR = 0,
		.CO_INIT = 0,
		.OUTPUT_MODE = 0,
	};
	TMR1->SCTRLbits = (struct TMR_SCTRL) {
		.OEN = 0,
		.OPS = 0,
		.VAL = 0,
		.EEOF = 0,
		.MSTR = 0,
		.CAPTURE_MODE = 3,
		.IPS = 0,
		.IEFIE = 1,
		.TOFIE = 0,
		.TCFIE = 0,
	};
	TMR1->CSCTRL = 0x2000;
	TMR1->LOAD = 0;
	TMR1->COMP1 = 0XFFFF;
	TMR1->CNTR = 0;
	TMR1->ENBLbits.ENBL1 = 1;
}

PROCESS(sen_process, "sen");
PROCESS_THREAD(sen_process, ev, data)
{
	foo();	
	enable_irq(TMR);	
	
	PROCESS_BEGIN();
	
	
	printf("SEN\n");
	
	etimer_set(&et_sen, 2 * CLOCK_SECOND);
	etimer_set(&et_count, 1);
	while(1) {
		PROCESS_WAIT_EVENT();
		if(etimer_expired(&et_sen))
		{
			int y;
			sys.sen_tcount=0;
			etimer_restart(&et_count);
			gpio_reset(TMR1);
//			printf("pulling low to init comms\n");
//			printf("count: %d \n", sys.sen_tcount);
			while(sys.sen_tcount<2)
			{
				PROCESS_PAUSE();
				if(etimer_expired(&et_count))
				{
					sys.sen_tcount++;
					etimer_reset(&et_count);
				}
			}
//			printf("high impedance to start listening\n");
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
//			printf("transmission over, pulling high again \n");
			SEN_OUT();
			gpio_set(TMR1);
			printf("\n checksum: %d = %d ? \n", checksum, sys.sen_data[4]);
			printf("data:  ");
			int x;
			for(x=0;x<5;x++) {
				printf(" %0d ",sys.sen_data[x]);
				sys.sen_data[x]=0;
			}
			printf("\n");
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

