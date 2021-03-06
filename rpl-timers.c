/**
 * \addtogroup uip6
 * @{
 */
/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
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
 *         RPL timer management.
 *
 * \author Joakim Eriksson <joakime@sics.se>, Nicolas Tsiftes <nvt@sics.se>
 */

#include <stdlib.h>
#include <errno.h>
#include <signal.h>

//#include "contiki-conf.h"
#include "rpl-private.h"
//#include "lib/random.h"
//#include "sys/ctimer.h"

#define DEBUG DEBUG_NONE
//#include "net/uip-debug.h"

/*---------------------------------------------------------------------------*/
static timer_t periodic_timer;

static void handle_periodic_timer(void *ptr);
static void new_dio_interval(rpl_instance_t *instance);
static void handle_dio_timer(void *ptr);

static uint16_t next_dis;

/* dio_send_ok is true if the node is ready to send DIOs */
static uint8_t dio_send_ok;

//TODO: set periodic timer to be periodic
//TODO: check DIS/DAO disabled for root node

static void
timerHandler(int sig, siginfo_t *si, void *uc)
{
  rpl_instance_t *instance;
  //instance = si->si_value.sival_ptr;
  timer_t *tidp;
  uint8_t instance_id = 30;
  tidp = si->si_value.sival_ptr;

  PRINTF("timerHandler\n");
  PRINTF("timerValue %p %i %i %i\n", tidp, sig, SIGUSR1, SIGUSR2);

  instance = rpl_get_instance(instance_id);
  if(instance == NULL) {
    PRINTF("Which RPL instance?");
    return;
  }

  if (sig == SIGUSR1) {
    PRINTF("DIO timer\n");
    handle_dio_timer((void*)instance);
  }

  /*
  if ( *tidp == firstTimerID )
    firstCB(sig, si, uc);
  else if ( *tidp == secondTimerID )
    secondCB(sig, si, uc);
  else if ( *tidp == thirdTimerID )
    thirdCB(sig, si, uc);
  */
}

/*---------------------------------------------------------------------------*/
static void
handle_periodic_timer(void *ptr)
{
  rpl_purge_routes();
  rpl_recalculate_ranks();

  /* handle DIS */
#ifdef RPL_DIS_SEND
  next_dis++;
  if(rpl_get_any_dag() == NULL && next_dis >= RPL_DIS_INTERVAL) {
    next_dis = 0;
    dis_output(NULL);
  }
#endif
  //ctimer_reset(&periodic_timer);
}

void rpl_init_timers(rpl_instance_t *instance) {
  struct sigaction sa;
  struct sigevent te;
  int ret;

  instance->dio_timer = malloc(sizeof(timer_t));

  /* Set up signal handler. */
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = timerHandler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR1, &sa, NULL) == -1) {
    PRINTF("Failed to setup signal handling.\n");
    return;
  }
  te.sigev_notify = SIGEV_SIGNAL;
  te.sigev_signo = SIGUSR1;
  te.sigev_value.sival_ptr = &instance->dio_timer;

  ret = timer_create(CLOCK_REALTIME, &te, instance->dio_timer);
  if (ret != 0) {
    PRINTF("RPL: Creating DIO timer failed '%s'.\n", strerror(errno));
    return;
  } else {
    PRINTF("RPL: DIO timer %p\n", &instance->dio_timer);
  }
}

