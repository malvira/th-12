#include <string.h>

/* contiki */
#include "contiki.h"
#include "contiki-net.h"
#include "net/rpl/rpl.h"

/* coap */
#if WITH_COAP == 3
#include "er-coap-03-engine.h"
#elif WITH_COAP == 6
#include "er-coap-06-engine.h"
#elif WITH_COAP == 7
#include "er-coap-07-engine.h"
#else
#error "CoAP version defined by WITH_COAP not implemented"
#endif

/* mc1322x */
#include "mc1322x.h"

/* th-12 */
#include "th-12.h"
#include "dht.h"

#define POST_INTERVAL (120 * CLOCK_SECOND)
#define ON_POWER_WAKE_TIME (30 * CLOCK_SECOND)

/* How long to wait before sleeping after starting the coap post */
/* will also sleep if a response to the post is recieved */
/* should be as short as possible */
#define SLEEP_AFTER_POST (0.05 * CLOCK_SECOND)

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12);

struct etimer et_do_dht, et_poweron_timeout;
static dht_result_t dht_current;
uip_ipaddr_t server_ipaddr;

/* value of sleep_ok determines if it is ok to sleep */
static uint32_t sleep_ok = 0;

/* time the next post is scheduled for: used to calculate how long to sleep */
static clock_time_t next_post;

/* used to go to sleep */
static struct ctimer ct_sleep;

char buf[256];

uint16_t create_dht_msg(dht_result_t *d, char *buf)
{
	rpl_dag_t *dag;
	uint8_t n = 0;
	rimeaddr_t *addr;
	uint16_t frac_t, int_t;
	char neg = ' ';

	addr = &rimeaddr_node_addr;

	if(d->t < 0) {
	  neg = '-';
	  int_t = (-1 * d->t)/10;
	  frac_t = (-1 * d->t) % 10;
	} else {
	  neg = ' ';
	  int_t = d->t/10;
	  frac_t = d->t % 10;
	}

	n += sprintf(&(buf[n]),"{\"eui\":\"%02x%02x%02x%02x%02x%02x%02x%02x\",\"t\":\"%c%d.%dC\",\"h\":\"%d.%d%%\",\"vb\":\"%dmV\"}",
		     addr->u8[0],
		     addr->u8[1],
		     addr->u8[2],
		     addr->u8[3],
		     addr->u8[4],
		     addr->u8[5],
		     addr->u8[6],
		     addr->u8[7],
		     neg,
		     int_t,
		     frac_t,
		     d->rh / 10,
		     d->rh % 10,
		     adc_vbatt
		);
	buf[n] = 0;
	PRINTF("buf: %s\n", buf);
	return n;
}

PROCESS_NAME(do_post);

void
go_to_sleep(void *ptr)
{

	PRINTF("before sleep\n\r");
	PRINTF("next post: %d\n\r", next_post);
	PRINTF("clock time: %d\n\r", clock_time());
	rtc_delay_ms(100);

	if(sleep_ok == 1) {
		PRINTF("go to sleep\n\r");
		/* sleep until we need to post */
		dht_uninit();
		
		rtimer_arch_sleep((next_post - clock_time() - 5) * (rtc_freq/CLOCK_CONF_SECOND));

		/* adjust the clock */
		/* currently this breaks everything after a few samples. not sure why. */
		/* also doesn't seem to be very critical */
//		clock_adjust_ticks((CRM->WU_COUNT*CLOCK_CONF_SECOND)/rtc_freq);
//		printf("adj: %d\n\r", CRM->WU_COUNT);
		dht_init();
	} else {
		PRINTF("can't sleep now, sleep not ok\n\r");
	}

	process_exit(&do_post);

	PRINTF("after sleep\n\r");
	PRINTF("next post: %d\n\r", next_post);
	PRINTF("clock time: %d\n\r", clock_time());
	if(etimer_expired(&et_do_dht)) {
		PRINTF("do dht IS expired\n\r");
	} else {
		PRINTF("do dht NOT expired\n\r");
	}

}

/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
void
client_chunk_handler(void *response)
{
  uint8_t *chunk;

	ctimer_stop(&ct_sleep);
  int len = coap_get_payload(response, &chunk);
  printf("|%.*s", len, (char *)chunk);
	go_to_sleep(NULL);
}

PROCESS(do_post, "post results");
PROCESS_THREAD(do_post, ev, data)
{
	PROCESS_BEGIN();
	static coap_packet_t request[1]; /* This way the packet can be treated as pointer as usual. */

	PRINTF("do post\n\r");

	/* we do a NON post since a CON could take 60 seconds to time out and we don't want to stay awake that long */
	coap_init_message(request, COAP_TYPE_NON, COAP_POST, 0 );
	coap_set_header_uri_path(request, "/th12");
	
	coap_set_payload(request, buf, strlen(buf));

	/* there is no good way to know if a NON request has finished */
	/* if it sucessful we might get a response back in client_chuck_handler */
	/* if we don't get a response back then we need to timeout and go back to sleep (potentially for a longer time than normal) */
	/* in fact, we don't really care about the response at all. A response just lets us go to sleep quicker (maybe) */
	/* the request should really go out within 50ms, so we will wait for that and then sleep */
	/* a one hop request will probably come back faster... */

	ctimer_set(&ct_sleep, SLEEP_AFTER_POST, go_to_sleep, NULL);

	COAP_BLOCKING_REQUEST(&server_ipaddr, REMOTE_PORT, request, client_chunk_handler);
	PRINTF("status %u: %s\n", coap_error_code, coap_error_message);
	
	PROCESS_END();
}

