#include <carmen/carmen.h>
#include "dot_messages.h"
#include "dot.h"


static double default_person_filter_a;
static double default_person_filter_px;
static double default_person_filter_py;
static double default_person_filter_pxy;
static double default_person_filter_qx;
static double default_person_filter_qy;
static double default_person_filter_qxy;
static double default_person_filter_rx;
static double default_person_filter_ry;
static double default_person_filter_rxy;
static double default_trash_filter_px;
static double default_trash_filter_py;
static double default_trash_filter_a;
static double default_trash_filter_qx;
static double default_trash_filter_qy;
static double default_trash_filter_qxy;
static double default_trash_filter_rx;
static double default_trash_filter_ry;
static double default_trash_filter_rxy;
static double default_door_filter_px;
static double default_door_filter_py;
static double default_door_filter_pt;
static double default_door_filter_a;
static double default_door_filter_qx;
static double default_door_filter_qy;
static double default_door_filter_qxy;
static double default_door_filter_qt;
static double default_door_filter_rx;
static double default_door_filter_ry;
static double default_door_filter_rt;

static int new_filter_threshold;
static double new_cluster_threshold;
static double map_diff_threshold;
static double map_occupied_threshold;
static double sensor_update_dist;
static int sensor_update_cnt;

static int person_filter_velocity_window;
static double person_filter_velocity_threshold;
static int kill_hidden_person_cnt;


static carmen_localize_globalpos_message odom;
static carmen_point_t last_sensor_update_odom;
static int do_sensor_update = 1;
static carmen_map_t static_map;
static carmen_dot_filter_p filters = NULL;
static int num_filters = 0;
static double laser_max_range;


static void person_filter_velocity_window_handler() {

  if (person_filter_velocity_window > MAX_PERSON_FILTER_VELOCITY_WINDOW)
    person_filter_velocity_window = MAX_PERSON_FILTER_VELOCITY_WINDOW;
}

/***************************
static void publish_pos_msg() {

  static carmen_dot_pos_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    first = 0;
  }

  msg.timestamp = carmen_get_time_ms();

  err = IPC_publishData(CARMEN_DOT_POS_MSG_NAME, &msg);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_DOT_POS_MSG_NAME);
}
************************/

static inline double dist(double dx, double dy) {

  return sqrt(dx*dx+dy*dy);
}

static void rotate2d(double *x, double *y, double theta) {

  double x2, y2;

  x2 = *x;
  y2 = *y; 

  *x = x2*cos(theta) - y2*sin(theta);
  *y = x2*sin(theta) + y2*cos(theta);
}

/*
 * inverts 2d matrix [[a, b],[c, d]]
 */
static int invert2d(double *a, double *b, double *c, double *d) {

  double det;
  double a2, b2, c2, d2;

  det = (*a)*(*d) - (*b)*(*c);
  
  if (fabs(det) <= 0.00000001)
    return -1;

  a2 = (*d)/det;
  b2 = -(*b)/det;
  c2 = -(*c)/det;
  d2 = (*a)/det;

  *a = a2;
  *b = b2;
  *c = c2;
  *d = d2;

  return 0;
}

/*
 * computes the orientation of the bivariate normal with variances
 * vx, vy, and covariance vxy.  returns an angle in [-pi/2, pi/2].
 */
static double bnorm_theta(double vx, double vy, double vxy) {

  double theta;
  double e1, e2;  // eigenvalues
  double ex, ey;  // major eigenvector

  e1 = (vx + vy)/2.0 + sqrt(vxy*vxy + (vx-vy)*(vx-vy)/4.0);
  e2 = (vx + vy)/2.0 - sqrt(vxy*vxy + (vx-vy)*(vx-vy)/4.0);
  ex = vxy;
  ey = (e1>e2?e1:e2) - vx;

  theta = atan2(ey, ex);

  return theta;
}

