CONTIKI_PROJECT=er-server
all: $(CONTIKI_PROJECT)

CONTIKI=../..

MAKE_MAC = MAKE_MAC_TSCH
MODULES += os/services/orchestra
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

# define rest resource file
REST_RESOURCES_DIR = ./resources
REST_RESOURCES_FILES = $(notdir $(shell find $(REST_RESOURCES_DIR) -name '*.c'))

# add to project
PROJECTDIRS += $(REST_RESOURCES_DIR)
PROJECT_SOURCEFILES += $(REST_RESOURCES_FILES)

# add observe
# PROJECT_SOURCEFILES += er-observe-client.c

# REST Engine shall use Erbium CoAP implementation
MODULES += os/net/app-layer/coap

CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include
