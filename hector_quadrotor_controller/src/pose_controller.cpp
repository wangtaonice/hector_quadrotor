//=================================================================================================
// Copyright (c) 2013, Johannes Meyer, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Flight Systems and Automatic Control group,
//       TU Darmstadt, nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================

#include <hector_quadrotor_controller/quadrotor_interface.h>
#include <hector_quadrotor_controller/pid.h>

#include <controller_interface/controller.h>

#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>

#include <ros/subscriber.h>
#include <ros/callback_queue.h>

namespace hector_quadrotor_controller {

using namespace controller_interface;

class PoseController : public controller_interface::Controller<QuadrotorInterface>
{
public:
  bool init(QuadrotorInterface *interface, ros::NodeHandle &root_nh, ros::NodeHandle &controller_nh)
  {
    // get interface handles
    pose_input_   = interface->addInput<PoseCommandHandle>("pose");
    twist_input_  = interface->addInput<TwistCommandHandle>("pose/twist");
    twist_output_ = interface->addOutput<TwistCommandHandle>("twist");
    interface->claim(twist_output_->getName());

    // subscribe to commanded pose and velocity
    ros::SubscribeOptions pose_subscribe_options = ros::SubscribeOptions::create<geometry_msgs::PoseStamped>(
      "command/pose", 1,
      boost::bind(&PoseController::poseCommandCallback, this, _1),
      ros::VoidConstPtr(), 0 // &callback_queue_
    );
    pose_subscriber_ = root_nh.subscribe(pose_subscribe_options);
    ros::SubscribeOptions twist_subscribe_options = ros::SubscribeOptions::create<geometry_msgs::TwistStamped>(
      "command/twist", 1,
      boost::bind(&PoseController::twistCommandCallback, this, _1),
      ros::VoidConstPtr(), 0 // &callback_queue_
    );
    twist_subscriber_ = root_nh.subscribe(twist_subscribe_options);

    // initialize PID controllers
    pid_.x.init(ros::NodeHandle(controller_nh, "xy"));
    pid_.y.init(ros::NodeHandle(controller_nh, "xy"));
    pid_.z.init(ros::NodeHandle(controller_nh, "z"));
    pid_.yaw.init(ros::NodeHandle(controller_nh, "yaw"));

    return true;
  }

  void reset()
  {
    pid_.x.reset();
    pid_.y.reset();
    pid_.x.reset();
    pid_.yaw.reset();
  }

  void poseCommandCallback(const geometry_msgs::PoseStampedConstPtr& command)
  {
    pose_command_ = *command;
    if (!(pose_input_->connected())) *pose_input_ = &(pose_command_.pose);

    ros::Time start_time = command->header.stamp;
    if (start_time.isZero()) start_time = ros::Time::now();
    if (!isRunning()) this->startRequest(start_time);
  }

  void twistCommandCallback(const geometry_msgs::TwistStampedConstPtr& command)
  {
    twist_command_ = *command;
    if (!(twist_input_->connected())) *twist_input_ = &(twist_command_.twist);

    ros::Time start_time = command->header.stamp;
    if (start_time.isZero()) start_time = ros::Time::now();
    if (!isRunning()) this->startRequest(start_time);
  }

  void starting(const ros::Time &time)
  {
    reset();
    twist_output_->start();
  }

  void stopping(const ros::Time &time)
  {
    twist_output_->stop();
  }

  void update(const ros::Time& time, const ros::Duration& period)
  {
    Twist output;

    // execute available callbacks in the callback queue (is this real-time safe?)
  //  callback_queue_.callAvailable();

    // return if no pose command is available
    if (!(pose_input_->connected())) {
      return;
    }

    // check command timeout
    // TODO

    // control horizontal position
    double error_n, error_w;
    HorizontalPositionCommandHandle(*pose_input_).getError(error_n, error_w);
    output.linear.x = pid_.x.update(error_n, twist_input_->twist().linear.x, period);
    output.linear.y = pid_.y.update(error_w, twist_input_->twist().linear.y, period);

    // control height
    output.linear.z = pid_.z.update(HeightCommandHandle(*pose_input_).getError(), twist_input_->twist().linear.z, period);

    // control yaw angle
    output.angular.z = pid_.yaw.update(HeadingCommandHandle(*pose_input_).getError(), twist_input_->twist().angular.z, period);

    // add twist command if available
    if (twist_input_->connected())
    {
      output.linear.x  += twist_input_->getCommand().linear.x;
      output.linear.y  += twist_input_->getCommand().linear.y;
      output.linear.z  += twist_input_->getCommand().linear.z;
      output.angular.x += twist_input_->getCommand().angular.x;
      output.angular.y += twist_input_->getCommand().angular.y;
      output.angular.z += twist_input_->getCommand().angular.z;
    }

    // set twist output
    twist_output_->setCommand(output);
  }

private:
  PoseCommandHandlePtr pose_input_;
  TwistCommandHandlePtr twist_input_;
  TwistCommandHandlePtr twist_output_;

  geometry_msgs::PoseStamped pose_command_;
  geometry_msgs::TwistStamped twist_command_;

  // ros::CallbackQueue callback_queue_;
  ros::Subscriber pose_subscriber_;
  ros::Subscriber twist_subscriber_;

  struct {
    PID x;
    PID y;
    PID z;
    PID yaw;
  } pid_;
};

} // namespace hector_quadrotor_controller

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(hector_quadrotor_controller::PoseController, controller_interface::ControllerBase)
