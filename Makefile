all: 
	make TARGET=th12 coap-post
	make TARGET=th12-lowpower coap-post-sleep

ifndef TARGET
TARGET=th12
endif

CONTIKI=./contiki

APPS += er-coap-07 erbium
APPDIRS += ${addprefix apps/, $(APPS)}

TARGETDIRS += ./targets

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

clean:
	rm -f *~ *core core *.srec \
	*.lst *.map \
	*.cprg *.bin *.data contiki*.a *.firmware core-labels.S *.ihex *.ini \
	*.ce *.co $(CLEAN)
	-rm -rf obj_th12 obj_th12-lowpower
