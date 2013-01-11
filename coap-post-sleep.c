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

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)
//#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0x2002, 0x3239, 0x614b, 0, 0, 0, 0, 1)
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0x0000, 0x0000, 0, 0, 0, 0, 1)

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12);

struct etimer et_do_dht, et_poweron_timeout;
static dht_result_t dht_current;
uip_ipaddr_t server_ipaddr;

/* value of sleep_ok determines if it is ok to sleep */
static uint8_t sleep_ok = 0;

/* time the next post is scheduled for: used to calculate how long to sleep */
static clock_time_t next_post;

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

/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
void
client_chunk_handler(void *response)
{
  uint8_t *chunk;

  int len = coap_get_payload(response, &chunk);
  printf("|%.*s", len, (char *)chunk);
}

PROCESS(do_post, "post results");
PROCESS_THREAD(do_post, ev, data)
{
	PROCESS_BEGIN();
	static coap_packet_t request[1]; /* This way the packet can be treated as pointer as usual. */
	SERVER_NODE(&server_ipaddr);

	coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0 );
	coap_set_header_uri_path(request, "/th12");
	
	coap_set_payload(request, buf, strlen(buf));

	/* lock doing more posts also until this finishes */
	sleep_ok = 0;
	COAP_BLOCKING_REQUEST(&server_ipaddr, REMOTE_PORT, request, client_chunk_handler);
	PRINTF("status %u: %s\n", coap_error_code, coap_error_message);
	sleep_ok = 1; /* it could take a while to get here if all tranmissions fail */

	/* should check if this try failed */
	/* if so, sleep for a while before trying again */

	if(sleep_ok) {
		/* sleep until we need to post */
		dht_uninit();
//		rtimer_arch_sleep((next_post - clock_time() - 2) * (rtc_freq/CLOCK_CONF_SECOND));
		/* adjust the clock */
//		clock_adjust_ticks(CRM->WU_COUNT/CLOCK_CONF_SECOND);
		dht_init();
	}

	
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

	while(1) {

		PROCESS_WAIT_EVENT();

		if(ev == PROCESS_EVENT_TIMER && etimer_expired(&et_do_dht)) {
			next_post = clock_time() + POST_INTERVAL;
			etimer_set(&et_do_dht, POST_INTERVAL);
			process_start(&read_dht, NULL);
		}		

	} 

	PROCESS_END();
}
