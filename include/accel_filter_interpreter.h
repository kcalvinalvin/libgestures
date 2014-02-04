// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

#include "base/memory/scoped_ptr.h"
#include <gtest/gtest.h>  // for FRIEND_TEST

#include "filter_interpreter.h"
#include "gestures.h"
#include "prop_registry.h"
#include "tracer.h"

#ifndef GESTURES_ACCEL_FILTER_INTERPRETER_H_
#define GESTURES_ACCEL_FILTER_INTERPRETER_H_

namespace gestures {

// This interpreter provides pointer and scroll acceleration based on
// an acceleration curve and the user's sensitivity setting.

class AccelFilterInterpreter : public FilterInterpreter {
  FRIEND_TEST(AccelFilterInterpreterTest, CustomAccelTest);
  FRIEND_TEST(AccelFilterInterpreterTest, SimpleTest);
  FRIEND_TEST(AccelFilterInterpreterTest, TimingTest);
  FRIEND_TEST(AccelFilterInterpreterTest, TinyMoveTest);
 public:
  // Takes ownership of |next|:
  AccelFilterInterpreter(PropRegistry* prop_reg, Interpreter* next,
                         Tracer* tracer);
  virtual ~AccelFilterInterpreter() {}

  virtual void ConsumeGesture(const Gesture& gs);

 private:
  struct CurveSegment {
    CurveSegment() : x_(INFINITY), sqr_(0.0), mul_(1.0), int_(0.0) {}
    CurveSegment(float x, float s, float m, float b)
        : x_(x), sqr_(s), mul_(m), int_(b) {}
    CurveSegment(const CurveSegment& that)
        : x_(that.x_), sqr_(that.sqr_), mul_(that.mul_), int_(that.int_) {}
    // Be careful adding new members: We currently cast arrays of CurveSegment
    // to arrays of float (to expose to the properties system)
    double x_;  // Max X value of segment. User's point will be less than this.
    double sqr_;  // x^2 multiplier
    double mul_;  // Slope of line (x multiplier)
    double int_;  // Intercept of line
  };

  static const size_t kMaxCurveSegs = 3;
  static const size_t kMaxCustomCurveSegs = 20;
  static const size_t kMaxAccelCurves = 5;

  // curves for sensitivity 1..5
  CurveSegment point_curves_[kMaxAccelCurves][kMaxCurveSegs];
  CurveSegment mouse_point_curves_[kMaxAccelCurves][kMaxCurveSegs];
  CurveSegment scroll_curves_[kMaxAccelCurves][kMaxCurveSegs];

  // Custom curves
  CurveSegment custom_point_[kMaxCustomCurveSegs];
  CurveSegment custom_scroll_[kMaxCustomCurveSegs];

  IntProperty pointer_sensitivity_;  // [1..5] or 0 for custom
  IntProperty scroll_sensitivity_;  // [1..5] or 0 for custom

  DoubleArrayProperty custom_point_prop_;
  DoubleArrayProperty custom_scroll_prop_;

  DoubleProperty point_x_out_scale_;
  DoubleProperty point_y_out_scale_;
  DoubleProperty scroll_x_out_scale_;
  DoubleProperty scroll_y_out_scale_;
  BoolProperty use_mouse_point_curves_;

  // Sometimes on wireless hardware (e.g. Bluetooth), patckets need to be
  // resent. This can lead to a time between packets that very large followed
  // by a very small one. Very small periods especially cause problems b/c they
  // make the velocity seem very fast, which leads to an exaggeration of
  // movement.
  // To compensate, we have bounds on what we expect a reasonable period to be.
  // Events that have too large or small a period get reassigned the last
  // reasonable period.
  DoubleProperty min_reasonable_dt_;
  DoubleProperty max_reasonable_dt_;
  stime_t last_reasonable_dt_;

  // If we enable smooth accel, the past few magnitudes are used to compute the
  // multiplication factor.
  BoolProperty smooth_accel_;
  stime_t last_end_time_;
  float last_mags_[2];
  size_t last_mags_size_;
};

}  // namespace gestures

#endif  // GESTURES_SCALING_FILTER_INTERPRETER_H_