static int dot_classify(carmen_dot_filter_p f) {

  carmen_dot_person_filter_p pf;
  carmen_dot_trash_filter_p tf;
  carmen_dot_door_filter_p df;
  double pdet, tdet, ddet;
  int type;

  if (!f->allow_change)
    return f->type;

  pf = &f->person_filter;
  tf = &f->trash_filter;
  df = &f->door_filter;

  pdet = pf->px*pf->py - pf->pxy*pf->pxy;
  tdet = tf->px*tf->py;
  ddet = df->px*df->py - df->pxy*df->pxy;

  return (pdet <= tdet ? CARMEN_DOT_PERSON : CARMEN_DOT_TRASH);  //dbug

  if (pdet <= tdet) {
    if (pdet <= ddet)
      type = CARMEN_DOT_PERSON;
    else
      type = CARMEN_DOT_DOOR;
  }
  else {
    if (tdet <= ddet)
      type = CARMEN_DOT_TRASH;
    else
      type = CARMEN_DOT_DOOR;
  }

  return type;
}

static double person_filter_velocity(carmen_dot_person_filter_p f) {

  int i;
  double vx, vy;

  if (f->vlen < person_filter_velocity_window)
    return 0.0;

  vx = 0.0;
  vy = 0.0;
  for (i = 0; i < person_filter_velocity_window; i++) {
    vx += f->vx[i];
    vy += f->vy[i];
  }
  vx /= (double)i;
  vy /= (double)i;

  printf("vel = %.4f\n", dist(vx, vy));

  return dist(vx, vy);
}

static void person_filter_motion_update(carmen_dot_person_filter_p f) {

  double ax, ay;
  
  // kalman filter time update with brownian motion model
  ax = carmen_uniform_random(-1.0, 1.0) * f->a;
  ay = carmen_uniform_random(-1.0, 1.0) * f->a;

  //printf("ax = %.4f, ay = %.4f\n", ax, ay);

  f->x += ax;
  f->y += ay;
  f->px = ax*ax*f->px + f->qx;
  f->pxy = ax*ax*f->pxy + f->qxy;
  f->py = ay*ay*f->py + f->qy;

  f->vx[f->vpos] = ax;
  f->vy[f->vpos] = ay;
  f->vpos = (f->vpos+1) % person_filter_velocity_window;
  if (f->vlen < person_filter_velocity_window)
    f->vlen++;
  else
    f->vlen = person_filter_velocity_window;
}

static void person_filter_sensor_update(carmen_dot_person_filter_p f,
				       double x, double y) {

  double pxx, pxy, pyx, pyy;
  double mxx, mxy, myx, myy;
  double kxx, kxy, kyx, kyy;
  double x2, y2;

  //printf("update_person_filter()\n");

  pxx = f->px;
  pxy = f->pxy;
  pyx = f->pxy;
  pyy = f->py;

  //printf("pxx = %.4f, pxy = %.4f, pyx = %.4f, pyy = %.4f\n", pxx, pxy, pyx, pyy);

  // kalman filter sensor update
  mxx = pxx + f->rx;
  mxy = pxy + f->rxy;
  myx = pyx + f->rxy;
  myy = pyy + f->ry;

  //printf("mxx = %.4f, mxy = %.4f, myx = %.4f, myy = %.4f\n", mxx, mxy, myx, myy);

  if (invert2d(&mxx, &mxy, &myx, &myy) < 0) {
    carmen_warn("Error: can't invert matrix M");
    return;
  }

  //printf("mxx = %.4f, mxy = %.4f, myx = %.4f, myy = %.4f\n", mxx, mxy, myx, myy);

  kxx = pxx*mxx+pxy*myx;
  kxy = pxx*mxy+pxy*myy;
  kyx = pyx*mxx+pyy*myx;
  kyy = pyx*mxy+pyy*myy;

  //printf("kxx = %.4f, kxy = %.4f, kyx = %.4f, kyy = %.4f\n", kxx, kxy, kyx, kyy);

  x2 = f->x + kxx*(x - f->x) + kxy*(y - f->y);
  y2 = f->y + kyx*(x - f->x) + kyy*(y - f->y);
  f->vx[f->vpos] = x2 - f->x;
  f->vy[f->vpos] = y2 - f->y;
  f->vpos = (f->vpos+1) % person_filter_velocity_window;
  if (f->vlen < person_filter_velocity_window)
    f->vlen++;
  else
    f->vlen = person_filter_velocity_window;
  f->x = x2;
  f->y = y2;
  f->px = (1.0 - kxx)*pxx + (-kxy)*pyx;
  f->pxy = (1.0 - kxx)*pxy + (-kxy)*pyy;
  f->py = (-kyx)*pxy + (1.0 - kyy)*pyy;
}

