#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "th-12.h"
#include "sen.h"

#include "mc1322x.h"

#include "dev/serial-line.h"

#if !UIP_CONF_IPV6_RPL && !defined (CONTIKI_TARGET_MINIMAL_NET)
#warning "Compiling with static routing!"
#include "static-routing.h"
#endif

#define DEBUG 0
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((u8_t *)addr)[0], ((u8_t *)addr)[1], ((u8_t *)addr)[2], ((u8_t *)addr)[3], ((u8_t *)addr)[4], ((u8_t *)addr)[5], ((u8_t *)addr)[6], ((u8_t *)addr)[7], ((u8_t *)addr)[8], ((u8_t *)addr)[9], ((u8_t *)addr)[10], ((u8_t *)addr)[11], ((u8_t *)addr)[12], ((u8_t *)addr)[13], ((u8_t *)addr)[14], ((u8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

th12_t sys;

PROCESS(th_12, "Temperature and Humidity enabled M-12");
AUTOSTART_PROCESSES(&th_12);

PROCESS_THREAD(th_12, ev, data)
{
	PROCESS_BEGIN();

	PRINTF("Temperature and Humidity enabled M-12\n");

  /* set up the shell */
	uart1_set_input(serial_line_input_byte);
	serial_line_init();
	serial_shell_init();
	shell_th12_init();

  /* initial system state */
	sys.cmd_mode = MODE_INITIALIZATION;
	sys.cmd_idx = 0;
	sys.sen_tcount=0;
	sys.sen_bcount=0;
	sys.sen_index=0;
	sys.sen_done=0;
  sen_init();

  while(1) {
	  PROCESS_WAIT_EVENT();
  } /* while (1) */

  PROCESS_END();
}
