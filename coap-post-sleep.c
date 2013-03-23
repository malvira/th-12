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

/* with 2 min post interval, sink checks every 4th wake, and up to 3 sink failures before reboot */
/* node should recover with in 8 min. Worst case would be 24min and triggering a reboot */ 

/* how long to wait between posts */
#define POST_INTERVAL (120 * CLOCK_SECOND)

/* stay awake for this long on power up */
#define ON_POWER_WAKE_TIME (120 * CLOCK_SECOND)

/* hostname for the sink */
static char sink_name[40] = "coap.lowpan.com";

/* perform a sink check this number of wake cycles */
/* 0 will always do a sink check */
/* a post sink will automatically checked and initialized on boot */
/* or when the sink URL has changed */
/* a sink check is a CON post instead of a NON */
/* the check is ok if the node gets a confirmation */
/* a bad resolve will also set sink_ok to 0 */
#define WAKE_CYCLES_PER_SINK_CHECK 4

/* after SINK_CHECK_TRIES of sink check failures, the node will reboot itself */
#define SINK_CHECK_TRIES 3

/* only report the battery voltage after */
/* the th12 has been running for BATTERY_DELAY */
/* the battery voltage input has a very slow time constant and on power up takes a while to reach */
/* the final voltage */
#define BATTERY_DELAY (100 * CLOCK_SECOND)

/* try to reread the sensor this many times if it reports a bad checksum before giving up */
#define SENSOR_RETRIES 3
static uint8_t sensor_tries;

/* how far in the future to schedule the retry. Should be short */
#define RETRY_INTERVAL (0.05 * CLOCK_SECOND)

/* How long to wait before sleeping after starting the coap post */
/* will also sleep if a response to the post is recieved */
/* should be as short as possible */
#define SLEEP_AFTER_POST (0.05 * CLOCK_SECOND)

/* give up waiting for a dag after DAG_TIMEOUT */
#define DAG_TIMEOUT (20 * CLOCK_SECOND)

/* debug */
#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)

PROCESS(th_12, "Temp/Humid Sensor");
AUTOSTART_PROCESSES(&th_12, &resolv_process);

struct etimer et_do_dht, et_poweron_timeout;
static dht_result_t dht_current;

/* sink state */
static uint8_t sink_ok = 0;
/* if sink hostname lookup is ok */
static int8_t resolv_ok = 0;
/* sink's ip address */
static uip_ipaddr_t *sink_addr;
/* number of wakes */
static uint8_t wakes = 0;
/* number of failed checks */
static uint8_t sink_checks_failed = 0;

/* flag tracks if this is the first post */
static uint8_t first_post = 1;

/* this is the corrected battery voltage */
/* the TH12 has a boost converter and so adc_vbatt cannot be used */
/* the battery voltage goes though a 0.5 voltage divider */
/* to adc0 */
static uint16_t vbatt;

/* value of sleep_ok determines if it is ok to sleep */
static uint32_t sleep_ok = 0;

/* flag to say if it's ok to report battery voltage */
static uint8_t report_batt = 0;
static struct ctimer ct_report_batt;

/* time the next post is scheduled for: used to calculate how long to sleep */
static clock_time_t next_post;

/* used to go to sleep */
static struct ctimer ct_sleep;

/* flag to test if con has failed or not */
static uint8_t con_ok;

/* track if we are doing a sensor retry or not */
static uint8_t retry = 0;


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

	/* {"eui":"ec473c4d12bdd1ce","t":" 22.1C","h":"18.3%","vb":"2678mV"} */
	n += sprintf(&(buf[n]),"{\"eui\":\"%02x%02x%02x%02x%02x%02x%02x%02x\",\"t\":\"%c%d.%dC\",\"h\":\"%d.%d%%\"",
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
		     vbatt
		);

	if (report_batt == 1) {
	  n += sprintf(&buf[n], ",\"vb\":\"%dmV\"}", vbatt);
	} else {
	  n += sprintf(&buf[n], "}");
	}

	buf[n] = 0;
	PRINTF("buf: %s\n", buf);
	return n;
}