static void trash_filter_motion_update(carmen_dot_trash_filter_p f) {

  double ax, ay;
  
  // kalman filter time update with brownian motion model
  ax = carmen_uniform_random(-1.0, 1.0) * f->a;
  ay = carmen_uniform_random(-1.0, 1.0) * f->a;

  //printf("ax = %.4f, ay = %.4f\n", ax, ay);

  f->x += ax;
  f->y += ay;
  f->px = ax*ax*f->px + f->qx;
  f->pxy = ax*ax*f->pxy + f->qxy;
  f->py = ay*ay*f->py + f->qy;
}

static void trash_filter_sensor_update(carmen_dot_trash_filter_p f,
				       double x, double y) {

  double pxx, pxy, pyx, pyy;
  double mxx, mxy, myx, myy;
  double kxx, kxy, kyx, kyy;

  pxx = f->px;
  pxy = f->pxy;
  pyx = f->pxy;
  pyy = f->py;
  
  // kalman filter sensor update
  mxx = pxx + f->rx;
  mxy = pxy + f->rxy;
  myx = pyx + f->rxy;
  myy = pyy + f->ry;
  if (invert2d(&mxx, &mxy, &myx, &myy) < 0) {
    carmen_warn("Error: can't invert matrix M");
    return;
  }
  kxx = pxx*mxx+pxy*myx;
  kxy = pxx*mxy+pxy*myy;
  kyx = pyx*mxx+pyy*myx;
  kyy = pyx*mxy+pyy*myy;
  f->x = f->x + kxx*(x - f->x) + kxy*(y - f->y);
  f->y = f->y + kyx*(x - f->x) + kyy*(y - f->y);
  f->px = (1.0 - kxx)*pxx + (-kxy)*pyx;
  f->pxy = (1.0 - kxx)*pxy + (-kxy)*pyy;
  f->py = (-kyx)*pxy + (1.0 - kyy)*pyy;  
}

static void door_filter_motion_update(carmen_dot_door_filter_p f) {

  double ax, ay;
  
  // kalman filter time update with brownian motion model
  ax = carmen_uniform_random(-1.0, 1.0) * f->a;
  ay = carmen_uniform_random(-1.0, 1.0) * f->a;

  //printf("ax = %.4f, ay = %.4f\n", ax, ay);

  f->x += ax;
  f->y += ay;
  f->px = ax*ax*f->px + f->qx;
  f->pxy = ax*ax*f->pxy + f->qxy;
  f->py = ay*ay*f->py + f->qy;
}

static void door_filter_sensor_update(carmen_dot_door_filter_p f, double x, double y) {

  double rx, ry, rxy, rt;
  double pxx, pxy, pyx, pyy, pt;
  double mxx, mxy, myx, myy, mt;
  double kxx, kxy, kyx, kyy, kt;

  pxx = f->px;
  pxy = f->pxy;
  pyx = f->pxy;
  pyy = f->py;
  pt = f->pt; //dbug: + f->qt ?

  rx = default_door_filter_rx*cos(f->t)*cos(f->t) +
    default_door_filter_ry*sin(f->t)*sin(f->t);
  ry = default_door_filter_rx*sin(f->t)*sin(f->t) +
    default_door_filter_ry*cos(f->t)*cos(f->t);
  rxy = default_door_filter_rx*cos(f->t)*sin(f->t) -
    default_door_filter_ry*cos(f->t)*sin(f->t);
  rt = default_door_filter_rt;

  // kalman filter sensor update
  mxx = pxx + rx;
  mxy = pxy + rxy;
  myx = pyx + rxy;
  myy = pyy + ry;
  mt = pt + rt;
  if (invert2d(&mxx, &mxy, &myx, &myy) < 0) {
    carmen_die("Error: can't invert matrix M");
    return;
  }
  mt = 1.0/mt;
  kxx = pxx*mxx+pxy*myx;
  kxy = pxx*mxy+pxy*myy;
  kyx = pyx*mxx+pyy*myx;
  kyy = pyx*mxy+pyy*myy;
  kt = pt*mt;
  f->x = f->x + kxx*(x - f->x) + kxy*(y - f->y);
  f->y = f->y + kyx*(x - f->x) + kyy*(y - f->y);
  f->px = (1.0 - kxx)*pxx + (-kxy)*pyx;
  f->pxy = (1.0 - kxx)*pxy + (-kxy)*pyy;
  f->py = (-kyx)*pxy + (1.0 - kyy)*pyy;
  f->pt = (1.0 - kt)*pt;
}

