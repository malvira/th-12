#ifndef __SEN_H__
#define __SEN_H__

#include "contiki.h"

#define SEN_EN(void) gpio_set(KBI1)

#define SEN_PU(void) \
	GPIO->PAD_DIR_RESET.TMR1=1;\
	GPIO->FUNC_SEL.TMR1=1;\

#define SEN_OUT(void) \
	GPIO->PAD_DIR_SET.TMR1=1; \
	GPIO->FUNC_SEL.TMR1=3;\

PROCESS_NAME(sen_process);

void sen_init(void);

#endif /*__SEN_H__*/
