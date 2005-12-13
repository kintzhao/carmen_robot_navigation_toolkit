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

#include "robot_central.h"
#include "robot_main.h"
#include "robot_laser.h"

static double frontlaser_offset;
static double rearlaser_offset;

static double carmen_robot_laser_bearing_skip_rate = 0.33;

static carmen_laser_laser_message front_laser, rear_laser;
static int front_laser_count = 0, rear_laser_count = 0;
static int front_laser_ready = 0, rear_laser_ready = 0;
static carmen_robot_laser_message robot_front_laser, robot_rear_laser;

static double max_front_velocity = 0;
static double min_rear_velocity = -0;

double carmen_robot_interpolate_heading(double head1, double head2, double fraction);

static void 
publish_frontlaser_message(carmen_robot_laser_message laser_msg)
{
  IPC_RETURN_TYPE err;
  
  err = IPC_publishData(CARMEN_ROBOT_FRONTLASER_NAME, &laser_msg);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_ROBOT_FRONTLASER_NAME);
}

static void 
publish_rearlaser_message(carmen_robot_laser_message laser_msg)
{
  IPC_RETURN_TYPE err;

  err = IPC_publishData(CARMEN_ROBOT_REARLASER_NAME, &laser_msg);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_ROBOT_REARLASER_NAME);
}

static void
construct_laser_message(carmen_robot_laser_message *msg, double offset, 
			int rear, double timestamp)
{
  int low;
  int high;
  double skew;
  double fraction;
  int laser_ready;

  if (!rear) {
    laser_ready = carmen_robot_get_skew(front_laser_count, &skew,
					CARMEN_ROBOT_FRONT_LASER_AVERAGE, 
					msg->host);
    if (!laser_ready) {
      carmen_warn("Waiting for front laser data to accumulate.\n");
      return;
    }

  } else {
    laser_ready = carmen_robot_get_skew(rear_laser_count, &skew,
					CARMEN_ROBOT_REAR_LASER_AVERAGE, 
					msg->host);
    if (!laser_ready) {
      carmen_warn("Waiting for rear laser data to accumulate.\n");
      return;
    }
  }

  fraction = carmen_robot_get_fraction(timestamp, skew, &low, &high);

  msg->robot_pose.x=carmen_robot_odometry[low].x + fraction *
    (carmen_robot_odometry[high].x - carmen_robot_odometry[low].x);
  msg->robot_pose.y= carmen_robot_odometry[low].y + fraction *
    (carmen_robot_odometry[high].y - carmen_robot_odometry[low].y);
  msg->robot_pose.theta=carmen_robot_interpolate_heading
    (carmen_robot_odometry[high].theta, 
     carmen_robot_odometry[low].theta, fraction);

  msg->tv = carmen_robot_odometry[low].tv + 
    fraction*(carmen_robot_odometry[high].tv - 
	      carmen_robot_odometry[low].tv);
  msg->rv = carmen_robot_odometry[low].rv + 
    fraction*(carmen_robot_odometry[high].rv - 
	      carmen_robot_odometry[low].rv);

  if(rear)
    msg->laser_pose.theta = msg->robot_pose.theta + M_PI;
  else
    msg->laser_pose.theta = msg->robot_pose.theta;
  msg->laser_pose.theta = 
    carmen_normalize_theta(msg->laser_pose.theta);
  msg->laser_pose.x = msg->robot_pose.x + offset * 
    cos(msg->laser_pose.theta);
  msg->laser_pose.y = msg->robot_pose.y + offset * 
    sin(msg->laser_pose.theta);
}

void 
carmen_robot_correct_laser_and_publish(void) 
{
  if(!front_laser_ready && !rear_laser_ready) 
    return;

  if (front_laser_ready) {
    construct_laser_message(&robot_front_laser, frontlaser_offset, 
			    0, front_laser.timestamp);
    fprintf(stderr, "f");
    publish_frontlaser_message(robot_front_laser);
    front_laser_ready = 0;    
  }

  if (rear_laser_ready) {
    construct_laser_message(&robot_rear_laser, rearlaser_offset, 
			    1, rear_laser.timestamp);
    fprintf(stderr, "r");
    publish_rearlaser_message(robot_rear_laser);
    rear_laser_ready = 0;
  }
}

