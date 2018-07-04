/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      Erbium (Er) REST Engine example.
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <string.h>
#include "contiki.h"
#include "coap-engine.h"
#include "sixtop.h"
#include "sf-simple.h"

#include "sys/log.h"
#define LOG_MODULE "er-server"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "dev/leds.h"

/*
 * Resources to be activated need to be imported through the extern keyword.
 * The build system automatically compiles the resources in the corresponding sub-directory.
 */
extern coap_resource_t res_hello;
// extern coap_resource_t res_push;
extern coap_resource_t res_toggle;
// extern coap_resource_t res_collect;
extern coap_resource_t res_bcollect;

PROCESS(er_example_server, "Erbium Example Server");
PROCESS(print_schedule, "Print TSCH Schedule");
PROCESS(node_process, "RPL Node");
AUTOSTART_PROCESSES(&er_example_server, &node_process, &print_schedule);

PROCESS_THREAD(er_example_server, ev, data)
{
  PROCESS_BEGIN();

  PROCESS_PAUSE();

  LOG_INFO("Starting Erbium Example Server\n");
  leds_toggle(LEDS_GREEN);
#ifdef RF_CHANNEL
  LOG_INFO("RF channel: %u\n", RF_CHANNEL);
#endif
#ifdef IEEE802154_PANID
  LOG_INFO("PAN ID: 0x%04X\n", IEEE802154_PANID);
#endif

  LOG_INFO("uIP buffer: %u\n", UIP_BUFSIZE);
  LOG_INFO("LL header: %u\n", UIP_LLH_LEN);
  LOG_INFO("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  LOG_INFO("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  /* Initialize the REST engine. */
  coap_engine_init();

  /*
   * Bind the resources to their Uri-Path.
   * WARNING: Activating twice only means alternate path, not two instances!
   * All static variables are the same for each URI path.
   */
  coap_activate_resource(&res_hello, "test/hello");
  // coap_activate_resource(&res_push, "test/push");
  coap_activate_resource(&res_toggle, "actuators/toggle");
  // coap_activate_resource(&res_collect, "g/collect");
  coap_activate_resource(&res_bcollect, "g/bcollect");

  /* Define application-specific events here. */
  while(1) {
    PROCESS_WAIT_EVENT();
  }  /* while (1) */

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(print_schedule, ev, data) //for add delete test
{
  static struct etimer etaa;
  static uint8_t counter = 1;
  PROCESS_BEGIN();
  etimer_set(&etaa, CLOCK_SECOND * 600);
#if CONTIKI_TARGET_COOJA
  extern uint8_t event_threshold;
  while(1) {
    PROCESS_YIELD_UNTIL(etimer_expired(&etaa));
    etimer_reset(&etaa);
    if(counter%2 == 0) {
      event_threshold = 20;
    }else {
      event_threshold = 1;
    }
    printf("counter=%d set event_threshold=%d\n", counter, event_threshold);
    if(counter == 20) {
      break;
    }
    counter++;
  }
#endif /* CONTIKI_TARGET_COOJA */
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(node_process, ev, data)
{
  // static int added_num_of_links = 0;
  static struct etimer et;
  // struct tsch_neighbor *n;

  PROCESS_BEGIN();

  sixtop_add_sf(&sf_simple_driver);
  etimer_set(&et, CLOCK_SECOND * 3600);
#if CONTIKI_TARGET_COOJA && 0
  static uint8_t hours_count = 1;
  while (1) {
    PROCESS_YIELD_UNTIL(etimer_expired(&et));
    etimer_reset(&et);

    extern uint8_t event_threshold;
    
    switch (hours_count) {
      case 1:
        event_threshold = 5;
        break;
      case 2:
        event_threshold = 20;
        break;
      case 3:
        event_threshold = 10;
        break;
      case 4:
        event_threshold = 1;
        break;
      case 5:
        event_threshold = 10;
        break;
      case 6:
        event_threshold = 2;
        break;
      case 7:
        event_threshold = 20;
        break;
      case 8:
        event_threshold = 60;
        break;
      default:
        break;
    }    
    printf("hours_count=%d set event_threshold=%d\n", hours_count, event_threshold);
    hours_count++;
  }
#endif /* CONTIKI_TARGET_COOJA */

#if CONTIKI_TARGET_COOJA && 0
  PROCESS_YIELD_UNTIL(etimer_expired(&et));
  etimer_reset(&et);
#include "node-id.h"
  extern uint8_t event_threshold;
  if((node_id == 3) || (node_id == 12) || (node_id == 9) || (node_id == 14) || (node_id == 28)) {
    event_threshold = 1;
    printf("set event_threshold=%d\n", event_threshold);
  }
#endif /* CONTIKI_TARGET_COOJA */

  // etimer_set(&et, CLOCK_SECOND * 60);
  // while(1) {
  //   PROCESS_YIELD_UNTIL(etimer_expired(&et));
  //   etimer_reset(&et);

  //   /* Get time-source neighbor */
  //   n = tsch_queue_get_time_source();

  //   if ( (added_num_of_links == 2) || (added_num_of_links == 3))
  //   {
  //     printf("App : Add a link\n");
  //     sf_simple_add_links(&n->addr, 1);
  //   }
  //   else if (added_num_of_links == 4)
  //   {
  //     printf("App : Delete a link\n");
  //     sf_simple_remove_links(&n->addr);
  //   }
  //   added_num_of_links++;
  // }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
