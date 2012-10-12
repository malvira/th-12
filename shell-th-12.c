#include "contiki.h"
#include "contiki-conf.h"
#include "shell.h"
#include "shell-th-12.h"
#include "th-12.h"
#include "mc1322x.h"

#include <stdio.h>
#include <string.h>

PROCESS(shell_info_process, "info");
SHELL_COMMAND(info_command,
	      "info",
	      "info: print system info and current status",
	      &shell_info_process);

PROCESS_THREAD(shell_info_process, ev, data)
{
  int i;
  
  PROCESS_BEGIN();

  printf("TH-12 Command Mode: ");
  if(sys.cmd_mode == MODE_INITIALIZATION)
  {
	  printf("initializing\n\r");
  }
  if(sys.cmd_mode == MODE_RUN)
  {
	  printf("running\n\r");
  }
  printf("This is a call to all unknowns\n\r");

  PROCESS_END();
}
/*
PROCESS_THREAD(shell_fspeed_process, ev, data)
{
  static int num = 0;
  const char *nextptr;

  PROCESS_BEGIN();

  if(data != NULL) {
	  num = shell_strtolong(data, &nextptr);
	  if(num >=  0 && num <= 30)
	  {
		  sys.fan_speed = num;
		  TMR0->ENBL &= ~TMR_ENABLE_BIT(FAN_SPEED);
		  pwm_init_stopped(FAN_SPEED, 1000, fan_pwm_hz[num]);
		  // something about pwm_duty_ex is not process compatible 
//		  pwm_duty_ex(TMR_NUM(3), fan_pwm_hz[num]);
		  TMR0->ENBL |= TMR_ENABLE_BIT(FAN_SPEED);
	  }
	  if(sys.fan_speed == 0)
	  {
		  fan_disable();
		  sys.fan_en = 0;
		  printf("fan disabled\n\r");
	  } else
	  {
		  fan_enable();
		  sys.fan_en = 1;
		  printf("fan enabled\n\r");
	  }
  }

  PROCESS_END();
}
*/
/*
PROCESS(shell_lev_process, "l");
SHELL_COMMAND(lev_command,
	      "l",
	      "l: lev",
	      &shell_lev_process);


// TODO: make this l {a,b,c,d,f} {o,c} num 
// a-f is the lev to set 
// o is open and c is close 
// num is the number of steps to do in that direction 
// o, c, and num are reset on every new command 
PROCESS_THREAD(shell_lev_process, ev, data)
{
  static int pulses = 0;
  const char *nextptr;
  const char *next, *lev, *command, *num;

  PROCESS_BEGIN();

  if(data == NULL) {
    PROCESS_EXIT();
  }

  lev = data;

  next = strchr(lev, ' ');
  if(next == data) {
    PROCESS_EXIT();
  }
  command = next + 1;

  next = strchr(command, ' ');
  if(next == data) {
    PROCESS_EXIT();
  }
  num = next + 1;

  pulses = shell_strtolong(num, &nextptr);

  printf("lev %c command %c pulses %d\n\r", *lev, *command, pulses);

  if((*lev == 'a') ||
     (*lev == 'b') ||
     (*lev == 'c') ||
     (*lev == 'd') ||
     (*lev == 'f'))
  {
	  if((*command == 'o') ||
	     (*command == 'c'))
	  {
		  if(*command == 'o')
		  {
			  sys.lev[*lev-'a'] = -pulses;
		  } else
		  {
			  sys.lev[*lev - 'a'] = pulses;
		  }
		  process_poll(&lev_process);
	  } else
	  {
		  printf("unknown lev command %c\n\r", *command);
	  }
  } else {
	  printf("unknown lev: %c\n\r", *lev);
  }

  PROCESS_END();
}
*/

void
shell_th12_init(void)
{
  shell_register_command(&info_command);
}
