/*********************************************************
 *
 * This source code is part of the Carnegie Mellon Robot
 * Navigation Toolkit (CARMEN)
 *
 * CARMEN Copyright (c) 2002 Michael Montemerlo, Nicholas
 * Roy, and Sebastian Thrun
 *
 * CARMEN is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public 
 * License as published by the Free Software Foundation; 
 * either version 2 of the License, or (at your option)
 * any later version.
 *
 * CARMEN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied 
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more 
 * details.
 *
 * You should have received a copy of the GNU General 
 * Public License along with CARMEN; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, 
 * Suite 330, Boston, MA  02111-1307 USA
 *
 ********************************************************/

#include <carmen/carmen.h>

#include <sys/ioctl.h>
#include "cerebellum_com.h"

#ifdef _REENTRANT
#include <pthread.h>
static pthread_t main_thread;
static int thread_is_running = 0;
#endif

#define        CEREBELLUM_TIMEOUT          2.0

#define        METRES_PER_INCH        0.0254
#define        METRES_PER_CEREBELLUM        (METRES_PER_INCH/10.0)
#define        DEGREES_PER_CEREBELLUM       0.1

#define        WHEELBASE              (13.4 * METRES_PER_INCH)
#define        ROT_VEL_FACT_RAD       (WHEELBASE/METRES_PER_CEREBELLUM)

#define        MAXV                   (1.0/METRES_PER_CEREBELLUM)

static char *dev_name;
static int State[4];
static int set_velocity = 0;
static int command_vl = 0, command_vr = 0;
static double last_command = 0;
static double last_update = 0;
static int timeout = 0;
static int moving = 0;
static double current_acceleration;
static double stopping_acceleration;
static carmen_robot_config_t robot_config;
static carmen_base_odometry_message odometry;
static int use_sonar = 0;

static int 
initialize_robot(char *dev)
{
  int result;
  double acc;

  result = carmen_cerebellum_connect_robot(dev);
  if(result == FALSE)
    return -1;

  acc = robot_config.acceleration/METRES_PER_CEREBELLUM;
  result = carmen_cerebellum_ac(acc);
  current_acceleration = robot_config.acceleration;

  if(result == FALSE)
    return -1;

  last_update = carmen_get_time_ms();

  return 1;
}

static void 
reset_status(void)
{
  char *host;
  odometry.x = 0.0;
  odometry.y = 0.0;
  odometry.theta = 0.0;
  odometry.tv = 0.0;
  odometry.rv = 0.0;
  odometry.timestamp = 0.0;

  host = carmen_get_tenchar_host_name();
  strcpy(odometry.host, host);   
}

static int
update_status(void)  /* This function takes approximately 60 ms to run */
{
  double vl, vr;
  static double last_published_update;

  if (last_published_update >= odometry.timestamp)
    return 0; 

  /* update odometry message */
  
  vl = ((double)State[2]) * METRES_PER_CEREBELLUM;
  vr = ((double)State[3]) * METRES_PER_CEREBELLUM;

  odometry.tv = 0.5 * (vl + vr);
  odometry.rv = (vl - vr) / ROT_VEL_FACT_RAD;
  
  last_published_update = odometry.timestamp;

  return 1;
}

static void 
command_robot(void)
{
  double time_elapsed = carmen_get_time_ms() - last_command;
  double current_time; 
  int acc;

  if (set_velocity)
    {
      if(command_vl == 0 && command_vr == 0) 
	{
	  moving = 0;
	  carmen_warn("S");
	}
      else 
	{
	  if (!moving)
	    {
	      acc = robot_config.acceleration/METRES_PER_CEREBELLUM;
	      carmen_cerebellum_ac(acc);
	      current_acceleration = robot_config.acceleration;
	    }
	  carmen_warn("V");
	  moving = 1;
	}

      set_velocity = 0;
      timeout = 0;

      carmen_cerebellum_set_velocity(command_vl, command_vr);
      last_command = carmen_get_time_ms();
    }
  else if(time_elapsed > CEREBELLUM_TIMEOUT && !timeout) 
    {
      carmen_warn("T");
      command_vl = 0;
      command_vr = 0;
      timeout = 1;
      moving = 0;
      carmen_cerebellum_set_velocity(0, 0);
    }  
  else if (moving && current_acceleration == robot_config.acceleration)
    {
      acc = stopping_acceleration/METRES_PER_CEREBELLUM;
      carmen_cerebellum_ac(acc);
      current_acceleration = stopping_acceleration;
    }

  carmen_cerebellum_get_state(State+0, State+1, State+2, State+3);
  
  current_time = carmen_get_time_ms();
  last_update = current_time;
  odometry.timestamp = current_time;

  if(use_sonar) 
    {
      carmen_warn("Sonar not supported by Cerebellum module\n");
      use_sonar = 0;
      carmen_param_set_variable("robot_use_sonar", "off", NULL);
    }
}

