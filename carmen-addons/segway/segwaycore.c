#include <carmen/carmen.h>
#include <canlib.h>
#include "segwaycore.h"

void segway_clear_status(segway_p segway)
{
  segway->status_ready = 0;
}

void segway_update_status(segway_p segway)
{
  short int temp;
  int tempint, err;
  long int id;
  unsigned char buffer[8];
  unsigned int dlc, flag;
  unsigned long time;
  double heading;

  /* read status message from the segway */
  err = canReadWait(segway->handle1, &id, &buffer, &dlc, &flag, &time, -1);
  if(err != 0)
    return;

  /* extract segway status from message */
  switch(id) {
  case SEGWAY_STATUS1_ID:
    temp = (buffer[4] << 8) | buffer[5];
    segway->voltage = temp;
    break;
  case SEGWAY_STATUS2_ID:
    temp = (buffer[0] << 8) | buffer[1];
    segway->pitch = temp / 7.8 * M_PI / 180.0;
    temp = (buffer[2] << 8) | buffer[3];
    segway->pitch_rate = temp / 7.8 * M_PI / 180.0;
    temp = (buffer[4] << 8) | buffer[5];
    segway->roll = temp / 7.8 * M_PI / 180.0;
    temp = (buffer[6] << 8) | buffer[7];
    segway->roll_rate = temp / 7.8 * M_PI / 180.0;
    break;
  case SEGWAY_STATUS3_ID:
    temp = (buffer[0] << 8) | buffer[1];
    segway->lw_velocity = temp / 332.0;
    temp = (buffer[2] << 8) | buffer[3];
    segway->rw_velocity = temp / 332.0;
    temp = (buffer[4] << 8) | buffer[5];
    segway->yaw_rate = temp / 7.8 * M_PI / 180.0;
    temp = (buffer[6] << 8) | buffer[7];
    segway->frame_counter = temp;
    break;
  case SEGWAY_STATUS4_ID:
    tempint = (buffer[2] << 24) | (buffer[3] << 16) |
      (buffer[0] << 8) | buffer[1];
    segway->lw_displacement = tempint / 33215.0;
    tempint = (buffer[6] << 24) | (buffer[7] << 16) |
      (buffer[4] << 8) | buffer[5];
    segway->rw_displacement = tempint / 33215.0;
    break;
  case SEGWAY_STATUS5_ID:
    segway->last_fa_displacement = segway->fore_aft_displacement;

    tempint = (buffer[2] << 24) | (buffer[3] << 16) |
      (buffer[0] << 8) | buffer[1];
    segway->fore_aft_displacement = tempint / 33215.0;
    tempint = (buffer[6] << 24) | (buffer[7] << 16) |
      (buffer[4] << 8) | buffer[5];
    segway->yaw_displacement = tempint / 112644.0 * M_PI * 2.0;
    
    if(segway->first_odom) {
      segway->last_fa_displacement = segway->fore_aft_displacement;
      segway->start_theta = segway->yaw_displacement;
      segway->first_odom = 0;
    }

    heading = carmen_normalize_theta(segway->yaw_displacement -
				     segway->start_theta);
    segway->x += (segway->fore_aft_displacement - 
		  segway->last_fa_displacement) * cos(heading);
    segway->y += (segway->fore_aft_displacement - 
		  segway->last_fa_displacement) * sin(heading);
    segway->theta = heading;

    segway->status_ready = 1;
    break;
  }
}

void segway_print_status(segway_p segway)
{
  fprintf(stderr, "%.0fV r(%.2f %.2f) p(%.2f %.2f) y(%.2f %.2f) vel(%.2f %.2f) \n",
	  segway->voltage, segway->roll, segway->roll_rate,
	  segway->pitch, segway->pitch_rate, 
	  segway->yaw_displacement, segway->yaw_rate,
	  segway->lw_velocity, segway->rw_velocity);
  fprintf(stderr, "Disp. lw/rw (%.3f %.3f) fore/aft %.3f yaw %.3f\n",
	  segway->lw_displacement, segway->rw_displacement,
	  segway->fore_aft_displacement, segway->yaw_displacement * 180 / M_PI);
}

