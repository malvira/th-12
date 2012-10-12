#ifndef __TH12_H__
#define __TH12_H__

/* system command mode */
/* two modes of commands */
/* Initialization is a command sequence that initializes the power board */
/* Run is the normal command sequence that issues speed commands and monitors the power board */

/* TODO need a MODE_STOP and MODE_STARTUP */
/* depends on monitoring actual speed though */
enum {
	MODE_INITIALIZATION,
	MODE_RUN,
};

struct th12_state {
	uint8_t cmd_mode; /* either initialization or run mode*/
	uint8_t cmd_idx;   /* the command index in the block */
	uint8_t sen_tcount; // the sensor timer count
	uint8_t sen_bcount; // the sensor bit count
	uint8_t sen_index; //the sensor bit index
	uint8_t sen_data[5]; // sensor data [humid int, humid dec, temp int, temp dec, checksum]
	uint8_t sen_done; //finished reading flag
	uint8_t sen_debug[100];
	uint8_t sen_din;
	uint16_t count;   // debug
};
typedef struct th12_state th12_t;

extern th12_t sys;

#endif /* __SHELL_COMPRESSOR_H__ */