static int
read_cerebellum_parameters(int argc, char **argv)
{
  int num_items;

  carmen_param_t param_list[] = {
    {"cerebellum", "dev", CARMEN_PARAM_STRING, &dev_name, 0, NULL},
    {"robot", "use_sonar", CARMEN_PARAM_ONOFF, &use_sonar, 1, NULL}, 
    {"robot", "max_t_vel", CARMEN_PARAM_DOUBLE, &(robot_config.max_t_vel), 
     1, NULL},
    {"robot", "max_r_vel", CARMEN_PARAM_DOUBLE, &(robot_config.max_r_vel), 
     1, NULL},
    {"robot", "acceleration", CARMEN_PARAM_DOUBLE, 
     &(robot_config.acceleration), 1, NULL},
    {"robot", "deceleration", CARMEN_PARAM_DOUBLE, 
     &(stopping_acceleration), 1, NULL}};

  num_items = sizeof(param_list)/sizeof(param_list[0]);
  carmen_param_install_params(argc, argv, param_list, num_items);

  if(use_sonar)
    {
      carmen_warn("Sonar not supported by Cerebellum module\n");
      use_sonar = 0;
      carmen_param_set_variable("robot_use_sonar", "off", NULL);
    }

  if (robot_config.acceleration > stopping_acceleration) 
    carmen_die("ERROR: robot_deceleration must be greater or equal "
	       "than robot_acceleration\n");
  
  return 0;
}

static void 
velocity_handler(MSG_INSTANCE msgRef, BYTE_ARRAY callData,
		 void *clientData __attribute__ ((unused)))
{
  IPC_RETURN_TYPE err;
  carmen_base_velocity_message vel;
  double vl, vr;

  FORMATTER_PTR formatter;
  
  formatter = IPC_msgInstanceFormatter(msgRef);
  err = IPC_unmarshallData(formatter, callData, &vel,
                     sizeof(carmen_base_velocity_message));
  IPC_freeByteArray(callData);

  carmen_test_ipc_return(err, "Could not unmarshall", 
			 IPC_msgInstanceName(msgRef));

  vl = vel.tv / METRES_PER_CEREBELLUM;
  vr = vel.tv / METRES_PER_CEREBELLUM;  

  vl -= 0.5 * vel.rv * ROT_VEL_FACT_RAD;
  vr += 0.5 * vel.rv * ROT_VEL_FACT_RAD;

  if(vl > MAXV)
    vl = MAXV;
  else if(vl < -MAXV)
    vl = -MAXV;
  if(vr > MAXV)
      vr = MAXV;
  else if(vr < -MAXV)
    vr = -MAXV;

  command_vl = (int)vl;
  command_vr = (int)vr;

  set_velocity = 1;
}

int 
carmen_cerebellum_initialize_ipc(void)
{
  IPC_RETURN_TYPE err;

  /* define messages created by cerebellum */
  err = IPC_defineMsg(CARMEN_BASE_ODOMETRY_NAME, IPC_VARIABLE_LENGTH,
                      CARMEN_BASE_ODOMETRY_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_BASE_ODOMETRY_NAME);

  err = IPC_defineMsg(CARMEN_BASE_VELOCITY_NAME, IPC_VARIABLE_LENGTH,
                      CARMEN_BASE_VELOCITY_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_BASE_VELOCITY_NAME);

  /* setup incoming message handlers */

  err = IPC_subscribe(CARMEN_BASE_VELOCITY_NAME, velocity_handler, NULL);
  carmen_test_ipc_exit(err, "Could not subscribe", CARMEN_BASE_VELOCITY_NAME);
  IPC_setMsgQueueLength(CARMEN_BASE_VELOCITY_NAME, 1);

  return IPC_No_Error;
}

#ifdef _REENTRANT
static void *
start_thread(void *data __attribute__ ((unused))) 
{
  while (1) {    
    command_robot();
    usleep(30000);
  }
  return(NULL);
}  
#endif


int 
carmen_cerebellum_start(int argc, char **argv)
{
  if (read_cerebellum_parameters(argc, argv) < 0)
    return -1;

  if (carmen_cerebellum_initialize_ipc() < 0) 
    {
      carmen_warn("\nError: Could not initialize IPC.\n");
      return -1;
    }

  if(initialize_robot(dev_name) < 0)
    {
      carmen_warn("\nError: Could not connect to robot. "
		  "Did you remember to turn the base on?\n");
      return -1;
    }

  reset_status();

  strcpy(odometry.host, carmen_get_tenchar_host_name());

#ifdef _REENTRANT
  pthread_create(&main_thread, NULL, start_thread, NULL);
  thread_is_running = 1;
#endif
  return 0;
}

int 
carmen_cerebellum_run(void) 
{
  IPC_RETURN_TYPE err;

  if (update_status() == 1)
    {
      err = IPC_publishData(CARMEN_BASE_ODOMETRY_NAME, &odometry);
      carmen_test_ipc_exit(err, "Could not publish", 
			   CARMEN_BASE_ODOMETRY_NAME);
    }
#ifndef _REENTRANT
  command_robot();
#endif
  
  return 1;
}

void 
carmen_cerebellum_shutdown(int x)
{
  if(x == SIGINT) 
    {
      carmen_verbose("\nShutting down robot...");
      sleep(1);
      carmen_cerebellum_set_velocity(0, 0);
      carmen_cerebellum_limp();
      last_update = carmen_get_time_ms();
      carmen_cerebellum_disconnect_robot();
      carmen_verbose("done.\n");
    }
}

void 
carmen_cerebellum_emergency_crash(int x __attribute__ ((unused)))
{
  carmen_cerebellum_set_velocity(0, 0);
  last_update = carmen_get_time_ms();
}
