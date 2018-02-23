/*
 * Copyright (c) 2015, SICS Swedish ICT.
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
 */
/**
 * \file
 *         A RPL+TSCH node able to act as either a simple node (6ln),
 *         DAG Root (6dr) or DAG Root with security (6dr-sec)
 *         Press use button at startup to configure.
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki.h"
#include "node-id.h"
#include "rpl.h"
#include "rpl-dag-root.h"
#include "sys/log.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-log.h"
#if UIP_CONF_IPV6_RPL_LITE == 0
#include "rpl-private.h"
#endif /* UIP_CONF_IPV6_RPL_LITE == 0 */
#include "orchestra.h"

#define DEBUG DEBUG_PRINT
#include "net/ipv6/uip-debug.h"

/*---------------------------------------------------------------------------*/
PROCESS(node_process, "RPL Node");
PROCESS(print_process, "TSCH Schedule");
AUTOSTART_PROCESSES(&node_process, &print_process);

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(node_process, ev, data)
{
  int is_coordinator;

  PROCESS_BEGIN();

  is_coordinator = 0;

#if CONTIKI_TARGET_COOJA
  is_coordinator = (node_id == 1);
#endif

  if(is_coordinator) {
    rpl_dag_root_init_dag_immediately();
    printf("is_coordinator\n");
  }
  NETSTACK_MAC.on();
  orchestra_init();

#if WITH_PERIODIC_ROUTES_PRINT
  {
    static struct etimer et;
    /* Print out routing tables every minute */
    etimer_set(&et, CLOCK_SECOND * 60);
    while(1) {
      /* Used for non-regression testing */
      #if RPL_WITH_STORING
        PRINTF("Routing entries: %u\n", uip_ds6_route_num_routes());
      #endif
      #if RPL_WITH_NON_STORING
        PRINTF("Routing links: %u\n", rpl_ns_num_nodes());
      #endif
      PROCESS_YIELD_UNTIL(etimer_expired(&et));
      etimer_reset(&et);
    }
  }
#endif /* WITH_PERIODIC_ROUTES_PRINT */

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
#include "tsch-schedule.h"
PROCESS_THREAD(print_process, ev, data)
{
  static struct etimer etaa;
  PROCESS_BEGIN();

  etimer_set(&etaa, CLOCK_SECOND * 30);
  while(1) {
    PROCESS_YIELD_UNTIL(etimer_expired(&etaa));
    etimer_reset(&etaa);
    tsch_schedule_print();
    printf("------------------------\n");
  }

  PROCESS_END();
}
