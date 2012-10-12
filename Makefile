all: th-12 

CONTIKI=./contiki
#CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# variable for root Makefile.include
WITH_UIP6=1
# for some platforms
UIP_CONF_IPV6=1

APPS += serial-shell
PROJECT_SOURCEFILES += shell-th-12.c sen.c 

include $(CONTIKI)/Makefile.include

