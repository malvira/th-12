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
#include "config.h"

/* th-12 */
#include "th-12.h"
#include "dht.h"

/* default POST location */
/* hostname for the sink */
#define DEFAULT_SINK_NAME "coap-8.lowpan.com" 
/* path to post to */
#define DEFAULT_SINK_PATH "/th12"

/* with 2 min post interval, sink checks every 4th wake, and up to 3 sink failures before reboot */
/* node should recover with in 8 min. Worst case would be 24min and triggering a reboot */ 

/* how long to wait between posts */
#define DEFAULT_POST_INTERVAL 120
/* stay awake for this long on power up */
#define DEFAULT_WAKE_TIME 120
/* perform a sink check this number of wake cycles */
#define DEFAULT_POSTS_PER_CHECK 4
/* after SINK_CHECK_TRIES of sink check failures, the node will reboot itself */
#define DEFAULT_MAX_POST_FAILS 3
/* whether or not the sensor is allowed to sleep */
#define DEFAULT_SLEEP_ALLOWED 1

/* MAX len for paths and hostnames */
#define SINK_MAXLEN 31

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

static void set_sleep_ok(void *ptr);
static struct ctimer ct_powerwake;
struct etimer et_do_dht, et_poweron_timeout;
static dht_result_t dht_current;

/* sink state */
static uint8_t sink_ok = 0;
/* if sink hostname lookup is ok */
static int8_t resolv_ok = 0;
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

/* other things we need */
static rpl_dag_t *dag;
static process_event_t ev_resolv_failed;
static process_event_t ev_post_con_started, ev_post_complete;


/* flash config */
/* MAX len for paths and hostnames */
#define SINK_MAXLEN 31

#define TH12_CONFIG_PAGE 0x1D000 /* nvm page where conf will be stored */
#define TH12_CONFIG_VERSION 1
#define TH12_CONFIG_MAGIC 0x5448

/* th12 config */
typedef struct {
  uint16_t magic; /* th12 magic number 0x5448 */
  uint16_t version; /* th12 config version number */
  uint8_t sink_name[SINK_MAXLEN + 1]; /* hostname for the sink */
  uint8_t sink_path[SINK_MAXLEN + 1]; /* path to post to */
  uint8_t post_interval; /* how long to wait between posts */
  uint8_t wake_time; /* stay awake for this long on power up */
  uint8_t posts_per_check; /* perform a sink check this number of wake cycles */
  uint8_t sleep_allowed; /* whether or not the sensor is allowed to sleep */
  uint8_t max_post_fails; /* after SINK_CHECK_TRIES of sink check failures, the node will reboot itself */
/* sink's ip address */
  uip_ipaddr_t sink_addr;
} TH12Config;

static TH12Config th12_cfg;

void 
th12_config_set_default(TH12Config *c) 
{
  c->magic = TH12_CONFIG_MAGIC;
  c->version = TH12_CONFIG_VERSION;
  strncpy(c->sink_name, DEFAULT_SINK_NAME, SINK_MAXLEN);
  strncpy(c->sink_path, DEFAULT_SINK_PATH, SINK_MAXLEN);
  c->post_interval = DEFAULT_POST_INTERVAL;
  c->wake_time = DEFAULT_WAKE_TIME;
  c->posts_per_check = DEFAULT_POSTS_PER_CHECK;
  c->sleep_allowed = DEFAULT_SLEEP_ALLOWED;
  c->max_post_fails = DEFAULT_MAX_POST_FAILS;
}

/* write out config to flash */
void th12_config_save(TH12Config *c) {
	nvmErr_t err;
	err = nvm_erase(gNvmInternalInterface_c, mc1322x_config.flags.nvmtype, 1 << TH12_CONFIG_PAGE/4096);
	err = nvm_write(gNvmInternalInterface_c, mc1322x_config.flags.nvmtype, (uint8_t *)c, TH12_CONFIG_PAGE, sizeof(TH12Config));
}

/* load the config from flash to the pass conf structure */
void th12_config_restore(TH12Config *c) {
	nvmErr_t err;
	nvmType_t type;
	if (mc1322x_config.flags.nvmtype == 0) { 
	  nvm_detect(gNvmInternalInterface_c, &type); 
	  mc1322x_config.flags.nvmtype = type;
	}
	err = nvm_read(gNvmInternalInterface_c, mc1322x_config.flags.nvmtype, c, TH12_CONFIG_PAGE, sizeof(TH12Config));
}

/* check the flash for magic number and proper version */
int th12_config_valid(TH12Config *c) {
	if (c->magic == TH12_CONFIG_MAGIC &&
	    c->version == TH12_CONFIG_VERSION) {
		return 1;
	} else {
#if DEBUG
		if (c->magic != TH12_CONFIG_MAGIC) { PRINTF("th12 config bad magic %04x\n\r", c->magic); }
		if (c->version != TH12_CONFIG_MAGIC) { PRINTF("th12 config bad version %04x\n\r", c->version); }
#endif
		return 0;
	}
}

