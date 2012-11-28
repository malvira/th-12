#ifndef __SEN_H__
#define __SEN_H__

#include "mc1322x.h"

#define DHT_EN(void) gpio_set(KBI1)

/* high impedance with pullup */
#define DHT_PU(void) \
	GPIO->PAD_DIR_RESET.TMR1=1;\
	GPIO->FUNC_SEL.TMR1=1;\

/* pin as output */
#define DHT_OUT(void) \
	GPIO->PAD_DIR_SET.TMR1=1; \
	GPIO->FUNC_SEL.TMR1=3;\

typedef struct dht_result {
	uint8_t rh_i; /* relative humidity in %: integer portion */
	uint8_t rh_d; /* RH%: decimal portion */
	uint8_t t_i;  /* temp in C: integer portion */
	uint8_t t_d;  /* temp decimal portion */
	uint8_t ok;   /* equals 1 if checksum was ok */
} dht_result_t;

PROCESS_NAME(read_dht);

void dht_init(void);

/* register a function to be called when the dht has a result */
/* the callback takes a dht_result_t */
/* void (*dht_result)(dht_result_t d); */
void register_dht_result( void (*dht_result_cb) );

#endif /*__SEN_H__*/