uint16_t create_error_msg(char *error, char *buf)
{
	uint8_t n = 0;
	rimeaddr_t *addr;

	addr = &rimeaddr_node_addr;

	/* {"eui":"ec473c4d12bdd1ce","err":"sensor failed"} */
	n += sprintf(&(buf[n]),"{\"eui\":\"%02x%02x%02x%02x%02x%02x%02x%02x\",\"err\":\"%s\"}",
		     addr->u8[0],
		     addr->u8[1],
		     addr->u8[2],
		     addr->u8[3],
		     addr->u8[4],
		     addr->u8[5],
		     addr->u8[6],
		     addr->u8[7],
		     error
		);

	buf[n] = 0;
	PRINTF("buf: %s\n", buf);
	return n;
}


PROCESS_NAME(do_post);

void
go_to_sleep(void *ptr)
{

	if(sleep_ok == 1) {
		PRINTF("go to sleep\n\r");
		/* sleep until we need to post */
		dht_uninit();

		if(vbatt < 2700) {
		  /* drive KBI2 high during sleep */
		  /* to keep the boost on */
		  CRM->WU_CNTLbits.EXT_OUT_POL |= (1 << 2);
		  gpio_set(KBI1);
		} else {
		  CRM->WU_CNTLbits.EXT_OUT_POL &= ~(1 << 2);
		  gpio_reset(KBI1);
		}

		if (next_post > (clock_time() + 5)) {
		  rtimer_arch_sleep((next_post - clock_time() - 5) * (rtc_freq/CLOCK_CONF_SECOND));
		}

		dht_init();
	} else {
		PRINTF("can't sleep now, sleep not ok\n\r");
	}

	process_exit(&do_post);

}

/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
void
client_chunk_handler(void *response)
{
  const uint8_t *chunk;

  ctimer_stop(&ct_sleep);
  int len = coap_get_payload(response, &chunk);
  printf("|%.*s", len, (char *)chunk);

  if (len != 0) {
    sink_ok = 1;
    sink_checks_failed = 0;
    con_ok = 1;
    if (!sleep_ok) {
      gpio_set(GPIO_43);
    }
  }

  go_to_sleep(NULL);
}

static rpl_dag_t *dag;

static process_event_t ev_resolv_failed;
static process_event_t ev_post_con_started, ev_post_complete;

PROCESS(resolv_sink, "resolv sink hostname");
PROCESS_THREAD(resolv_sink, ev, data)
{

  PROCESS_BEGIN();

  static struct timer t_get_dag_timeout;
  timer_set(&t_get_dag_timeout, DAG_TIMEOUT);

  ev_resolv_failed = process_alloc_event();

  dag = NULL;

  while (dag == NULL && !timer_expired(&t_get_dag_timeout)) {

    PROCESS_PAUSE();

    dag = rpl_get_any_dag();

    if (dag != NULL ) {
      uip_ipaddr_t *addr;
      PRINTF("joined DAG.\n");

      PRINTF("Trying to resolv %s\n", sink_name);
      resolv_query(sink_name);

      PROCESS_WAIT_EVENT();

      if(ev == resolv_event_found) {
	PRINTF("resolv_event_found\n");

	if(resolv_lookup(sink_name, &sink_addr) == RESOLV_STATUS_CACHED) {
	  PRINT6ADDR(sink_addr);
	  PRINTF("\n\r");
	  resolv_ok = 1;
	} else {
	  PRINTF("host not found\n\r");
	  resolv_ok = 0;
	  process_post(&do_post, ev_resolv_failed, NULL);
	}

	PROCESS_EXIT();

      }
    }
  }

  if (timer_expired(&t_get_dag_timeout) ) {
    PRINTF("DAG timed out\n\r");
    resolv_ok = 0;
    process_post(&th_12, ev_post_complete, NULL);
  }

  PROCESS_END();
}

PROCESS(dummy, "dummy");
PROCESS_THREAD(dummy, ev, data)
{
  PROCESS_BEGIN();
  PRINTF("dummy\n");
  static coap_packet_t request[1]; /* This way the packet can be treated as pointer as usual. */
  coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0 );
  coap_set_header_uri_path(request, "/th12");
  coap_set_payload(request, buf, strlen(buf));
  COAP_BLOCKING_REQUEST(sink_addr, REMOTE_PORT, request, client_chunk_handler);
  PRINTF("dummy done\n");
  PROCESS_END();
}

