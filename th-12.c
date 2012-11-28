
/* debug */
#define DEBUG DEBUG_ANNOTATE
#include "net/uip-debug.h"

/* contiki */
#include "contiki.h"

/* mc1322x */
#include "mc1322x.h"

/* th-12 */
#include "th-12.h"
#include "dht.h"

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12);

struct etimer et_do_dht;

void do_result( dht_result_t d) {
	PRINTF("got result from DHT\n\r");
}

PROCESS_THREAD(th_12, ev, data)
{
	PROCESS_BEGIN();

	register_dht_result(do_result);
	
	PRINTF("Temp/Humid Sensor\n\r");
	
	dht_init();
	
	etimer_set(&et_do_dht, 1 * CLOCK_SECOND);

	while(1) {
		PROCESS_WAIT_EVENT();

		if(etimer_expired(&et_do_dht)) {
			etimer_set(&et_do_dht, 1 * CLOCK_SECOND);
			process_start(&read_dht, NULL);
		}		

	} 

	PROCESS_END();
}