static void 
check_message_data_chunk_sizes(carmen_laser_laser_message *laser_ptr)
{
  static int first_front = 1, first_rear = 1;
  int first;
  carmen_robot_laser_message robot_laser;
  carmen_laser_laser_message laser;

  if (laser_ptr == &front_laser) {
    first = first_front;
    robot_laser = robot_front_laser;
  } else {
    first = first_rear;
    robot_laser = robot_rear_laser;
  }

  laser = *laser_ptr;

  if(first) {
    robot_laser.num_readings = laser.num_readings;
    robot_laser.range = 
      (float *)calloc(robot_laser.num_readings, sizeof(float));
    carmen_test_alloc(robot_laser.range);
    robot_laser.tooclose = 
      (char *)calloc(robot_laser.num_readings, sizeof(char));
    carmen_test_alloc(robot_laser.tooclose);   
    
    first = 0;
  } else if(robot_laser.num_readings != laser.num_readings) {
    robot_laser.num_readings = laser.num_readings;
    robot_laser.range = 
      (float *)realloc(robot_laser.range, 
		       sizeof(float) * robot_laser.num_readings);
    carmen_test_alloc(robot_laser.range);
    robot_laser.tooclose = (char *)realloc
      (robot_laser.tooclose, sizeof(char) * robot_laser.num_readings);
    carmen_test_alloc(robot_laser.tooclose);
  }

  if (laser_ptr == &front_laser) {
    first_front = first;
    robot_front_laser = robot_laser;
  } else {
    first_rear = first;
    robot_rear_laser = robot_laser;
  }
}

static void 
laser_frontlaser_handler(void)
{
  int i;
  double safety_distance;
  double theta, delta_theta;
  carmen_traj_point_t robot_posn, obstacle_pt;
  double max_velocity, velocity;

  int tooclose = -1;
  double tooclose_theta = 0;

  int min_index;
  double min_theta = 0;
  double min_dist;
  
  int debug = carmen_carp_get_verbose();

  static double time_since_last_process = 0;
  double skip_sum = 0;

  /* We just got a new laser message. It may be that the new message contains
     a different number of laser readings than we were expecting, maybe
     because the laser server was restarted. Here, we check to make sure that
     our outgoing messages can handle the same number of readings as the
     newly-arrived incoming message. */

  check_message_data_chunk_sizes(&front_laser);

  carmen_robot_update_skew(CARMEN_ROBOT_FRONT_LASER_AVERAGE, 
			   &front_laser_count, 
			   front_laser.timestamp, front_laser.host);

  memcpy(robot_front_laser.range, front_laser.range, 
	 robot_front_laser.num_readings * sizeof(float));
  memset(robot_front_laser.tooclose, 0, robot_front_laser.num_readings);


  if (carmen_get_time() - time_since_last_process < 
      1.0/carmen_robot_collision_avoidance_frequency)
    return;

  time_since_last_process = carmen_get_time();

  safety_distance = 
    carmen_robot_config.length / 2.0 + carmen_robot_config.approach_dist + 
    0.5 * carmen_robot_latest_odometry.tv * carmen_robot_latest_odometry.tv / 
    carmen_robot_config.acceleration +
    carmen_robot_latest_odometry.tv * carmen_robot_config.reaction_time;

  robot_front_laser.forward_safety_dist = safety_distance;
  robot_front_laser.side_safety_dist = 
    carmen_robot_config.width / 2.0 + carmen_robot_config.side_dist;
  robot_front_laser.turn_axis = 1e6;
  robot_front_laser.timestamp = front_laser.timestamp;
  robot_front_laser.host = front_laser.host;

  max_velocity = carmen_robot_config.max_t_vel;

  robot_posn.x = 0;
  robot_posn.y = 0;
  robot_posn.theta = 0;

  carmen_carp_set_verbose(0);
  
  theta = -M_PI/2; 
  delta_theta = M_PI/(robot_front_laser.num_readings-1);

  skip_sum = 0.0;
  for(i = 0; i < robot_front_laser.num_readings; i++, theta += delta_theta) {
    skip_sum += carmen_robot_laser_bearing_skip_rate;
    if (skip_sum > 0.95)
      {
	skip_sum = 0.0;
	robot_front_laser.tooclose[i] = -1;
	continue;
      }

    obstacle_pt.x = frontlaser_offset + 
      robot_front_laser.range[i] * cos(theta);
    obstacle_pt.y = robot_front_laser.range[i] * sin(theta);
    carmen_geometry_move_pt_to_rotating_ref_frame
      (&obstacle_pt, carmen_robot_latest_odometry.tv,
       carmen_robot_latest_odometry.rv);    
    velocity = carmen_geometry_compute_velocity
      (robot_posn, obstacle_pt, &carmen_robot_config);

    if (velocity < carmen_robot_config.max_t_vel) 
      {
	if (velocity < max_velocity)
	  {
	    tooclose = i;
	    tooclose_theta = theta;
	    max_velocity = velocity;
	  }
	robot_front_laser.tooclose[i] = 1;
    }
  } /* End of for(i = 0; i < robot_front_laser.num_readings; i++) */

  min_index = 0;
  min_dist = robot_front_laser.range[0];
  for(i = 1; i < robot_front_laser.num_readings; i++) { 
    if (robot_front_laser.range[i] < min_dist) {
      min_dist = robot_front_laser.range[i];
      min_index = i;
    }
  }

  carmen_carp_set_verbose(debug);
  if (max_velocity < carmen_robot_config.max_t_vel) {
    if (max_velocity > 0)
      carmen_verbose
	("velocity [46m%.1f[0m : distance %.4f delta_x %.4f (%.3f %d)\n", 
	 max_velocity,  robot_front_laser.range[min_index], 
	 frontlaser_offset + 
	 robot_front_laser.range[min_index]*cos(min_theta), 
	 cos(min_theta), min_index);
    else
	carmen_verbose
	  ("velocity [43m%.1f[0m : distance %.4f delta_x %.4f (%.3f %d)\n",
	   max_velocity, robot_front_laser.range[min_index], 
	   frontlaser_offset + 
	   robot_front_laser.range[min_index]*cos(min_theta), 
	   cos(min_theta), min_index);
  } else
    carmen_verbose("Ok\n");
  carmen_carp_set_verbose(0);

  front_laser_ready = 1;

  carmen_carp_set_verbose(debug);
  if (tooclose >= 0 && tooclose < robot_front_laser.num_readings) {
    if (max_velocity < carmen_robot_config.max_t_vel) {
      if (max_velocity > 0)
	carmen_verbose
	  ("velocity [42m%.1f[0m : distance %.4f delta_x %.4f (%.3f %d)\n",
	   max_velocity,  robot_front_laser.range[tooclose], 
	   frontlaser_offset + 
	   robot_front_laser.range[tooclose]*cos(tooclose_theta), 
	   cos(tooclose_theta), tooclose);
      else
	carmen_verbose
	  ("velocity [41m%.1f[0m : distance %.4f delta_x %.4f (%.3f %d)\n",
	   max_velocity, 
	   robot_front_laser.range[tooclose], 
	   frontlaser_offset + 
	   robot_front_laser.range[tooclose]*cos(tooclose_theta), 
	   cos(tooclose_theta), tooclose);
    } else
      carmen_verbose("Ok\n");
  } else {
    carmen_verbose("[44mNone too close[0m %.1f %.1f min_dist %.1f\n", 
		 carmen_robot_latest_odometry.tv, 
		 carmen_robot_latest_odometry.rv, min_dist );
  }
  carmen_carp_set_verbose(0);

  if (max_velocity >= 0 && max_velocity < CARMEN_ROBOT_MIN_ALLOWED_VELOCITY)
    max_velocity = 0.0;

  if(max_velocity <= 0.0 && carmen_robot_latest_odometry.tv > 0.0)
    {
      fprintf(stderr, "S");
      carmen_robot_stop_robot(CARMEN_ROBOT_ALLOW_ROTATE);
    }
  
  max_front_velocity = max_velocity;
  carmen_carp_set_verbose(debug);
}