static void add_new_dot_filter(int *cluster_map, int c, int n,
			       double *x, double *y) {
  int i;

  num_filters++;
  filters = (carmen_dot_filter_p) realloc(filters, num_filters*sizeof(carmen_dot_filter_t));
  carmen_test_alloc(filters);

  for (i = 0; i < n && cluster_map[i] != c; i++);
  if (i == n)
    carmen_die("Error: invalid cluster! (cluster %d not found)\n", c);

  filters[num_filters-1].person_filter.x = x[i];
  filters[num_filters-1].person_filter.y = y[i];
  for (i = 0; i < MAX_PERSON_FILTER_VELOCITY_WINDOW; i++) {
    filters[num_filters-1].person_filter.vx[i] = 0.0;
    filters[num_filters-1].person_filter.vy[i] = 0.0;
  }
  filters[num_filters-1].person_filter.vpos = 0;
  filters[num_filters-1].person_filter.vlen = 0;
  filters[num_filters-1].person_filter.hidden_cnt = 0;
  filters[num_filters-1].person_filter.px = default_person_filter_px;
  filters[num_filters-1].person_filter.py = default_person_filter_py;
  filters[num_filters-1].person_filter.pxy = default_person_filter_pxy;
  filters[num_filters-1].person_filter.a = default_person_filter_a;
  filters[num_filters-1].person_filter.qx = default_person_filter_qx;
  filters[num_filters-1].person_filter.qy = default_person_filter_qy;
  filters[num_filters-1].person_filter.qxy = default_person_filter_qxy;
  filters[num_filters-1].person_filter.rx = default_person_filter_rx;
  filters[num_filters-1].person_filter.ry = default_person_filter_ry;
  filters[num_filters-1].person_filter.rxy = default_person_filter_rxy;

  for (i = 0; i < n; i++)
    if (cluster_map[i] == c)
      person_filter_sensor_update(&filters[num_filters-1].person_filter, x[i], y[i]);

  filters[num_filters-1].trash_filter.x = x[i];
  filters[num_filters-1].trash_filter.y = y[i];
  filters[num_filters-1].trash_filter.px = default_trash_filter_px;
  filters[num_filters-1].trash_filter.py = default_trash_filter_py;
  filters[num_filters-1].trash_filter.a = default_trash_filter_a;
  filters[num_filters-1].trash_filter.qx = default_trash_filter_qx;
  filters[num_filters-1].trash_filter.qy = default_trash_filter_qy;
  filters[num_filters-1].trash_filter.qxy = default_trash_filter_qxy;
  filters[num_filters-1].trash_filter.rx = default_trash_filter_rx;
  filters[num_filters-1].trash_filter.ry = default_trash_filter_ry;
  filters[num_filters-1].trash_filter.rxy = default_trash_filter_rxy;

  for (i = 0; i < n; i++)
    if (cluster_map[i] == c)
      trash_filter_sensor_update(&filters[num_filters-1].trash_filter, x[i], y[i]);

  filters[num_filters-1].door_filter.x = x[i];
  filters[num_filters-1].door_filter.y = y[i];
  filters[num_filters-1].door_filter.t =
    bnorm_theta(filters[num_filters-1].person_filter.px,
		filters[num_filters-1].person_filter.py,
		filters[num_filters-1].person_filter.pxy);
  filters[num_filters-1].door_filter.px = default_door_filter_px;
  filters[num_filters-1].door_filter.py = default_door_filter_py;
  filters[num_filters-1].door_filter.pt = default_door_filter_pt;
  filters[num_filters-1].door_filter.pxy = 0;  //dbug?
  filters[num_filters-1].door_filter.a = default_door_filter_a;
  filters[num_filters-1].door_filter.qx = default_door_filter_qx;
  filters[num_filters-1].door_filter.qy = default_door_filter_qy;
  filters[num_filters-1].door_filter.qxy = default_door_filter_qxy;
  filters[num_filters-1].door_filter.qt = default_door_filter_qt;  

  for (i = 0; i < n; i++)
    if (cluster_map[i] == c)
      door_filter_sensor_update(&filters[num_filters-1].door_filter, x[i], y[i]);

  filters[num_filters-1].type = dot_classify(&filters[num_filters-1]);
  filters[num_filters-1].allow_change = 1;
  filters[num_filters-1].sensor_update_cnt = 0;
}

