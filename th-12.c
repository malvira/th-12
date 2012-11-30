#include <string.h>

/* contiki */
#include "contiki.h"
#include "contiki-net.h"
#include "net/rpl/rpl.h"

/* mc1322x */
#include "mc1322x.h"

/* th-12 */
#include "th-12.h"
#include "dht.h"

/* rplstats */
#include "httpd-ws.h"
#include "rplstats.h"

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12, &httpd_ws_process, &rplstats);

struct etimer et_do_dht;

static dht_result_t dht_current;

static const char ct_json[] = "application/json";
/*[2002:3239:614b:000b:0000:0000:0000:0000]*/
static char host[64] = "[aaaa::1]";
static uint16_t port = 80;
static char path[80] = "/sample";

static char buf[HTTPD_OUTBUF_SIZE];
static uint8_t buf_lock = 0;

static struct httpd_ws_state *s;

static
PT_THREAD(send_buf(struct httpd_ws_state *s))
{
	memcpy(s->outbuf, buf, HTTPD_OUTBUF_SIZE);
	s->outbuf_pos = strlen(buf);
	buf_lock = 0;

	PSOCK_BEGIN(&s->sout);
	if(s->outbuf_pos > 0) {
		SEND_STRING(&s->sout, s->outbuf, s->outbuf_pos);
		s->outbuf_pos = 0;
	}
	PSOCK_END(&s->sout);
}

uint16_t create_dht_msg(char *buf)
{
	rpl_dag_t *dag;
	uint8_t n = 0;
	buf_lock = 1;
	n += sprintf(&(buf[n]),"{\"t\":\"%d.%dC\", \"h\": \"%d.%d%%\", \"vb\":\"%dmV\" }", 
		     dht_current.t_i,
		     dht_current.t_d,
		     dht_current.rh_i,
		     dht_current.rh_d,
		     adc_vbatt
		);
	buf[n] = 0;
	PRINTF("buf: %s\n", buf);
	return n;
}

PROCESS(do_post, "post results");
PROCESS_THREAD(do_post, ev, data)
{
	rpl_dag_t *dag;
	uint16_t content_len;
	uip_ipaddr_t *addr;


	PROCESS_BEGIN();

	dag = rpl_get_any_dag();
	if(dag != NULL) {		  
		PRINTF("post!\n\r");
		PRINTF("prefix info, len %d\n\r", dag->prefix_info.length);
		PRINT6ADDR(&(dag->prefix_info.prefix));
		PRINTF("\n\r");
		addr = &(dag->prefix_info.prefix);
		/* assume 64 bit prefix for now */
		sprintf(host, "[%02x%02x:%02x%02x:%02x%02x:%02x%02x::1]", 
			((u8_t *)addr)[0], ((u8_t *)addr)[1], 
			((u8_t *)addr)[2], ((u8_t *)addr)[3], 
			((u8_t *)addr)[4], ((u8_t *)addr)[5], 
			((u8_t *)addr)[6], ((u8_t *)addr)[7]);
		PRINTF("host: %s\n\r", host);
		
		content_len = create_dht_msg(buf);
		s = httpd_ws_request(HTTPD_WS_POST, host, NULL, port,
				     path, ct_json,
				     content_len, send_buf);
		while (s->state != 0) { PROCESS_PAUSE(); }
	}

	PROCESS_END();

}

void do_result( dht_result_t d) {
	adc_service();

	dht_current.t_i = d.t_i;
	dht_current.t_d = d.t_d;
	dht_current.rh_i = d.rh_i;
	dht_current.rh_d = d.rh_d;

//	ANNOTATE("temp: %2d.%02dC humid: %2d.%02d%%, ", d.t_i, d.t_d, d.rh_i, d.rh_d);
//	ANNOTATE("vbatt: %dmV ", adc_vbatt);
//	ANNOTATE("a5: %4dmV, a6: %4dmV ", adc_voltage(5), adc_voltage(6));
//	ANNOTATE("\n\r");

	process_start(&do_post, NULL);

}


httpd_ws_script_t
httpd_ws_get_script(struct httpd_ws_state *s)
{
  return NULL;
}

PROCESS_THREAD(th_12, ev, data)
{
	PROCESS_BEGIN();

	register_dht_result(do_result);
	
	PRINTF("Temp/Humid Sensor\n\r");
	
	dht_init();
	adc_setup_chan(5);
	adc_setup_chan(6);
	
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