void th12_config_print(void) {
	PRINTF("th12 config:\n\r");
	PRINTF("  magic:    %04x\n\r", th12_cfg.magic);
	PRINTF("  version:  %d\n\r",   th12_cfg.version);
	PRINTF("  sink name: %s\n\r", th12_cfg.sink_name);
	PRINTF("  sink path: %s\n\r",   th12_cfg.sink_path);
	PRINTF("  interval: %d\n\r",   th12_cfg.post_interval);
	PRINTF("  wake time: %d\n\r",   th12_cfg.wake_time);
	PRINTF("  posts per check: %d\n\r",   th12_cfg.posts_per_check);
	PRINTF("  max post fails: %d\n\r",   th12_cfg.max_post_fails);
	PRINTF("  sleep allowed: %d\n\r",   th12_cfg.sleep_allowed);
	PRINTF("  ip addr: ");
	PRINT6ADDR(&th12_cfg.sink_addr);
	PRINTF("\n\r");	
}

int
ipaddr_sprint(char *s, const uip_ipaddr_t *addr)
{
  uint16_t a;
  unsigned int i;
  int f;
  int n;
  n = 0;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) {
	n += sprintf(&s[n], "::");
      }
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
	n += sprintf(&s[n], ":");
      }
      n += sprintf(&s[n], "%x", a); 
    }
  }
  return n;
}

RESOURCE(config, METHOD_GET | METHOD_POST , "config", "title=\"Config parameters\";rt=\"Data\"");

void
config_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  uint8_t *param;
  uip_ipaddr_t *new_addr;
  const char *pstr;
  size_t len = 0;

  /* refresh the wake timer */
  ctimer_set(&ct_powerwake, th12_cfg.wake_time * CLOCK_SECOND, set_sleep_ok, NULL);
  
  if ((len = REST.get_query_variable(request, "param", &pstr))) {
    if (strncmp(pstr, "interval", len) == 0) {
      param = &th12_cfg.post_interval;
    } else if(strncmp(pstr, "wake_time", len) == 0) {
      param = &th12_cfg.wake_time;
    } else if(strncmp(pstr, "posts_per_check", len) == 0) {
      param = &th12_cfg.posts_per_check;
    } else if(strncmp(pstr, "max_post_fails", len) == 0) {
      param = &th12_cfg.max_post_fails;
    } else if(strncmp(pstr, "sleep_allowed", len) == 0) {
      param = &th12_cfg.sleep_allowed;
    } else if(strncmp(pstr, "channel", len) == 0) {
      param = &mc1322x_config.channel;
    } else if (strncmp(pstr, "netloc", len) == 0) {
      param = th12_cfg.sink_name;
    } else if(strncmp(pstr, "path", len) == 0) {
      param = th12_cfg.sink_path;
    } else if(strncmp(pstr, "ip", len) == 0) {
      new_addr = &th12_cfg.sink_addr;
    } else {
      goto bad;
    }
  } else {
    goto bad;
  }

  if (REST.get_method_type(request) == METHOD_POST) {
    const uint8_t *new;
    REST.get_request_payload(request, &new);
    if(strncmp(pstr, "channel", len) == 0) {
      *param = (uint8_t)atoi(new) - 11;
    } else if ( (strncmp(pstr, "netloc", len) == 0) || 
		(strncmp(pstr, "path", len) == 0) ) {
      strncpy(param, new, SINK_MAXLEN);
    } else if(strncmp(pstr, "ip", len) == 0) {
      uiplib_ipaddrconv(new, new_addr);
      PRINT6ADDR(new_addr);      
    }  else {
      *param = (uint8_t)atoi(new);
    }
    th12_config_save(&th12_cfg);
    
    /* do clean-up actions */
    if (strncmp(pstr, "interval", len) == 0) {
      /* send a post_complete event to schedule a post with the new interval */
      process_post(&th_12, ev_post_complete, NULL);
    } else if(strncmp(pstr, "wake_time", len) == 0) {
      ctimer_set(&ct_powerwake, th12_cfg.wake_time * CLOCK_SECOND, set_sleep_ok, NULL);
    } else if ( (strncmp(pstr, "netloc", len) == 0) || 
		(strncmp(pstr, "path", len) == 0)  ||
		(strncmp(pstr, "ip", len) == 0) ) {
      sink_ok = 0; resolv_ok = 0; wakes = 0;
      process_start(&read_dht, NULL);
    } else if(strncmp(pstr, "channel", len) == 0) {
      set_channel(mc1322x_config.channel);
      mc1322x_config_save(&mc1322x_config);
      CRM->SW_RST = 0x87651234;
      while (1) { continue; }
    }

  } else { /* GET */
    uint8_t n;
    if (strncmp(pstr, "channel", len) == 0) {
      n = sprintf(buffer, "%d", *param + 11);
    } else if ( (strncmp(pstr, "netloc", len) == 0) || 
		(strncmp(pstr, "path", len) == 0) ) {
      strncpy(buffer, param, SINK_MAXLEN);
      REST.set_response_payload(response, buffer, strlen(param));
    } else if ( (strncmp(pstr, "ip", len) == 0)) {
      n = ipaddr_sprint(buffer, &th12_cfg.sink_addr);
    } else {
      n = sprintf(buffer, "%d", *param);
    }
    REST.set_response_payload(response, buffer, n);
  }

  return;

