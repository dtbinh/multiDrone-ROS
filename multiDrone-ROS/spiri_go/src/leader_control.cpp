/**
 * leader adaptive control
 */

#include "ros/ros.h"
#include <actionlib/client/simple_action_client.h>
#include <actionlib/client/terminal_state.h>
#include <spiri_go/TakeoffAction.h>
#include "spiri_go.h"
#include <std_msgs/String.h>
#include <sensor_msgs/NavSatFix.h>
#include <geometry_msgs/TwistStamped.h>
#include <sstream>
#include <px4ros/gpsPosition.h>
#include <math.h>


//global variables
sensor_msgs::NavSatFix selfGlobalPos;    //self gps subscribed from mavros
px4ros::gpsPosition neigGlobalPos;       //neighbour gps subscribed from leader_control_decode.py

#define R  6371000         //meter
#define PI 3.1415926535898

/*******************************************************************************
 *  Callback function on the gps of the copter
 *
 *  @param gpsPositionConstPtr& the gps callback pointer
 ******************************************************************************/
void gpsSubCb(const sensor_msgs::NavSatFixConstPtr& msg){
   selfGlobalPos = *msg;
}

/*******************************************************************************
 *  Callback function on the gps of the neighbour copter
 *
 *  @param gpsPositionConstPtr& the gps callback pointer
 ******************************************************************************/
void neigGpsSubCb(const px4ros::gpsPositionConstPtr& msg)
{
    neigGlobalPos = *msg;
}

/*******************************************************************************
 *  Main function
 *
 *  @param 
 ******************************************************************************/
int main(int argc, char **argv) {
	ros::init(argc, argv, "spiri_go_client");
	ros::NodeHandle nh;

	ros::Rate loop_rate(10.0);
	
	//velocity to publish
	geometry_msgs::TwistStamped vel;
	vel.twist.linear.x = 0.0;
	vel.twist.linear.y = 0.0;
	vel.header.stamp = ros::Time::now();

	//variables definition
	float x_0 = 0.0;
	float y_0 = 0.0;
	float x_1 = 0.0;
	float y_1 = 0.0;
	float d12 = 10;   //meter,two leaders mantain the distance
	float vx0 = 0.0;
	float vy0 = 0.0;
	float currentDisPow = 0.0;
	
	//takeoff action object
	actionlib::SimpleActionClient<spiri_go::TakeoffAction> ac("spiri_take_off", true);  //first param: server name

    // Subscriber to the Copter gps
    string gps_ = nh.resolveName("/mavros/global_position/raw/fix");
    ros::Subscriber gps = nh.subscribe(gps_, 5, gpsSubCb);

    // Subscriber to the neighbour gps
    string neigGps_ = nh.resolveName("neighbour_position");
    ros::Subscriber neigGps = nh.subscribe(neigGps_, 5, neigGpsSubCb);	

	//Publisher to the gps(toString)
	string gps_string_pub_ = nh.resolveName("serial_data_send");
	ros::Publisher gps_string_pub = nh.advertise<std_msgs::String>(gps_string_pub_, 5);

	//Publisher to the velocity of the copter
	string vel_pub_ = nh.resolveName("/mavros/setpoint_velocity/cmd_vel");
	ros::Publisher vel_pub = nh.advertise<geometry_msgs::TwistStamped>(vel_pub_, 5);
	
	ROS_INFO("Waiting for action server(takeOff) to start.");

	ac.waitForServer();	
	
	ROS_INFO("Action server started--Taking off!");

	spiri_go::TakeoffGoal goal;

	//set the target height to take off!
	goal.height = 3;
	
	ac.sendGoal(goal);

	//wait for action(takeOff) to return
	bool is_takeOff_finished = ac.waitForResult(ros::Duration(60.0));
	ros::Duration(10).sleep();
	if(is_takeOff_finished ) {
		ROS_INFO("reached target height!");

		while(nh.ok()) {
			//1.publish self gps(toString)
			std_msgs::String gps_string;
      		std::stringstream ss;
      		ss << "o"<<selfGlobalPos.longitude<<"a"<<selfGlobalPos.latitude<<"#";
      		gps_string.data = ss.str();
      		gps_string_pub.publish(gps_string);
	
			ROS_INFO("self_lat:%f", selfGlobalPos.latitude);
			ROS_INFO("self_lon:%f", selfGlobalPos.longitude);
 
			//2.read neighbour gps
			ROS_INFO("Neigb_lat:%f", neigGlobalPos.lat);
			ROS_INFO("Neigb_lon:%f", neigGlobalPos.lon);

			//3.calculate velocity(adaptive control)
			if(neigGlobalPos.lat == 0.0 || neigGlobalPos.lon == 0.0) {
				vel.twist.linear.x = 0.0;
				vel.twist.linear.y = 0.0;
			}
			else {
				x_1 = float(PI/180) * R * neigGlobalPos.lat;
				y_1 = float(PI/180) * R * neigGlobalPos.lon;
				x_0 = float(PI/180) * R * selfGlobalPos.latitude;
				y_0 = float(PI/180) * R * selfGlobalPos.longitude;
				
				currentDisPow = pow(x_1-x_0, 2) + pow(y_1-y_0, 2);

				vel.twist.linear.x = vx0 + (x_1 - x_0)*(currentDisPow - pow(d12, 2))*0.01;
				vel.twist.linear.y = vy0 + (y_1 - y_0)*(currentDisPow - pow(d12, 2))*0.01;

				vel.twist.linear.y = -1 * vel.twist.linear.y; 
			}

			
			if(vel.twist.linear.x >= 1) {
				vel.twist.linear.x = 1;
			}else if(vel.twist.linear.x <= -1) {
				vel.twist.linear.x = -1;
			}
			if(vel.twist.linear.y >= 1) {
				vel.twist.linear.y = 1;
			}else if(vel.twist.linear.y <= -1) {
				vel.twist.linear.y = -1;
			}
			

			vel.header.stamp = ros::Time::now();

			//4.publish velocity to mavros 
			vel_pub.publish(vel);
			ROS_INFO("velocity_x:%f", vel.twist.linear.x);
			ROS_INFO("velocity_y:%f", vel.twist.linear.y);
		
			ros::spinOnce();
    		loop_rate.sleep();
		}		
	}
	else {
		ROS_INFO("should set mode Land!");
	}

	return 0;

}
