/*
 * Configuration for AMI305 (and AK8974) magnetometer driver.
 */

#ifndef __LINUX_I2C_AMI305_H
#define __LINUX_I2C_AMI305_H

#define AMI305_NO_MAP		  0
#define AMI305_DEV_X		  1
#define AMI305_DEV_Y		  2
#define AMI305_DEV_Z		  3
#define AMI305_INV_DEV_X	 -1
#define AMI305_INV_DEV_Y	 -2
#define AMI305_INV_DEV_Z	 -3

struct ami305_platform_data {
	s8 axis_x;
	s8 axis_y;
	s8 axis_z;
};

#endif /* __LINUX_I2C_AMI305_H */