bad:
  REST.set_response_status(response, REST.status.BAD_REQUEST);

}

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

      PRINTF("Trying to resolv %s\n", th12_cfg.sink_name);
      resolv_query(th12_cfg.sink_name);

      PROCESS_WAIT_EVENT();

      if(ev == resolv_event_found) {
	uip_ipaddr_t *addr;
	PRINTF("resolv_event_found\n");

	addr = &(th12_cfg.sink_addr);
	if(resolv_lookup(th12_cfg.sink_name, &addr) == RESOLV_STATUS_CACHED) {
	  PRINT6ADDR(addr);
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
  }

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
  if (!resolv_ok || (wakes % th12_cfg.posts_per_check ) == 0) {
    PRINTF("sink check with CON\n");
    resolv_ok = -1; sink_ok = 0;
    if (strncmp("", th12_cfg.sink_name, SINK_MAXLEN) == 0) {
      PRINTF("sink name null, trying with ip ");
      PRINT6ADDR(&th12_cfg.sink_addr);
      PRINTF("\n\r");
      resolv_ok = 1;
    } else {
      process_start(&resolv_sink, NULL);
    }
    coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0 );
    con_ok = 0;
    process_post(&th_12, ev_post_con_started, NULL);
  } else {
    PRINTF("NON post\n");
    coap_init_message(request, COAP_TYPE_NON, COAP_POST, 0 );
  }
  coap_set_header_uri_path(request, th12_cfg.sink_path);

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

  COAP_BLOCKING_REQUEST(&th12_cfg.sink_addr, REMOTE_PORT, request, client_chunk_handler);
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

static void
set_sleep_ok(void *ptr)
{
	PRINTF("Power ON wake timeout expired. Ok to sleep\n\r");
	gpio_reset(GPIO_43);
	if (th12_cfg.sleep_allowed) {
	  sleep_ok = 1;
	  go_to_sleep(NULL);
	}
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

//  rplinfo_activate_resources();
  rest_activate_resource(&resource_config);

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

  /* turn on RED led on power up for 2 secs. then turn off */
  GPIO->FUNC_SEL.KBI5 = 3;
  GPIO->PAD_DIR_SET.KBI5 = 1;
  gpio_set(KBI5);

  /* Turn on GREEN led we join a DAG. Turn off once we start sleeping */
  GPIO->FUNC_SEL.GPIO_43 = 3;
  GPIO->PAD_DIR_SET.GPIO_43 = 1;
  gpio_reset(GPIO_43);

  /* get the config from flash or make a default config */
  th12_config_restore(&th12_cfg);
  if (!th12_config_valid(&th12_cfg)) {
    th12_config_set_default(&th12_cfg);
    th12_config_save(&th12_cfg);
  }
  th12_config_print();

  ctimer_set(&ct_ledoff, 2 * CLOCK_SECOND, led_off, NULL);

  /* do an initial post on startup */
  /* this will be a "sink check" and will wait for a DAG to be found and force a sink resolv */
  etimer_set(&et_do_dht, 5 * CLOCK_SECOND);
  ctimer_set(&ct_powerwake, th12_cfg.wake_time * CLOCK_SECOND, set_sleep_ok, NULL);
  ctimer_set(&ct_report_batt, BATTERY_DELAY, set_report_batt_ok, NULL);

  while(1) {

    PROCESS_WAIT_EVENT();

    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&et_do_dht)) {
      PRINTF("do_dht expired\n\r");
      PRINTF("sink_ok %d wakes %d failed %d retry %d\n\r", sink_ok, wakes, sink_checks_failed, retry);
      PRINTF("mod %d\n", wakes % th12_cfg.posts_per_check);
      next_post = clock_time() + th12_cfg.post_interval * CLOCK_SECOND;
      etimer_set(&et_do_dht, th12_cfg.post_interval * CLOCK_SECOND);

      if (sink_checks_failed >= th12_cfg.max_post_fails) {
	if(vbatt > 2700) {
	  PRINTF("max sink failures reached, rebooting\n\r");
	  CRM->SW_RST = 0x87651234;
	  while (1) { continue; }
	} else {
	  /* If the battery voltage is too low, we can't reboot (has the boost will stop running) */
	  /* restart RPL and try again... */
	  sink_checks_failed = 0;
	  rpl_init();
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
      next_post = clock_time() + th12_cfg.post_interval * CLOCK_SECOND;
      etimer_set(&et_do_dht, th12_cfg.post_interval * CLOCK_SECOND);
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