static int dot_filter(double x, double y) {

  int i, imin;
  double dmin;
  double dx, dy, d;
  double vx, vy, vxy;
  double theta;

  imin = -1;
  dmin = 100000.0;

  for (i = 0; i < num_filters; i++) {
    //if (filters[i].type == CARMEN_DOT_PERSON) {
      dx = filters[i].person_filter.x;
      dy = filters[i].person_filter.y;
      vx = filters[i].person_filter.px;
      vy = filters[i].person_filter.py;
      vxy = filters[i].person_filter.pxy;
      //}
    dx = x - dx;
    dy = y - dy;
    d = dist(dx, dy);

    if (d < dmin) {
      // check if (x,y) is roughly within 3 stdev's of (E[x],E[y])
      // dbug: probably should use eigen transformation instead of rotation
      theta = bnorm_theta(vx, vy, vxy);
      rotate2d(&dx, &dy, -theta);
      if (fabs(dx) < 3.0*sqrt(vx/fabs(cos(theta))) && 
	  fabs(dy) < 3.0*sqrt(vy/fabs(sin(theta)))) {
	dmin = d;
	imin = i;
      }
    }
      //else if (filters[i].type == CARMEN_DOT_TRASH) {
      dx = filters[i].trash_filter.x;
      dy = filters[i].trash_filter.y;
      vx = filters[i].trash_filter.px;
      vy = filters[i].trash_filter.py;
      vxy = filters[i].trash_filter.pxy;
      //}
    dx = x - dx;
    dy = y - dy;
    d = dist(dx, dy);

    if (d < dmin) {
      // check if (x,y) is roughly within 3 stdev's of (E[x],E[y])
      // dbug: probably should use eigen transformation instead of rotation
      theta = bnorm_theta(vx, vy, vxy);
      rotate2d(&dx, &dy, -theta);
      if (fabs(dx) < 3.0*sqrt(vx/fabs(cos(theta))) && 
	  fabs(dy) < 3.0*sqrt(vy/fabs(sin(theta)))) {
	dmin = d;
	imin = i;
      }
    }
      //else { // if (filters[i].type == CARMEN_DOT_DOOR)
      dx = filters[i].door_filter.x;
      dy = filters[i].door_filter.y;
      vx = filters[i].door_filter.px;
      vy = filters[i].door_filter.py;
      vxy = filters[i].door_filter.pxy;
      //}

    dx = x - dx;
    dy = y - dy;
    d = dist(dx, dy);

    if (d < dmin) {
      // check if (x,y) is roughly within 3 stdev's of (E[x],E[y])
      // dbug: probably should use eigen transformation instead of rotation
      theta = bnorm_theta(vx, vy, vxy);
      rotate2d(&dx, &dy, -theta);
      if (fabs(dx) < 3.0*sqrt(vx/fabs(cos(theta))) && 
	  fabs(dy) < 3.0*sqrt(vy/fabs(sin(theta)))) {
	dmin = d;
	imin = i;
      }
    }
  }

  if (imin >= 0) {
    filters[imin].person_filter.hidden_cnt = 0;
    person_filter_sensor_update(&filters[imin].person_filter, x, y);
    if (filters[imin].do_motion_update) {
      filters[imin].do_motion_update = 0;
      trash_filter_motion_update(&filters[imin].trash_filter);
      door_filter_motion_update(&filters[imin].door_filter);
    }
    if (do_sensor_update || filters[imin].sensor_update_cnt < sensor_update_cnt) {
      filters[imin].sensor_update_cnt++;
      trash_filter_sensor_update(&filters[imin].trash_filter, x, y);
      door_filter_sensor_update(&filters[imin].door_filter, x, y);
    }
    filters[imin].type = dot_classify(&filters[imin]);

    return 1;
  }

  return 0;
}

static inline int is_in_map(int x, int y) {

  return (x >= 0 && y >= 0 && x < static_map.config.x_size &&
	  y < static_map.config.y_size);
}

