/*
The MIT License (MIT)

Copyright (c) 2015 silverx

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <inttypes.h>
#include <math.h>

#include "pid.h"
#include "config.h"
#include "util.h"
#include "drv_pwm.h"
#include "control.h"
#include "defines.h"
#include "drv_time.h"

#include "sixaxis.h"

#include "gestures.h"


extern int ledcommand;		

extern float rx[4];
extern float gyro[3];
extern int failsafe;
extern float pidoutput[3];

extern float lpffilter( float in,int num );

extern char auxchange[AUXNUMBER];
extern char aux[AUXNUMBER];

extern float looptime;
extern float attitude[3];

int onground = 1;

float thrsum;

float rxcopy[4];

float error[PIDNUMBER];
float motormap( float input);
int lastchange;

float yawangle;
float angleerror[3];

float debugthr;

float overthrottlefilt;

/*
// bump test vars
int lastchange;
int pulse;
unsigned long timestart;
*/

extern float apid(int x );
extern void imu_calc( void);
extern void savecal( void);

void motorcontrol(void);
int gestures(void);
void pid_precalc( void);
float motorfilter( float motorin ,int number);


void control( void)
{

	// hi rates
	float ratemulti;
	float ratemultiyaw;
	float maxangle;
	float anglerate;
	
	if ( aux[RATES] ) 
	{
		ratemulti = HIRATEMULTI;
		ratemultiyaw = HIRATEMULTIYAW;
		maxangle = MAX_ANGLE_HI;
		anglerate = LEVEL_MAX_RATE_HI;
	}
	else 
	{
		ratemulti = 1.0f;
		ratemultiyaw = 1.0f;
		maxangle = MAX_ANGLE_LO;
		anglerate = LEVEL_MAX_RATE_LO;
	}

	
	for ( int i = 0 ; i < 3; i++)
	{
		rxcopy[i]=   rx[i];
	}
	
	
	if ( auxchange[HEADLESSMODE] )
	{
		yawangle = 0;
	}
	
	if ( (aux[HEADLESSMODE] )&&!aux[LEVELMODE] ) 
	{
				
		yawangle = yawangle + gyro[2]*looptime;
		
		float temp = rxcopy[0];
		rxcopy[0] = rxcopy[0] * cosf( yawangle) - rxcopy[1] * sinf(yawangle );
		rxcopy[1] = rxcopy[1] * cosf( yawangle) + temp * sinf(yawangle ) ;
	}
	else
	{
		yawangle = 0;
	}
	
// check for acc calibration
		
	int command = gestures2();

	if ( command )
	{	
			if ( command == 3 )
			{
				gyro_cal(); // for flashing lights
				acc_cal();
				savecal();
				// reset loop time 
				extern unsigned lastlooptime;
				lastlooptime = gettime();
			}
			else
			{
				ledcommand = 1;
				if ( command == 2 )
				{
					aux[CH_AUX1]= 1;
					
				}
				if ( command == 1 )
				{
					aux[CH_AUX1]= 0;
				}
			}
	}

imu_calc();

pid_precalc();

	if ( aux[LEVELMODE] ) 
	{// level mode

	angleerror[0] = rxcopy[0] * maxangle - attitude[0];
	angleerror[1] = rxcopy[1] * maxangle - attitude[1];

	error[0] = apid(0) * anglerate * DEGTORAD  - gyro[0];
	error[1] = apid(1) * anglerate * DEGTORAD  - gyro[1];	 

		
	}
else
{ // rate mode
	
	error[0] = rxcopy[0] * MAX_RATE * DEGTORAD * ratemulti - gyro[0];
	error[1] = rxcopy[1] * MAX_RATE * DEGTORAD * ratemulti - gyro[1];
	
	// reduce angle Iterm towards zero
	extern float aierror[3];
	for ( int i = 0 ; i <= 2 ; i++) aierror[i] *= 0.8f;

	
}	


 error[2] = rxcopy[2] * MAX_RATEYAW * DEGTORAD * ratemultiyaw - gyro[2];

	pid(0);
	pid(1);
	pid(2);

// map throttle so under 10% it is zero	
float	throttle = mapf(rx[3], 0 , 1 , -0.1 , 1 );
if ( throttle < 0   ) throttle = 0;
if ( throttle > 1.0f ) throttle = 1.0f;


// turn motors off if throttle is off and pitch / roll sticks are centered
	if ( failsafe || (throttle < 0.001f && (!ENABLESTIX||  (fabs(rx[0]) < 0.5f && fabs(rx[1]) < 0.5f ) ) ) ) 

	{ // motors off
		
		onground = 1;
		thrsum = 0;
		for ( int i = 0 ; i <= 3 ; i++)
		{
			pwm_set( i , 0 );
		}				
			
		// reset the overthrottle filter
		lpf( &overthrottlefilt, 0.0f , 0.72f); // 50hz 1khz sample rate
		
		#ifdef MOTOR_FILTER		
		// reset the motor filter
		for ( int i = 0 ; i <= 3 ; i++)
					{		
					motorfilter( 0 , i);
					}	
		#endif
		
	}
	else
	{
		// throttle angle compensation
#ifdef AUTO_THROTTLE
		if ( aux[LEVELMODE]||AUTO_THROTTLE_ACRO_MODE ) 
		{
	
			float autothrottle = fastcos( attitude[0] * DEGTORAD ) * fastcos( attitude[1] * DEGTORAD );
			float old_throttle = throttle;
			if ( autothrottle <= 0.5f ) autothrottle = 0.5f;
			throttle = throttle / autothrottle;
			// limit to 90%
			if ( old_throttle < 0.9f ) if ( throttle > 0.9f ) throttle = 0.9f;

			if ( throttle > 1.0f ) throttle = 1.0f;
			debugthr = autothrottle;

		}
#endif		
		onground = 0;
		float mix[4];	


		mix[MOTOR_FR] = throttle - pidoutput[0] - pidoutput[1] + pidoutput[2];		// FR
		mix[MOTOR_FL] = throttle + pidoutput[0] - pidoutput[1] - pidoutput[2];		// FL	
		mix[MOTOR_BR] = throttle - pidoutput[0] + pidoutput[1] - pidoutput[2];		// BR
		mix[MOTOR_BL] = throttle + pidoutput[0] + pidoutput[1] + pidoutput[2];		// BL	

		
#ifdef MIX_LOWER_THROTTLE 	

// limit reduction to this amount ( 0.0 - 1.0)
// 0.0 = no action 
// 0.5 = reduce up to 1/2 throttle	
//1.0 = reduce all the way to zero 
#define MIX_THROTTLE_REDUCTION_MAX 0.5
		
float overthrottle = 0;

for ( int i = 0 ; i < 3 ; i++)
{
	if ( mix[i] > overthrottle ) overthrottle =  mix[i];
}

overthrottle -=1.0f;

if ( overthrottle > (float) MIX_THROTTLE_REDUCTION_MAX ) overthrottle = (float) MIX_THROTTLE_REDUCTION_MAX;

#ifdef MIX_THROTTLE_FILTER_LPF
if (overthrottle > overthrottlefilt)  lpf ( &overthrottlefilt, overthrottle , 0.82); // 20hz 1khz sample rate
else lpf ( &overthrottlefilt, overthrottle , 0.72); // 50hz 1khz sample rate
#else
if (overthrottle > overthrottlefilt) overthrottlefilt += 0.005f;
else overthrottlefilt -= 0.01f;
#endif

if (overthrottlefilt > 0.5f) overthrottlefilt = 0.5;
if (overthrottlefilt < -0.1f) overthrottlefilt = -0.1;


overthrottle = overthrottlefilt;

if ( overthrottle < 0.0f ) overthrottle = 0.0f;

if ( overthrottle > 0 )
{// exceeding max motor thrust
	
	// prevent too much throttle reduction
	if ( overthrottle > (float) MIX_THROTTLE_REDUCTION_MAX ) overthrottle = (float) MIX_THROTTLE_REDUCTION_MAX;
	// reduce by a percentage only, so we get an inbetween performance
	overthrottle *= ((float) MIX_THROTTLE_REDUCTION_PERCENT / 100.0f);		
	
	for ( int i = 0 ; i < 3 ; i++)
	{
		mix[i] -=overthrottle;
	}
}
#endif
		
		
#ifdef MOTOR_FILTER		

for ( int i = 0 ; i <= 3 ; i++)
			{
			//if ( mix[i] < 0 ) mix[i] = 0;
			//if ( mix[i] > 1 ) mix[i] = 1;
			mix[i] = motorfilter(  mix[i] , i);
			}	
#endif

		for ( int i = 0 ; i <= 3 ; i++)
		{
		float test = motormap( mix[i] );
		#ifndef NOMOTORS
		pwm_set( i , ( test )  );
		#else
		#warning "NO MOTORS"
		#endif
		}	

		thrsum = 0;
		for ( int i = 0 ; i <= 3 ; i++)
		{
			if ( mix[i] < 0 ) mix[i] = 0;
			if ( mix[i] > 1 ) mix[i] = 1;
			thrsum+= mix[i];
		}	
		thrsum = thrsum / 4;
		
	}// end motors on
	
}
	
// the old map for 490Hz
float motormapx( float input)
{ 
	// this is a thrust to pwm function
	//  float 0 to 1 input and output
	// output can go negative slightly
	// measured eachine motors and prop, stock battery
	// a*x^2 + b*x + c
	// a = 0.262 , b = 0.771 , c = -0.0258

if (input > 1) input = 1;
if (input < 0) input = 0;

input = input*input*0.262f  + input*(0.771f);
input += -0.0258f;

return input;   
}

// 8k pwm is where the motor thrust is relatively linear for the H8 6mm motors
// it's due to the motor inductance cancelling the nonlinearities.
float motormap( float input)
{
	return input;
}



float hann_lastsample[4];
float hann_lastsample2[4];

// hanning 3 sample filter
float motorfilter( float motorin ,int number)
{
 	float ans = motorin*0.25f + hann_lastsample[number] * 0.5f +   hann_lastsample2[number] * 0.25f ;
	
	hann_lastsample2[number] = hann_lastsample[number];
	hann_lastsample[number] = motorin;
	
	return ans;
}
