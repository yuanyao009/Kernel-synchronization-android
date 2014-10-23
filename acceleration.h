//
//  acceleration.h
//  HW3
//
//  Created by Yuan Yao on 10/10/14.
//  Copyright (c) 2014 Yuan Yao. All rights reserved.
//
#ifndef _ACCELERATION_H
#define _ACCELERATION_H

#include <linux/types.h>
struct dev_acceleration{
	int x;
	int y;
	int z;
};

struct acc_motion {
	
	unsigned int dlt_x; /* +/- around X-axis */
	unsigned int dlt_y; /* +/- around Y-axis */
	unsigned int dlt_z; /* +/- around Z-axis */
	
	unsigned int frq;   /* Number of samples that satisfies:
                         sum_each_sample(dlt_x + dlt_y + dlt_z) > NOISE */
};

struct collection{
	struct wait_queue_head_t heading;
	struct list_head Node;
	int event_id;
	struct acc_motion *motionType;
};


extern int event_num;
extern struct dev_acceleration *our_buffer;
extern int acceleration_head;
extern int acceleration_end;
extern int buffer_Count_acceleration;
#endif