// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logging_filter_interpreter.h"

#include <errno.h>
#include <fcntl.h>
#include <string>

#include <base/file_util.h>
#include <base/file_path.h>
#include <base/values.h>

#include "logging.h"

namespace gestures {

LoggingFilterInterpreter::LoggingFilterInterpreter(PropRegistry* prop_reg,
                                                   Interpreter* next,
                                                   Tracer* tracer)
    : FilterInterpreter(prop_reg, next, tracer, true),
      logging_notify_(prop_reg, "Logging Notify", 0, this),
      logging_reset_(prop_reg, "Logging Reset", 0, this),
      integrated_touchpad_(prop_reg, "Integrated Touchpad", 0) {
  InitName();
  if (prop_reg && log_.get())
    prop_reg->set_activity_log(log_.get());
}

void LoggingFilterInterpreter::IntWasWritten(IntProperty* prop) {
  if (prop == &logging_notify_)
    Dump("/var/log/xorg/touchpad_activity_log.txt");
  if (prop == &logging_reset_)
    Clear();
};

std::string LoggingFilterInterpreter::EncodeActivityLog() {
  return Encode();
}

void LoggingFilterInterpreter::Dump(const char* filename) {
  std::string data = Encode();
  std::string fn(filename);
  FilePath fp(fn);
  file_util::WriteFile(fp, data.c_str(), data.size());
}
}  // namespace gestures