PROCESS(do_post, "post results");
PROCESS_THREAD(do_post, ev, data)
{
  static uint8_t doing_con;

  PROCESS_BEGIN();
  static coap_packet_t request[1]; /* This way the packet can be treated as pointer as usual. */

  PRINTF("do post\n\r");

  /* we do a NON post since a CON could take 60 seconds to time out and we don't want to stay awake that long */
  if (!resolv_ok || (wakes % WAKE_CYCLES_PER_SINK_CHECK) == 0) {
    PRINTF("sink check with CON\n");
    resolv_ok = -1; sink_ok = 0;
    process_start(&resolv_sink, NULL);
    coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0 );
    con_ok = 0;
    process_post(&th_12, ev_post_con_started, NULL);
  } else {
    PRINTF("NON post\n");
    coap_init_message(request, COAP_TYPE_NON, COAP_POST, 0 );
  }
  coap_set_header_uri_path(request, "/th12");

  coap_set_payload(request, buf, strlen(buf));

  /* there is no good way to know if a NON request has finished */
  /* if it sucessful we might get a response back in client_chuck_handler */
  /* if we don't get a response back then we need to timeout and go back to sleep (potentially for a longer time than normal) */
  /* in fact, we don't really care about the response at all. A response just lets us go to sleep quicker (maybe) */
  /* the request should really go out within 50ms, so we will wait for that and then sleep */
  /* a one hop request will probably come back faster... */

  if (sink_ok == 1) {
    ctimer_set(&ct_sleep, SLEEP_AFTER_POST, go_to_sleep, NULL);
  }

  while (resolv_ok == -1) {
    PROCESS_PAUSE();
    if (resolv_ok == 0) {
      PRINTF("resolv failed\n");
      sink_checks_failed++;
      process_post(&th_12, ev_post_complete, NULL);
      PROCESS_EXIT();
    }
  }

  COAP_BLOCKING_REQUEST(sink_addr, REMOTE_PORT, request, client_chunk_handler);
  PRINTF("status %u: %s\n", coap_error_code, coap_error_message);
  if (con_ok == 0) {
    PRINTF("CON failed\n");
    sink_checks_failed++;
  }

  process_post(&th_12, ev_post_complete, NULL);


  PROCESS_END();
}

static process_event_t ev_sensor_retry_request;

void do_result( dht_result_t d) {
	uint16_t frac_t, int_t;
	char neg = ' ';

	sensor_tries++;

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
		ANNOTATE("a0: %4dmV, a5: %4dmV, a6: %4dmV ", adc_voltage(0), adc_voltage(5), adc_voltage(6));
		vbatt = adc_voltage(0) * 2;
		ANNOTATE("vbatt: %dmV ", vbatt);
		ANNOTATE("\n\r");

		create_dht_msg(&d, buf);

		/* NON posts leave the do_post process hanging around */
		/* kill it so we can start another */
		process_exit(&do_post);
		process_start(&do_post, NULL);

	} else {
		PRINTF("bad checksum\n\r");
		if(sensor_tries < SENSOR_RETRIES) {
		  PRINTF("retry sensor: %d\n\r", sensor_tries);
			process_post(&th_12, ev_sensor_retry_request, NULL);
		} else {
			PRINTF("too many sensor retries, giving up.\n\r");
			retry = 0;
			create_error_msg("sensor failed", buf);
			process_exit(&do_post);
			process_start(&do_post, NULL);
		}
	}

}

static struct ctimer ct_powerwake;
static void
set_sleep_ok(void *ptr)
{
	PRINTF("Power ON wake timeout expired. Ok to sleep\n\r");
	gpio_reset(GPIO_43);
	sleep_ok = 1;
	go_to_sleep(NULL);
}

static void
set_report_batt_ok(void *ptr)
{
	report_batt = 1;
}

static struct ctimer ct_ledoff;
void
led_off(void *ptr)
{
	gpio_reset(KBI5);
}

