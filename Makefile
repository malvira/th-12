all: th-12 

CONTIKI=./contiki
APPS += er-coap-07 erbium

APPDIRS += ${addprefix apps/, $(APPS)}

CFLAGS += -DWITH_COAP=7
CFLAGS += -DREST=coap_rest_implementation
CFLAGS += -DUIP_CONF_TCP=0

# variable for root Makefile.include
WITH_UIP6=1
# for some platforms
UIP_CONF_IPV6=1

PROJECT_SOURCEFILES += dht.c 

PROJECTDIRS += ./rplinfo
PROJECT_SOURCEFILES += rplinfo.c

include $(CONTIKI)/Makefile.include

