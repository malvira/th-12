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

#define POST_INTERVAL (10 * CLOCK_SECOND)
#define ON_POWER_WAKE_TIME (30 * CLOCK_SECOND)

/* How long to wait before sleeping after starting the coap post */
/* will also sleep if a response to the post is recieved */
/* should be as short as possible */
#define SLEEP_AFTER_POST (0.05 * CLOCK_SECOND)

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)
//#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0x2002, 0xc63d, 0xeeef, 0, 0, 0, 0, 1)
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0x0000, 0x0000, 0, 0, 0, 0, 1)

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12);

struct etimer et_do_dht, et_poweron_timeout;
static dht_result_t dht_current;
uip_ipaddr_t server_ipaddr;

/* value of sleep_ok determines if it is ok to sleep */
static uint8_t sleep_ok = 0;

/* lock posting while they happen --- coap can take 62 secs to timeout and we don't want to queue many posts at a time */
static uint8_t post_ok = 1;

/* post_timed_out is cleared by the post callback routine. */
/* the callback routine isn't called if it never gets a response */
static uint8_t post_timed_out = 1;

/* time the next post is scheduled for: used to calculate how long to sleep */
static clock_time_t next_post;

/* used to go to sleep */
static struct ctimer ct_sleep;

char buf[64];

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
	PRINTF("go to sleep\n\r");
	if(sleep_ok) {
		/* sleep until we need to post */
		dht_uninit();
		rtimer_arch_sleep((next_post - clock_time() - 2) * (rtc_freq/CLOCK_CONF_SECOND));
		maca_on();
		/* adjust the clock */
		clock_adjust_ticks(CRM->WU_COUNT/CLOCK_CONF_SECOND);
		dht_init();
	}
	process_exit(&do_post);
	post_ok = 1;
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
	SERVER_NODE(&server_ipaddr);

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
}

static struct ctimer ct_powerwake;
void
set_sleep_ok(void *ptr)
{
	PRINTF("Power ON wake timeout expired. Ok to sleep\n\r");
	sleep_ok = 1;
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
	
	dht_init();
	adc_setup_chan(5);
	adc_setup_chan(6);

	etimer_set(&et_do_dht, POST_INTERVAL);
	ctimer_set(&ct_powerwake, ON_POWER_WAKE_TIME, set_sleep_ok, NULL);

	dag = NULL;

	while(1) {

		PROCESS_WAIT_EVENT();

		if(ev == PROCESS_EVENT_TIMER && etimer_expired(&et_do_dht)) {
			next_post = clock_time() + POST_INTERVAL;
			etimer_set(&et_do_dht, POST_INTERVAL);

			if(dag == NULL) {
				dag = rpl_get_any_dag();
				if (dag != NULL) {
					uip_ipaddr_t *addr;
					addr = &(dag->prefix_info.prefix);
					/* assume 64 bit prefix for now */
					memcpy(&server_ipaddr, addr, sizeof(uip_ipaddr_t));
					server_ipaddr.u16[7] = UIP_HTONS(1);
					PRINTF("joined DAG. Posting to ");
					PRINT6ADDR(&server_ipaddr);
					PRINTF("\n\r");
				}
			}

			if(dag != NULL && post_ok == 1) {
				/* lock doing more posts also until this finishes */
				post_ok = 0;
				process_start(&read_dht, NULL);
			}
		}		

	} 

	PROCESS_END();
}