double 
carmen_robot_laser_max_front_velocity(void) 
{
  return max_front_velocity;
}

double 
carmen_robot_laser_min_rear_velocity(void) 
{
  return min_rear_velocity;
}

static void 
laser_rearlaser_handler(void) 
{
  int i;
  double safety_distance;
  double theta, delta_theta;
  carmen_traj_point_t robot_posn, obstacle_pt;
  double min_velocity, velocity;

  int tooclose = -1;
  double tooclose_theta = 0;

  static double time_since_last_process = 0;
  double skip_sum = 0;

  /* We just got a new laser message. It may be that the new message contains
     a different number of laser readings than we were expecting, maybe
     because the laser server was restarted. Here, we check to make sure that
     our outgoing messages can handle the same number of readings as the
     newly-arrived incoming message. */

  check_message_data_chunk_sizes(&rear_laser);

  carmen_robot_update_skew(CARMEN_ROBOT_REAR_LASER_AVERAGE, 
			   &rear_laser_count, 
			   rear_laser.timestamp, rear_laser.host);

  memcpy(robot_rear_laser.range, rear_laser.range, 
	 robot_rear_laser.num_readings * sizeof(float));
  memset(robot_rear_laser.tooclose, 0, robot_rear_laser.num_readings);

  if (carmen_get_time() - time_since_last_process < 
      1.0/carmen_robot_collision_avoidance_frequency)
    return;

  time_since_last_process = carmen_get_time();

  safety_distance = 
    carmen_robot_config.length / 2.0 + carmen_robot_config.approach_dist + 
    0.5 * carmen_robot_latest_odometry.tv * carmen_robot_latest_odometry.tv / 
    carmen_robot_config.acceleration +
    carmen_robot_latest_odometry.tv * carmen_robot_config.reaction_time;

  robot_rear_laser.forward_safety_dist = safety_distance;
  robot_rear_laser.side_safety_dist = 
    carmen_robot_config.width / 2.0 + carmen_robot_config.side_dist;
  robot_rear_laser.turn_axis = 1e6;
  robot_rear_laser.timestamp = rear_laser.timestamp;
  robot_rear_laser.host = rear_laser.host;

  min_velocity = -carmen_robot_config.max_t_vel;

  robot_posn.x = 0;
  robot_posn.y = 0;
  robot_posn.theta = 0;

  theta = -M_PI/2; 
  delta_theta = M_PI/(robot_rear_laser.num_readings-1);
  skip_sum = 0.0;
  for(i = 0; i < robot_rear_laser.num_readings; i++, theta += delta_theta) {
    skip_sum += carmen_robot_laser_bearing_skip_rate;
    if (skip_sum > 0.95)
      {
	skip_sum = 0.0;
	robot_rear_laser.tooclose[i] = -1;
	continue;
      }

    obstacle_pt.x = rearlaser_offset + robot_rear_laser.range[i] * cos(theta);
    obstacle_pt.y = robot_rear_laser.range[i] * sin(theta);
    carmen_geometry_move_pt_to_rotating_ref_frame
      (&obstacle_pt, carmen_robot_latest_odometry.tv,
       carmen_robot_latest_odometry.rv);    
    velocity = carmen_geometry_compute_velocity
      (robot_posn, obstacle_pt, &carmen_robot_config);    
    velocity = -velocity;

    if (velocity > -carmen_robot_config.max_t_vel) {
      if (velocity > min_velocity)
	{
	  tooclose = i;
	  tooclose_theta = theta;
	  min_velocity = velocity;
	}
      robot_rear_laser.tooclose[i] = 1;
    }
  } /* End of for(i = 0; i < robot_rear_laser.num_readings; i++) */

  rear_laser_ready = 1;

  if (min_velocity <= 0 && min_velocity > -.03)
    min_velocity = 0.0;

  if(min_velocity >= 0.0 && carmen_robot_latest_odometry.tv < 0.0)
    {
      fprintf(stderr, "S");
      carmen_robot_stop_robot(CARMEN_ROBOT_ALLOW_ROTATE);
    }
  
  min_rear_velocity = min_velocity;
}