void do_result( dht_result_t d) {
	uint16_t frac_t, int_t;
	char neg = ' ';

	if (d.ok == 1) {

		adc_service();
		
		dht_current.t = d.t;
		dht_current.rh = d.rh;
		
#if DEBUG_ANNOTATE || DEBUG_FULL
		if(d.t < 0) {
			neg = '-';
			int_t = (-1 * d.t)/10;
			frac_t = (-1 * d.t) % 10;
		} else {
			neg = ' ';
			int_t = d.t/10;
			frac_t = d.t % 10;
		}
#endif
		
		ANNOTATE("temp: %c%d.%dC humid: %d.%d%%, ", neg, int_t, frac_t, d.rh / 10, d.rh % 10);
		ANNOTATE("vbatt: %dmV ", adc_vbatt);
		ANNOTATE("a5: %4dmV, a6: %4dmV ", adc_voltage(5), adc_voltage(6));
		ANNOTATE("\n\r");
		
		create_dht_msg(&d, buf);
		
		process_start(&do_post, NULL);

	} else {
		ANNOTATE("bad checksum\n\r");
		go_to_sleep(NULL);
	}

}

static struct ctimer ct_powerwake;
void
set_sleep_ok(void *ptr)
{
	PRINTF("Power ON wake timeout expired. Ok to sleep\n\r");
	/* XXX Todo: test doing a go_to_sleep here */
	gpio_reset(GPIO_43);
	sleep_ok = 1;
}

static struct ctimer ct_ledoff;
void
led_off(void *ptr)
{
	gpio_reset(KBI5);
}


static rpl_dag_t *dag;

PROCESS_THREAD(th_12, ev, data)
{
	PROCESS_BEGIN();

	/* Initialize the REST engine. */
	rest_init_engine();

	rplinfo_activate_resources();

	register_dht_result(do_result);
	
	PRINTF("Sleeping Temp/Humid Sensor\n\r");

#ifdef RPL_LEAF_ONLY
	PRINTF("RPL LEAF ONLY\n\r");
#endif

	/* use T2 as a debug pin */
	GPIO->FUNC_SEL.TMR2 = 3;
	GPIO->PAD_DIR_SET.TMR2 = 1;
	gpio_set(TMR2);

//	dht_uninit();
//	rtimer_arch_sleep(10 * rtc_freq);
//	maca_on();

	dht_init();
	adc_setup_chan(5);
	adc_setup_chan(6);

	etimer_set(&et_do_dht, POST_INTERVAL);
	ctimer_set(&ct_powerwake, ON_POWER_WAKE_TIME, set_sleep_ok, NULL);

	/* turn on RED led on power up for 2 secs. then turn off */
	GPIO->FUNC_SEL.KBI5 = 3;
	GPIO->PAD_DIR_SET.KBI5 = 1;
	gpio_set(KBI5);

	/* Turn on GREEN led we join a DAG. Turn off once we start sleeping */
	GPIO->FUNC_SEL.GPIO_43 = 3;
	GPIO->PAD_DIR_SET.GPIO_43 = 1;
	gpio_reset(GPIO_43);
	
	ctimer_set(&ct_ledoff, 2 * CLOCK_SECOND, led_off, NULL);

	dag = NULL;

	while(1) {

		if(dag == NULL) {
			dag = rpl_get_any_dag();
			if (dag != NULL) {
				uip_ipaddr_t *addr;
				gpio_set(GPIO_43);
				addr = &(dag->prefix_info.prefix);
				/* assume 64 bit prefix for now */
				memcpy(&server_ipaddr, addr, sizeof(uip_ipaddr_t));
				server_ipaddr.u16[3] = 0;
				server_ipaddr.u16[4] = 0;
				server_ipaddr.u16[5] = 0;
				server_ipaddr.u16[6] = 0;
				server_ipaddr.u16[7] = UIP_HTONS(1);
				PRINTF("joined DAG. Posting to ");
				PRINT6ADDR(&server_ipaddr);
				PRINTF("\n\r");
				/* queue up a post to get some instant satisfaction */
				etimer_set(&et_do_dht, 1 * CLOCK_SECOND);
			}

			PROCESS_PAUSE();

		} else {
			PROCESS_WAIT_EVENT();
		}

		PRINTF(".");
			
		if(ev == PROCESS_EVENT_TIMER && etimer_expired(&et_do_dht)) {
			PRINTF("post schedule\n\r");
			next_post = clock_time() + POST_INTERVAL;
			etimer_set(&et_do_dht, POST_INTERVAL);

			if(dag != NULL) {

				if(sleep_ok == 1) {
					dht_init();

					/* gpio_set(TMR2); */
					/* rtc_delay_ms(5); */
					/* gpio_reset(TMR2); */
					
					CRM->WU_CNTLbits.EXT_OUT_POL = 0xf; /* drive KBI0-3 high during sleep */
					rtimer_arch_sleep(2 * rtc_freq);
					maca_on();
					
					/* gpio_set(TMR2); */
					/* rtc_delay_ms(5); */
					/* gpio_reset(TMR2); */
					
					/* adjust the clock */
					/* can't really explain why you shouldn't adjust ticks */
					/* but you shouldn't... */
//					clock_adjust_ticks((CRM->WU_COUNT*CLOCK_CONF_SECOND)/rtc_freq);
//					printf("adj: %d\n\r", CRM->WU_COUNT);
				}

				process_start(&read_dht, NULL);
			}
		}		

	} 

	PROCESS_END();
}