void segway_initialize(segway_p segway)
{
  segway->handle1 = canOpenChannel(SEGWAY_CHANNEL1, 0);
  if(segway->handle1 < 0)
    carmen_die("Error: could not open connection to first CAN bus.\n");
  canSetBusParams(segway->handle1, SEGWAY_BITRATE, 6, 2, 2, 1, 0);
  canBusOn(segway->handle1);

  segway->handle2 = canOpenChannel(SEGWAY_CHANNEL2, 0);
  if(segway->handle2 < 0)
    carmen_die("Error: could not open connection to second CAN bus.\n");
  canSetBusParams(segway->handle2, SEGWAY_BITRATE, 6, 2, 2, 1, 0);
  canBusOn(segway->handle2);

  segway->first_odom = 1;
  segway->x = 0;
  segway->y = 0;
  segway->theta = 0;
}

void segway_free(segway_p segway)
{
  segway_stop(segway);
  canClose(segway->handle1);
  canClose(segway->handle2);
}

void segway_kill(segway_p segway)
{
  unsigned char message[8];
  
  canWrite(segway->handle1, 0x412, message, 8, 0);
  canWrite(segway->handle2, 0x412, message, 8, 0);
}

void segway_command(segway_p segway, double tv, double rv, 
		    unsigned short status_command, 
		    unsigned short status_parameter)
{
  int err;
  short int tv_short, rv_short;
  unsigned char message[8];

  /* convert velocities into message units */
  tv_short = (short int)(tv / 3.576 * 1176.0);
  rv_short = (short int)(rv / 0.3 * 100);
  if(tv_short > SEGWAY_MAX_TV)
    tv_short = SEGWAY_MAX_TV;
  if(tv_short < -SEGWAY_MAX_TV)
    tv_short = -SEGWAY_MAX_TV;
  if(rv_short > SEGWAY_MAX_RV)
    rv_short = SEGWAY_MAX_RV;
  if(rv_short < -SEGWAY_MAX_RV)
    rv_short = -SEGWAY_MAX_RV;
  message[0] = (tv_short >> 8);
  message[1] = (tv_short & 0xFF);
  message[2] = (rv_short >> 8);
  message[3] = (rv_short & 0xFF);
  message[4] = (status_command >> 8);
  message[5] = (status_command & 0xFF);
  message[6] = (status_parameter >> 8);
  message[7] = (status_parameter & 0xFF);
  err = canWrite(segway->handle1, SEGWAY_COMMAND_ID, message, 8, 0);
  err = canWrite(segway->handle2, SEGWAY_COMMAND_ID, message, 8, 0);

  /* update the status messages */
  segway_update_status(segway);
}

void segway_set_velocity(segway_p segway, double tv, double rv)
{
  segway_command(segway, tv, rv, 0, 0);
}

void segway_stop(segway_p segway)
{
  segway_command(segway, 0, 0, 0, 0);
}

void segway_set_max_velocity(segway_p segway, double percent)
{
  unsigned short param;

  param = (unsigned int)(percent * 16.0);
  if(param > 16)
    param = 16;
  segway_command(segway, 0.0, 0.0, SEGWAY_VEL_SCALE_FACTOR, param);
}

void segway_set_max_acceleration(segway_p segway, double percent)
{
  unsigned short param;

  param = (unsigned int)(percent * 16.0);
  if(param > 16)
    param = 16;
  segway_command(segway, 0.0, 0.0, SEGWAY_ACCEL_SCALE_FACTOR, param);
}

void segway_set_max_torque(segway_p segway, double percent)
{
  unsigned short param;

  param = (unsigned int)(percent * 16.0);
  if(param > 16)
    param = 16;
  segway_command(segway, 0.0, 0.0, SEGWAY_TORQUE_SCALE_FACTOR, param);
}

void segway_set_gain_schedule(segway_p segway, int schedule)
{
  segway_command(segway, 0.0, 0.0, SEGWAY_GAIN_SCHEDULE, (short int)schedule);
}