void 
carmen_robot_add_laser_handlers(void) 
{
  IPC_RETURN_TYPE err;

  /* define messages created by this module */
  err = IPC_defineMsg(CARMEN_ROBOT_FRONTLASER_NAME,
                      IPC_VARIABLE_LENGTH,
                      CARMEN_ROBOT_FRONTLASER_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_ROBOT_FRONTLASER_NAME);

  err = IPC_defineMsg(CARMEN_ROBOT_REARLASER_NAME,
                      IPC_VARIABLE_LENGTH,
                      CARMEN_ROBOT_REARLASER_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_ROBOT_REARLASER_NAME);

  carmen_laser_subscribe_frontlaser_message
    (&front_laser, (carmen_handler_t)laser_frontlaser_handler,
     CARMEN_SUBSCRIBE_LATEST);
  carmen_laser_subscribe_rearlaser_message
    (&rear_laser, (carmen_handler_t)laser_rearlaser_handler,
     CARMEN_SUBSCRIBE_LATEST);
  carmen_running_average_clear(CARMEN_ROBOT_FRONT_LASER_AVERAGE);
  carmen_running_average_clear(CARMEN_ROBOT_REAR_LASER_AVERAGE);
}

void 
carmen_robot_add_laser_parameters(char *progname) 
{
  int error;

  error = carmen_param_get_double("frontlaser_offset",&frontlaser_offset);
  carmen_param_handle_error(error, carmen_robot_usage, progname);
  error = carmen_param_get_double("rearlaser_offset", &rearlaser_offset);
  carmen_param_handle_error(error, carmen_robot_usage, progname);
  carmen_param_get_double("laser_bearing_skip_rate",
			  &carmen_robot_laser_bearing_skip_rate);
}

