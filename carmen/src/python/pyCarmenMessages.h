#include "pyCarmen.h"

//static MessageHandler *_callback;



/*Deal with the front laser handler here*/

static MessageHandler *_laser_callback;
class front_laser{
 public:
  static void lazer_msg(carmen_robot_laser_message *msg)
  {
    _laser_callback->set_front_laser_message(msg);
    if (_laser_callback) _laser_callback->run_cb("front_laser", "get_front_laser_message()");
  }

  front_laser(MessageHandler *cb)
  {
    _laser_callback = cb;
    carmen_robot_subscribe_frontlaser_message
      (NULL, (carmen_handler_t)lazer_msg, CARMEN_SUBSCRIBE_LATEST);
  }
};


/*Deal with the rear laser handler here*/
static MessageHandler *_rlaser_callback;
class rear_laser{
 public:
  static void lazer_msg_rear(carmen_robot_laser_message *msg)
  {
    _rlaser_callback->set_rear_laser_message(msg);
    if (_rlaser_callback) _rlaser_callback->run_cb("rear_laser", "get_rear_laser_message()");
  }

  rear_laser(MessageHandler *cb)
  {
    _rlaser_callback = cb;
    carmen_robot_subscribe_rearlaser_message
      (NULL, (carmen_handler_t)lazer_msg_rear, CARMEN_SUBSCRIBE_LATEST);
  }
};


/*Deal with the rear laser handler here*/
static MessageHandler *_global_pose_callback;
class global_pose{
 public:

  static void my_callback(carmen_localize_globalpos_message *msg)
  {
    _global_pose_callback->set_globalpos_message(msg);
    if (_global_pose_callback) _global_pose_callback->run_cb("global_pose", "get_globalpos_message()");
  }

  global_pose(MessageHandler *cb)
  {    
    _global_pose_callback = cb;
    carmen_localize_subscribe_globalpos_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};

/*Deal with the odometry handler here*/
static MessageHandler *_odometry_callback;
class odometry {
 public:

  static void my_callback(carmen_base_odometry_message *msg)
  {
    _odometry_callback->set_odometry_message(msg);
    if (_odometry_callback) _odometry_callback->run_cb("odometry", "get_odometry_message()");
  }

  odometry(MessageHandler *cb)
  {    
    _odometry_callback = cb;
    carmen_base_subscribe_odometry_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};


/*Deal with the odometry handler here*/
static MessageHandler *_sonar_callback;
class sonar {
 public:

  static void my_callback(carmen_base_sonar_message *msg)
  {
    _sonar_callback->set_sonar_message(msg);
    if (_sonar_callback) _sonar_callback->run_cb("sonar", "get_sonar_message()");
  }

  sonar(MessageHandler *cb)
  {    
    _sonar_callback = cb;
    carmen_base_subscribe_sonar_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};


/*Deal with the bumper handler here*/
static MessageHandler *_bumper_callback;
class bumper {
 public:

  static void my_callback(carmen_base_bumper_message *msg)
  {
    _bumper_callback->set_bumper_message(msg);
    if (_bumper_callback) _bumper_callback->run_cb("bumper", "get_bumper_message()");
  }

  bumper(MessageHandler *cb)
  {    
    _bumper_callback = cb;
    carmen_base_subscribe_bumper_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};

/*Deal with the Navigator Status handler here*/
static MessageHandler *_navigator_status_callback;
class navigator_status {
 public:

  static void my_callback(carmen_navigator_status_message *msg)
  {
    _navigator_status_callback->set_navigator_status_message(msg);
    if (_navigator_status_callback) _navigator_status_callback->run_cb("navigator_status", "get_navigator_status_message()");
  }

  navigator_status(MessageHandler *cb)
  { 
    _navigator_status_callback = cb;
    carmen_navigator_subscribe_status_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};


/*Deal with the Navigator Plan handler here*/
static MessageHandler *_navigator_plan_callback;
class navigator_plan {
 public:

  static void my_callback(carmen_navigator_plan_message *msg)
  {
    _navigator_plan_callback->set_navigator_plan_message(msg);
    if (_navigator_plan_callback) _navigator_plan_callback->run_cb("navigator_plan", "get_navigator_plan_message()");
  }

  navigator_plan(MessageHandler *cb)
  { 
    _navigator_plan_callback = cb;
    carmen_navigator_subscribe_plan_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};


/*Deal with the Navigator Stopped handler here*/
static MessageHandler *_navigator_stopped_callback;
class navigator_stopped {
 public:

  static void my_callback(carmen_navigator_autonomous_stopped_message *msg)
  {
    _navigator_stopped_callback->set_navigator_autonomous_stopped_message(msg);
    if (_navigator_stopped_callback) _navigator_stopped_callback->run_cb("navigator_stopped", "get_navigator_autonomous_stopped_message()");
  }

  navigator_stopped(MessageHandler *cb)
  { 
    _navigator_stopped_callback = cb;
    carmen_navigator_subscribe_autonomous_stopped_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};



/*Deal with the arm handler here*/
static MessageHandler *_arm_callback;
class arm{
 public:
  static void my_callback(carmen_arm_state_message *msg)
  {
    _arm_callback->set_arm_state_message(msg);
    if (_arm_callback) _arm_callback->run_cb("arm_state", "get_arm_state_message()");
  }

  arm(MessageHandler *cb)
  {
    _arm_callback = cb;
    carmen_arm_subscribe_state_message
      (NULL, (carmen_handler_t)my_callback, CARMEN_SUBSCRIBE_LATEST);
  }
};
