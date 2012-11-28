all: th-12 

CONTIKI=./contiki

# variable for root Makefile.include
WITH_UIP6=1
# for some platforms
UIP_CONF_IPV6=1

PROJECT_SOURCEFILES += dht.c 

include $(CONTIKI)/Makefile.include