PROCESS_THREAD(th_12, ev, data)
{

  PROCESS_BEGIN();

  ev_post_con_started = process_alloc_event();
  ev_post_complete = process_alloc_event();
  ev_sensor_retry_request = process_alloc_event();

  /* Initialize the REST engine. */
  /* You need one of these */
  /* Otherwise things will "work" but fail in insidious ways */
//	coap_receiver_init();
  rest_init_engine();

  rplinfo_activate_resources();

  register_dht_result(do_result);

  PRINTF("Sleeping Temp/Humid Sensor\n\r");

#ifdef RPL_LEAF_ONLY
  PRINTF("RPL LEAF ONLY\n\r");
#endif

  /* boost enable pin */
  GPIO->FUNC_SEL.KBI2 = 3;
  GPIO->PAD_DIR_SET.KBI2 = 1;
  gpio_set(KBI2);

  /* use T2 as a debug pin */
  GPIO->FUNC_SEL.TMR2 = 3;
  GPIO->PAD_DIR_SET.TMR2 = 1;
  gpio_set(TMR2);

  dht_init();
  adc_setup_chan(0); /* battery voltage through divider */
  adc_setup_chan(5);
  adc_setup_chan(6);

  etimer_set(&et_do_dht, POST_INTERVAL);
  ctimer_set(&ct_powerwake, ON_POWER_WAKE_TIME, set_sleep_ok, NULL);
  ctimer_set(&ct_report_batt, BATTERY_DELAY, set_report_batt_ok, NULL);

  /* turn on RED led on power up for 2 secs. then turn off */
  GPIO->FUNC_SEL.KBI5 = 3;
  GPIO->PAD_DIR_SET.KBI5 = 1;
  gpio_set(KBI5);

  /* Turn on GREEN led we join a DAG. Turn off once we start sleeping */
  GPIO->FUNC_SEL.GPIO_43 = 3;
  GPIO->PAD_DIR_SET.GPIO_43 = 1;
  gpio_reset(GPIO_43);

  ctimer_set(&ct_ledoff, 2 * CLOCK_SECOND, led_off, NULL);

  /* report an initial sample */
  /* this will be a "sink check" and will wait for a DAG to be found and force a sink resolv */
  process_start(&read_dht, NULL);

  while(1) {

    PROCESS_WAIT_EVENT();

    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&et_do_dht)) {
      PRINTF("do_dht expired\n\r");
      PRINTF("sink_ok %d wakes %d failed %d retry %d\n\r", sink_ok, wakes, sink_checks_failed, retry);
      PRINTF("mod %d\n", wakes % WAKE_CYCLES_PER_SINK_CHECK);
      next_post = clock_time() + POST_INTERVAL;
      etimer_set(&et_do_dht, POST_INTERVAL);

      if (sink_checks_failed >= SINK_CHECK_TRIES) {
	if(vbatt > 2700) {
	  PRINTF("max sink failures reached, rebooting\n\r");
	  CRM->SW_RST = 0x87651234;
	  while (1) { continue; }
	} else {
	  /* If the battery voltage is too low, we can't reboot (has the boost will stop running) */
	  /* what else can we do to try to recover? */
	  /* restart RPL? */
	  /* redo resolv? */
	}
      }

      if(!retry) {
	wakes++;
      }

      if(sleep_ok == 1) {
	dht_init();

	CRM->WU_CNTLbits.EXT_OUT_POL = 0xf; /* drive KBI0-3 high during sleep */
	rtimer_arch_sleep(2 * rtc_freq);
	maca_on();

      }

      if (!retry) {
	sensor_tries = 0;
      }
      process_start(&read_dht, NULL);
    }

    if ( ev == ev_post_con_started) {
      etimer_stop(&et_do_dht);
      PRINTF("stopping do_dht timer: waiting for CON to complete\n\r");
    }

    if( ev == ev_post_complete ) {
      next_post = clock_time() + POST_INTERVAL;
      etimer_set(&et_do_dht, POST_INTERVAL);
      retry = 0;
      PRINTF("do_dht scheduled\n");
      go_to_sleep(NULL);
    }

    if ( ev == ev_sensor_retry_request ) {
      retry = 1;
      next_post = clock_time() + RETRY_INTERVAL;
      etimer_set(&et_do_dht, RETRY_INTERVAL);
      PRINTF("sensor failed schedule retry\n");
    }


  }

  PROCESS_END();
}