/*---------------------------------------------------------------------------*/
static void
new_dio_interval(rpl_instance_t *instance)
{
  int ret;
  uint32_t time;
  //clock_time_t ticks;

  struct itimerspec its;

  /* TODO: too small timer intervals for many cases */
  time = 1UL << instance->dio_intcurrent;
  time *= 1000; // s -> ms
  instance->dio_next_delay = time;

  /* random number between I/2 and I */
  time = time/2 + (time / 2 * (uint32_t)rand()) / RAND_MAX;

  its.it_value.tv_sec = time / 1000000;
  its.it_value.tv_nsec = time % 1000000;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;

  /* Convert from milliseconds to CLOCK_TICKS. */
  /* ticks = (time * CLOCK_SECOND) / 1000; */
  /* instance->dio_next_delay = ticks; */

  /* random number between I/2 and I */
  /* ticks = ticks / 2 + (ticks / 2 * (uint32_t)random_rand()) / RANDOM_RAND_MAX; */

  /*
   * The intervals must be equally long among the nodes for Trickle to
   * operate efficiently. Therefore we need to calculate the delay between
   * the randomized time and the start time of the next interval.
   */
  //instance->dio_next_delay -= ticks;
  instance->dio_next_delay -= time;
  instance->dio_send = 1;

#if RPL_CONF_STATS
  /* keep some stats */
  instance->dio_totint++;
  instance->dio_totrecv += instance->dio_counter;
  ANNOTATE("#A rank=%u.%u(%u),stats=%d %d %d %d,color=%s\n",
	   DAG_RANK(instance->current_dag->rank, instance),
           (10 * (instance->current_dag->rank % instance->min_hoprankinc)) / instance->min_hoprankinc,
           instance->current_dag->version,
           instance->dio_totint, instance->dio_totsend,
           instance->dio_totrecv,instance->dio_intcurrent,
	   instance->current_dag->rank == ROOT_RANK(instance) ? "BLUE" : "ORANGE");
#endif /* RPL_CONF_STATS */

  /* reset the redundancy counter */
  instance->dio_counter = 0;

  /* schedule the timer */
  PRINTF("RPL: Scheduling DIO timer\n");
  //ctimer_set(&instance->dio_timer, ticks, &handle_dio_timer, instance);

  ret = timer_settime(*(instance->dio_timer), 0, &its, NULL);
  if (ret != 0) {
    PRINTF("RPL: Scheduling DIO timer failed '%s'.\n", strerror(errno));
    PRINTF("RPL: FAIL INFO: %i %i %i\n", &instance->dio_timer, its.it_value.tv_sec, its.it_value.tv_nsec);
  }

}
/*---------------------------------------------------------------------------*/
static void
handle_dio_timer(void *ptr)
{
  rpl_instance_t *instance;
  struct itimerspec its;
  int ret;

  its.it_value.tv_sec = 1;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;

  instance = (rpl_instance_t *)ptr;

  PRINTF("RPL: DIO Timer triggered\n");
  if(!dio_send_ok) {
    if(1/*uip_ds6_get_link_local(ADDR_PREFERRED) != NULL*/) {//TODO: link-local
      dio_send_ok = 1;
    } else {
      PRINTF("RPL: Postponing DIO transmission since link local address is not ok\n");
      //ctimer_set(&instance->dio_timer, CLOCK_SECOND, &handle_dio_timer, instance);

      ret = timer_settime(*(instance->dio_timer), 0, &its, NULL);
      if (ret != 0) {
	PRINTF("RPL: Scheduling DIO timer failed.\n");
      }

      return;
    }
  }

  if(instance->dio_send) {
    /* send DIO if counter is less than desired redundancy */
    if(instance->dio_counter < instance->dio_redundancy) {
#if RPL_CONF_STATS
      instance->dio_totsend++;
#endif /* RPL_CONF_STATS */
      dio_output(instance, NULL);
    } else {
      PRINTF("RPL: Supressing DIO transmission (%d >= %d)\n",
             instance->dio_counter, instance->dio_redundancy);
    }
    instance->dio_send = 0;
    PRINTF("RPL: Scheduling DIO timer %lu ticks in future (sent)\n",
           instance->dio_next_delay);
    //ctimer_set(&instance->dio_timer, instance->dio_next_delay, handle_dio_timer, instance);

    its.it_value.tv_sec = instance->dio_next_delay / 1000000;// TODO: check value
    its.it_value.tv_nsec = instance->dio_next_delay % 1000000;// TODO: check value
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    ret = timer_settime(*(instance->dio_timer), 0, &its, NULL);
    if (ret != 0) {
      PRINTF("RPL: Scheduling DIO timer failed.\n");
    }

  } else {
    /* check if we need to double interval */
    if(instance->dio_intcurrent < instance->dio_intmin + instance->dio_intdoubl) {
      instance->dio_intcurrent++;
      PRINTF("RPL: DIO Timer interval doubled %d\n", instance->dio_intcurrent);
    }
    new_dio_interval(instance);
  }
}
/*---------------------------------------------------------------------------*/
void
rpl_reset_periodic_timer(void)
{
  struct itimerspec its;
  int ret;

  its.it_value.tv_sec = 1;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;

  next_dis = RPL_DIS_INTERVAL / 2 +
    ((uint32_t)RPL_DIS_INTERVAL * (uint32_t)rand()) / RAND_MAX -
    RPL_DIS_START_DELAY;
  //ctimer_set(&periodic_timer, CLOCK_SECOND, handle_periodic_timer, NULL);
  ret = timer_settime(periodic_timer, 0, &its, NULL);
  if (ret != 0) {
    PRINTF("RPL: Scheduling DIS timer failed.\n");
  }
}
/*---------------------------------------------------------------------------*/
/* Resets the DIO timer in the instance to its minimal interval. */
void
rpl_reset_dio_timer(rpl_instance_t *instance)
{
#if !RPL_LEAF_ONLY
  /* Do not reset if we are already on the minimum interval,
     unless forced to do so. */
  if(instance->dio_intcurrent > instance->dio_intmin) {
    instance->dio_counter = 0;
    instance->dio_intcurrent = instance->dio_intmin;
    new_dio_interval(instance);
  }
#if RPL_CONF_STATS
  rpl_stats.resets++;
#endif /* RPL_CONF_STATS */
#endif /* RPL_LEAF_ONLY */
}
/*---------------------------------------------------------------------------*/
static void
handle_dao_timer(void *ptr)
{
  /* rpl_instance_t *instance; */
  /* struct itimerspec its; */
  /* int ret; */

  /* its.it_value.tv_sec = 1; */
  /* its.it_value.tv_nsec = 0; */
  /* its.it_interval.tv_sec = 0; */
  /* its.it_interval.tv_nsec = 0; */

  /* instance = (rpl_instance_t *)ptr; */

  /* if(!dio_send_ok && uip_ds6_get_link_local(ADDR_PREFERRED) == NULL) { */
  /*   PRINTF("RPL: Postpone DAO transmission\n"); */
  /*   //ctimer_set(&instance->dao_timer, CLOCK_SECOND, handle_dao_timer, instance); */
  /*   ret = timer_settime(*(instance->dao_timer), 0, &its, NULL); */
  /*   if (ret != 0) { */
  /*     PRINTF("RPL: Scheduling DIO timer failed.\n"); */
  /*   } */
  /*   return; */
  /* } */

  /* /\* Send the DAO to the DAO parent set -- the preferred parent in our case. *\/ */
  /* if(instance->current_dag->preferred_parent != NULL) { */
  /*   PRINTF("RPL: handle_dao_timer - sending DAO\n"); */
  /*   /\* Set the route lifetime to the default value. *\/ */
  /*   dao_output(instance->current_dag->preferred_parent, instance->default_lifetime); */
  /* } else { */
  /*   PRINTF("RPL: No suitable DAO parent\n"); */
  /* } */
  /* //ctimer_stop(&instance->dao_timer); */
  /* its.it_value.tv_sec = 0; */
  /* its.it_value.tv_nsec = 0; */
  /* its.it_interval.tv_sec = 0; */
  /* its.it_interval.tv_nsec = 0; */
  /* ret = timer_settime(*(instance->dao_timer), 0, &its, NULL); */
  /* if (ret != 0) { */
  /*   PRINTF("RPL: Stopping DIO timer failed.\n"); */
  /* } */
}
/*---------------------------------------------------------------------------*/
void
rpl_schedule_dao(rpl_instance_t *instance)
{
  //clock_time_t expiration_time;
  //expiration_time = etimer_expiration_time(&instance->dao_timer.etimer);

  struct itimerspec its;
  int ret;

  //TODO: check whether DAO is already running
  if(/*!etimer_expired(&instance->dao_timer.etimer)*/0) {
    PRINTF("RPL: DAO timer already scheduled\n");
  } else {
    /* expiration_time = RPL_DAO_LATENCY / 2 + */
    /*   (random_rand() % (RPL_DAO_LATENCY)); */
    /* PRINTF("RPL: Scheduling DAO timer %u ticks in the future\n", */
    /*        (unsigned)expiration_time); */
    /* ctimer_set(&instance->dao_timer, expiration_time, */
    /*            handle_dao_timer, instance); */

    //TODO: really use the calculation above....
    its.it_value.tv_sec = 1;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    ret = timer_settime(*(instance->dao_timer), 0, &its, NULL);
    if (ret != 0) {
      PRINTF("RPL: Stopping DIO timer failed.\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
