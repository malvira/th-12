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

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0x2002, 0x3239, 0x614b, 0, 0, 0, 0, 1)

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12);

struct etimer et_do_dht;
static dht_result_t dht_current;
uip_ipaddr_t server_ipaddr;

char buf[64];

uint16_t create_dht_msg(dht_result_t *d, char *buf)
{
	rpl_dag_t *dag;
	uint8_t n = 0;
	rimeaddr_t *addr;

	addr = &rimeaddr_node_addr;

	n += sprintf(&(buf[n]),"{\"eui\":\"%02x%02x%02x%02x%02x%02x%02x%02x\",\"t\":\"%2d.%02dC\",\"h\":\"%2d.%02d%%\",\"vb\":\"%dmV\"}",
		     addr->u8[0],
		     addr->u8[1],
		     addr->u8[2],
		     addr->u8[3],
		     addr->u8[4],
		     addr->u8[5],
		     addr->u8[6],
		     addr->u8[7],
		     d->t  / 10,
		     d->t  % 10,
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
	       	
	COAP_BLOCKING_REQUEST(&server_ipaddr, REMOTE_PORT, request, client_chunk_handler);

	PROCESS_END();
}

void do_result( dht_result_t d) {
	adc_service();

	dht_current.t = d.t;
	dht_current.rh = d.rh;

	ANNOTATE("temp: %2d.%02dC humid: %2d.%02d%%, ", d.t/10, d.t % 10, d.rh / 10, d.rh % 10);
	ANNOTATE("vbatt: %dmV ", adc_vbatt);
	ANNOTATE("a5: %4dmV, a6: %4dmV ", adc_voltage(5), adc_voltage(6));
	ANNOTATE("\n\r");

	create_dht_msg(&d, buf);

	process_start(&do_post, NULL);
}

PROCESS_THREAD(th_12, ev, data)
{
	PROCESS_BEGIN();

	/* Initialize the REST engine. */
	rest_init_engine();

	rplinfo_activate_resources();

	register_dht_result(do_result);
	
	PRINTF("Temp/Humid Sensor\n\r");
	
	dht_init();
	adc_setup_chan(5);
	adc_setup_chan(6);
	
	etimer_set(&et_do_dht, 30 * CLOCK_SECOND);

	while(1) {
		PROCESS_WAIT_EVENT();

		if(etimer_expired(&et_do_dht)) {
			etimer_set(&et_do_dht, 30 * CLOCK_SECOND);
			process_start(&read_dht, NULL);
		}		

	} 

	PROCESS_END();
}