// returns 1 if point is in map
static int map_filter(double x, double y) {

  carmen_world_point_t wp;
  carmen_map_point_t mp;
  double d;
  int md, i, j;

  wp.map = &static_map;
  wp.pose.x = x;
  wp.pose.y = y;

  carmen_world_to_map(&wp, &mp);

  d = map_diff_threshold/static_map.config.resolution;
  md = carmen_round(d);

  for (i = mp.x-md; i <= mp.x+md; i++)
    for (j = mp.y-md; j <= mp.y+md; j++)
      if (is_in_map(i, j) && dist(i-mp.x, j-mp.y) <= d &&
	  static_map.map[i][j] >= map_occupied_threshold)
	return 1;

  return 0;
}

static int cluster(int *cluster_map, int cluster_cnt, int current_reading,
		   double *x, double *y) {
  
  int i, r;
  int nmin;
  double dmin;
  double d;

  if (cluster_cnt == 0) {
    cluster_map[current_reading] = 1;
    //printf("putting reading %d at (%.2f %.2f) in cluster 1\n", current_reading, *x, *y);
    return 1;
  }

  r = current_reading;
  dmin = new_cluster_threshold;
  nmin = -1;

  // nearest neighbor
  for (i = 0; i < r; i++) {
    if (cluster_map[i]) {
      d = dist(x[i]-x[r], y[i]-y[r]);
      if (d < dmin) {
	dmin = d;
	nmin = i;
      }
    }
  }

  if (nmin < 0) {
    cluster_cnt++;
    cluster_map[r] = cluster_cnt;
  }
  else
    cluster_map[r] = cluster_map[nmin];

  //  printf("putting reading %d at (%.2f %.2f) in cluster %d\n", current_reading, *x, *y,
  //	 cluster_map[r]);

  return cluster_cnt;
}

static void delete_filter(int i) {

  if (i < num_filters-1)
    memmove(&filters[i], &filters[i+1], (num_filters-i-1)*sizeof(carmen_dot_filter_t));
  num_filters--;
}

static void kill_people() {

  int i;

  for (i = 0; i < num_filters; i++)
    if (++filters[i].person_filter.hidden_cnt >= kill_hidden_person_cnt &&
	filters[i].type == CARMEN_DOT_PERSON)
      delete_filter(i);
}

static void filter_motion() {

  int i;

  for (i = 0; i < num_filters; i++) {
    if (person_filter_velocity(&filters[i].person_filter) >=
	person_filter_velocity_threshold) {
      filters[i].type = CARMEN_DOT_PERSON;
      filters[i].allow_change = 0;
    }
    person_filter_motion_update(&filters[i].person_filter);
  }
}

static void laser_handler(carmen_robot_laser_message *laser) {

  int i, c, n;
  static int cluster_map[500];
  static int cluster_cnt;
  static double x[500], y[500];

  if (static_map.map == NULL || odom.timestamp == 0.0)
    return;

  carmen_localize_correct_laser(laser, &odom);

  kill_people();
  filter_motion();

  if (dist(last_sensor_update_odom.x - odom.globalpos.x,
	   last_sensor_update_odom.y - odom.globalpos.y) >= sensor_update_dist) {
    do_sensor_update = 1;
    for (i = 0; i < num_filters; i++)
      filters[i].do_motion_update = 1;
  }
  else
    do_sensor_update = 0;
  
  if (do_sensor_update) {
    last_sensor_update_odom.x = odom.globalpos.x;
    last_sensor_update_odom.y = odom.globalpos.y;
  }

  cluster_cnt = 0;
  for (i = 0; i < laser->num_readings; i++) {
    if (laser->range[i] < laser_max_range) {
      x[i] = laser->x + cos(laser->theta + (i-90)*M_PI/180.0) * laser->range[i];
      y[i] = laser->y + sin(laser->theta + (i-90)*M_PI/180.0) * laser->range[i];
      if (!map_filter(x[i], y[i]) && !dot_filter(x[i], y[i]))
	cluster_cnt = cluster(cluster_map, cluster_cnt, i, x, y);
      else
	cluster_map[i] = 0;
    }
  }

  //printf("cluster_cnt = %d\n", cluster_cnt);

  for (c = 1; c <= cluster_cnt; c++) {
    n = 0;
    for (i = 0; i < laser->num_readings; i++)
      if (cluster_map[i] == c)
	n++;
    //printf("cluster %d has %d readings\n", c, n);
    if (n >= new_filter_threshold)
      add_new_dot_filter(cluster_map, c, laser->num_readings, x, y);
  }

  for (i = 0; i < num_filters; i++) {
    printf("vel = %.4f, ", person_filter_velocity(&filters[i].person_filter));
    if (filters[i].type == CARMEN_DOT_PERSON) {
      printf("PERSON (x=%.2f, y=%.2f) (vx=%.4f, vy=%.4f, vxy=%.4f)\n",
	     filters[i].person_filter.x, filters[i].person_filter.y,
	     filters[i].person_filter.px, filters[i].person_filter.py,
	     filters[i].person_filter.pxy);
    }
    else if (filters[i].type == CARMEN_DOT_TRASH)
      printf("TRASH (x=%.2f, y=%.2f) (vx=%.4f, vy=%.4f, vxy=%.4f)\n",
	     filters[i].trash_filter.x, filters[i].trash_filter.y,
	     filters[i].trash_filter.px, filters[i].trash_filter.py,
	     filters[i].trash_filter.pxy);
    else
      printf("DOOR (x=%.2f, y=%.2f, t=%.2f) (vx=%.4f, vy=%.4f, vxy=%.4f, vt=%.4f)\n",
	     filters[i].door_filter.x, filters[i].door_filter.y,
	     filters[i].door_filter.t, filters[i].door_filter.px,
	     filters[i].door_filter.py, filters[i].door_filter.pxy,
	     filters[i].door_filter.pt);
  }
  printf("\n");
}

static void ipc_init() {

  /*
  IPC_RETURN_TYPE err;

  err = IPC_defineMsg(CARMEN_DOT_POS_MSG_NAME, IPC_VARIABLE_LENGTH, 
		      CARMEN_DOT_POS_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_POS_MSG_NAME);
  */

  odom.timestamp = 0.0;
  static_map.map = NULL;

  carmen_robot_subscribe_frontlaser_message(NULL,
					    (carmen_handler_t)laser_handler,
					    CARMEN_SUBSCRIBE_LATEST);
  carmen_localize_subscribe_globalpos_message(&odom, NULL,
					      CARMEN_SUBSCRIBE_LATEST);

  carmen_map_get_gridmap(&static_map);
}

static void params_init(int argc, char *argv[]) {

  carmen_param_t param_list[] = {
    {"robot", "front_laser_max", CARMEN_PARAM_DOUBLE, &laser_max_range, 1, NULL},
    {"dot", "person_filter_a", CARMEN_PARAM_DOUBLE, &default_person_filter_a, 1, NULL},
    {"dot", "person_filter_px", CARMEN_PARAM_DOUBLE, &default_person_filter_px, 1, NULL},
    {"dot", "person_filter_py", CARMEN_PARAM_DOUBLE, &default_person_filter_py, 1, NULL},
    {"dot", "person_filter_pxy", CARMEN_PARAM_DOUBLE, &default_person_filter_pxy, 1, NULL},
    {"dot", "person_filter_qx", CARMEN_PARAM_DOUBLE, &default_person_filter_qx, 1, NULL},
    {"dot", "person_filter_qy", CARMEN_PARAM_DOUBLE, &default_person_filter_qy, 1, NULL},
    {"dot", "person_filter_qxy", CARMEN_PARAM_DOUBLE, &default_person_filter_qxy, 1, NULL},
    {"dot", "person_filter_rx", CARMEN_PARAM_DOUBLE, &default_person_filter_rx, 1, NULL},
    {"dot", "person_filter_ry", CARMEN_PARAM_DOUBLE, &default_person_filter_ry, 1, NULL},
    {"dot", "person_filter_rxy", CARMEN_PARAM_DOUBLE, &default_person_filter_rxy, 1, NULL},
    {"dot", "trash_filter_px", CARMEN_PARAM_DOUBLE, &default_trash_filter_px, 1, NULL},
    {"dot", "trash_filter_py", CARMEN_PARAM_DOUBLE, &default_trash_filter_py, 1, NULL},
    {"dot", "trash_filter_a", CARMEN_PARAM_DOUBLE, &default_trash_filter_a, 1, NULL},
    {"dot", "trash_filter_qx", CARMEN_PARAM_DOUBLE, &default_trash_filter_qx, 1, NULL},
    {"dot", "trash_filter_qy", CARMEN_PARAM_DOUBLE, &default_trash_filter_qy, 1, NULL},
    {"dot", "trash_filter_qxy", CARMEN_PARAM_DOUBLE, &default_trash_filter_qxy, 1, NULL},
    {"dot", "trash_filter_rx", CARMEN_PARAM_DOUBLE, &default_trash_filter_rx, 1, NULL},
    {"dot", "trash_filter_ry", CARMEN_PARAM_DOUBLE, &default_trash_filter_ry, 1, NULL},
    {"dot", "trash_filter_rxy", CARMEN_PARAM_DOUBLE, &default_trash_filter_rxy, 1, NULL},
    {"dot", "door_filter_px", CARMEN_PARAM_DOUBLE, &default_door_filter_px, 1, NULL},
    {"dot", "door_filter_py", CARMEN_PARAM_DOUBLE, &default_door_filter_py, 1, NULL},
    {"dot", "door_filter_pt", CARMEN_PARAM_DOUBLE, &default_door_filter_pt, 1, NULL},
    {"dot", "door_filter_a", CARMEN_PARAM_DOUBLE, &default_door_filter_a, 1, NULL},
    {"dot", "door_filter_qx", CARMEN_PARAM_DOUBLE, &default_door_filter_qx, 1, NULL},
    {"dot", "door_filter_qy", CARMEN_PARAM_DOUBLE, &default_door_filter_qy, 1, NULL},
    {"dot", "door_filter_qxy", CARMEN_PARAM_DOUBLE, &default_door_filter_qxy, 1, NULL},
    {"dot", "door_filter_qt", CARMEN_PARAM_DOUBLE, &default_door_filter_qt, 1, NULL},
    {"dot", "door_filter_rx", CARMEN_PARAM_DOUBLE, &default_door_filter_rx, 1, NULL},
    {"dot", "door_filter_ry", CARMEN_PARAM_DOUBLE, &default_door_filter_ry, 1, NULL},
    {"dot", "door_filter_rt", CARMEN_PARAM_DOUBLE, &default_door_filter_rt, 1, NULL},
    {"dot", "new_filter_threshold", CARMEN_PARAM_INT, &new_filter_threshold, 1, NULL},
    {"dot", "new_cluster_threshold", CARMEN_PARAM_DOUBLE, &new_cluster_threshold, 1, NULL},
    {"dot", "map_diff_threshold", CARMEN_PARAM_DOUBLE, &map_diff_threshold, 1, NULL},
    {"dot", "map_occupied_threshold", CARMEN_PARAM_DOUBLE, &map_occupied_threshold, 1, NULL},
    {"dot", "person_filter_velocity_window", CARMEN_PARAM_INT, &person_filter_velocity_window,
     1, (carmen_param_change_handler_t)person_filter_velocity_window_handler},
    {"dot", "person_filter_velocity_threshold", CARMEN_PARAM_DOUBLE,
     &person_filter_velocity_threshold, 1, NULL},
    {"dot", "kill_hidden_person_cnt", CARMEN_PARAM_INT, &kill_hidden_person_cnt, 1, NULL},
    {"dot", "sensor_update_dist", CARMEN_PARAM_DOUBLE, &sensor_update_dist, 1, NULL},
    {"dot", "sensor_update_cnt", CARMEN_PARAM_INT, &sensor_update_cnt, 1, NULL}
  };

  carmen_param_install_params(argc, argv, param_list,
	 		      sizeof(param_list) / sizeof(param_list[0]));
}

void shutdown_module(int sig) {

  sig = 0;

  exit(0);
}

int main(int argc __attribute__ ((unused)), char *argv[]) {

  //int c;

  carmen_initialize_ipc(argv[0]);
  carmen_param_check_version(argv[0]);
  carmen_randomize();

  signal(SIGINT, shutdown_module);

  params_init(argc, argv);
  ipc_init();

  carmen_terminal_cbreak(0);

  while (1) {
    sleep_ipc(0.01);
    /****************
    c = getchar();
    if (c != EOF) {
      switch (c) {
      case ' ':
	get_background_range = 1;
	printf("\nCollecting background range...");
	fflush(0);
	break;
      case 'p':
	display_position = !display_position;
      }
    }
    ****************/
  }

  return 0;
}