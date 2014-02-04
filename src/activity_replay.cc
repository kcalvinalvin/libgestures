// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "activity_replay.h"

#include <string>

#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <gtest/gtest.h>

#include "logging.h"
#include "prop_registry.h"
#include "set.h"
#include "unittest_util.h"
#include "util.h"

using std::endl;
using std::set;
using std::string;

namespace gestures {

ActivityReplay::ActivityReplay(PropRegistry* prop_reg)
    : log_(NULL), prop_reg_(prop_reg) {}

bool ActivityReplay::Parse(const string& data) {
  std::set<string> emptyset;
  return Parse(data, emptyset);
}

bool ActivityReplay::Parse(const string& data,
                           const std::set<string>& honor_props) {
  log_.Clear();
  names_.clear();

  int error_code;
  string error_msg;
  Value* root = base::JSONReader::ReadAndReturnError(data, true, &error_code,
                                                     &error_msg);
  if (!root) {
    Err("Parse failed: %s", error_msg.c_str());
    return false;
  }
  if (root->GetType() != Value::TYPE_DICTIONARY) {
    Err("Root type is %d, but expected %d (dictionary)",
        root->GetType(), Value::TYPE_DICTIONARY);
    return false;
  }
  DictionaryValue* dict = static_cast<DictionaryValue*>(root);
  // Get and apply user-configurable properties
  DictionaryValue* props_dict = NULL;
  if (dict->GetDictionary(ActivityLog::kKeyProperties, &props_dict) &&
      !ParseProperties(props_dict, honor_props)) {
    Err("Unable to parse properties.");
    return false;
  }
  // Get and apply hardware properties
  DictionaryValue* hwprops_dict = NULL;
  if (!dict->GetDictionary(ActivityLog::kKeyHardwarePropRoot, &hwprops_dict)) {
    Err("Unable to get hwprops dict.");
    return false;
  }
  if (!ParseHardwareProperties(hwprops_dict, &hwprops_))
    return false;
  log_.SetHardwareProperties(hwprops_);
  ListValue* entries = NULL;
  ListValue* next_layer_entries = NULL;
  char next_layer_path[PATH_MAX];
  snprintf(next_layer_path, sizeof(next_layer_path), "%s.%s",
           ActivityLog::kKeyNext, ActivityLog::kKeyRoot);
  if (!dict->GetList(ActivityLog::kKeyRoot, &entries)) {
    Err("Unable to get list of entries from root.");
    return false;
  }
  if (dict->GetList(next_layer_path, &next_layer_entries) &&
      (entries->GetSize() < next_layer_entries->GetSize()))
    entries = next_layer_entries;

  for (size_t i = 0; i < entries->GetSize(); ++i) {
    DictionaryValue* entry = NULL;
    if (!entries->GetDictionary(i, &entry)) {
      Err("Invalid entry at index %zu", i);
      return false;
    }
    if (!ParseEntry(entry))
      return false;
  }
  return true;
}

bool ActivityReplay::ParseProperties(DictionaryValue* dict,
                                     const std::set<string>& honor_props) {
  if (!prop_reg_)
    return true;
  ::set<Property*> props = prop_reg_->props();
  for (::set<Property*>::const_iterator it = props.begin(), e = props.end();
       it != e; ++it) {
    const char* key = (*it)->name();

    // TODO(clchiou): This is just a emporary workaround for property changes.
    // I will work out a solution for this kind of changes.
    if (!strcmp(key, "Compute Surface Area from Pressure") ||
        !strcmp(key, "Touchpad Device Output Bias on X-Axis") ||
        !strcmp(key, "Touchpad Device Output Bias on Y-Axis")) {
      continue;
    }

    if (!honor_props.empty() && !SetContainsValue(honor_props, string(key)))
      continue;
    Value* value = NULL;
    if (!dict->Get(key, &value)) {
      Err("Log doesn't have value for property %s", key);
      continue;
    }
    if (!(*it)->SetValue(value)) {
      Err("Unable to restore value for property %s", key);
      return false;
    }
  }
  return true;
}

#define PARSE_HP(obj, key, KeyType, KeyFn, var, VarType, required)      \
  do {                                                                  \
    KeyType temp;                                                       \
    if (!obj->KeyFn(key, &temp)) {                                      \
      Err("Parse failed for key %s", key);                              \
      if (required)                                                     \
        return false;                                                   \
    }                                                                   \
    var = static_cast<VarType>(temp);                                   \
  } while (0)

bool ActivityReplay::ParseHardwareProperties(DictionaryValue* obj,
                                             HardwareProperties* out_props) {
  HardwareProperties props;
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropLeft, double, GetDouble,
           props.left, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropTop, double, GetDouble,
           props.top, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropRight, double, GetDouble,
           props.right, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropBottom, double, GetDouble,
           props.bottom, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropXResolution, double, GetDouble,
           props.res_x, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropYResolution, double, GetDouble,
           props.res_y, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropXDpi, double, GetDouble,
           props.screen_x_dpi, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropYDpi, double, GetDouble,
           props.screen_y_dpi, float, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropOrientationMinimum,
           double, GetDouble, props.orientation_minimum, float, false);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropOrientationMaximum,
           double, GetDouble, props.orientation_maximum, float, false);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropMaxFingerCount, int, GetInteger,
           props.max_finger_cnt, unsigned short, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropMaxTouchCount, int, GetInteger,
           props.max_touch_cnt, unsigned short, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropSupportsT5R2, bool, GetBoolean,
           props.supports_t5r2, bool, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropSemiMt, bool, GetBoolean,
           props.support_semi_mt, bool, true);
  PARSE_HP(obj, ActivityLog::kKeyHardwarePropIsButtonPad, bool, GetBoolean,
           props.is_button_pad, bool, true);
  *out_props = props;
  return true;
}

#undef PARSE_HP

bool ActivityReplay::ParseEntry(DictionaryValue* entry) {
  string type;
  if (!entry->GetString(ActivityLog::kKeyType, &type)) {
    Err("Can't get entry type.");
    return false;
  }
  if (type == ActivityLog::kKeyHardwareState)
    return ParseHardwareState(entry);
  if (type == ActivityLog::kKeyTimerCallback)
    return ParseTimerCallback(entry);
  if (type == ActivityLog::kKeyCallbackRequest)
    return ParseCallbackRequest(entry);
  if (type == ActivityLog::kKeyGesture)
    return ParseGesture(entry);
  if (type == ActivityLog::kKeyPropChange)
    return ParsePropChange(entry);
  Err("Unknown entry type");
  return false;
}

bool ActivityReplay::ParseHardwareState(DictionaryValue* entry) {
  HardwareState hs = HardwareState();
  if (!entry->GetInteger(ActivityLog::kKeyHardwareStateButtonsDown,
                         &hs.buttons_down)) {
    Err("Unable to parse hardware state buttons down");
    return false;
  }
  int temp;
  if (!entry->GetInteger(ActivityLog::kKeyHardwareStateTouchCnt,
                         &temp)) {
    Err("Unable to parse hardware state touch count");
    return false;
  }
  hs.touch_cnt = temp;
  if (!entry->GetDouble(ActivityLog::kKeyHardwareStateTimestamp,
                        &hs.timestamp)) {
    Err("Unable to parse hardware state timestamp");
    return false;
  }
  ListValue* fingers = NULL;
  if (!entry->GetList(ActivityLog::kKeyHardwareStateFingers, &fingers)) {
    Err("Unable to parse hardware state fingers");
    return false;
  }
  // Sanity check
  if (fingers->GetSize() > 30) {
    Err("Too many fingers in hardware state");
    return false;
  }
  FingerState fs[fingers->GetSize()];
  for (size_t i = 0; i < fingers->GetSize(); ++i) {
    DictionaryValue* finger_state = NULL;
    if (!fingers->GetDictionary(i, &finger_state)) {
      Err("Invalid entry at index %zu", i);
      return false;
    }
    if (!ParseFingerState(finger_state, &fs[i]))
      return false;
  }
  hs.fingers = fs;
  hs.finger_cnt = fingers->GetSize();
  double temp_double;
  if (!entry->GetDouble(ActivityLog::kKeyHardwareStateRelX,
                        &temp_double)) {
    // There may not have rel_ entries for old logs
    Log("Unable to parse hardware state rel_x");
  } else {
    hs.rel_x = temp_double;
    if (!entry->GetDouble(ActivityLog::kKeyHardwareStateRelY,
                          &temp_double)) {
      Err("Unable to parse hardware state rel_y");
      return false;
    }
    hs.rel_y = temp_double;
    if (!entry->GetDouble(ActivityLog::kKeyHardwareStateRelWheel,
                          &temp_double)) {
      Err("Unable to parse hardware state rel_wheel");
      return false;
    }
    hs.rel_wheel = temp_double;
    if (!entry->GetDouble(ActivityLog::kKeyHardwareStateRelHWheel,
                          &temp_double)) {
      Err("Unable to parse hardware state rel_hwheel");
      return false;
    }
    hs.rel_hwheel = temp_double;
  }
  log_.LogHardwareState(hs);
  return true;
}

bool ActivityReplay::ParseFingerState(DictionaryValue* entry,
                                      FingerState* out_fs) {
  double dbl = 0.0;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStateTouchMajor, &dbl)) {
    Err("can't parse finger's touch major");
    return false;
  }
  out_fs->touch_major = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStateTouchMinor, &dbl)) {
    Err("can't parse finger's touch minor");
    return false;
  }
  out_fs->touch_minor = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStateWidthMajor, &dbl)) {
    Err("can't parse finger's width major");
    return false;
  }
  out_fs->width_major = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStateWidthMinor, &dbl)) {
    Err("can't parse finger's width minor");
    return false;
  }
  out_fs->width_minor = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStatePressure, &dbl)) {
    Err("can't parse finger's pressure");
    return false;
  }
  out_fs->pressure = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStateOrientation, &dbl)) {
    Err("can't parse finger's orientation");
    return false;
  }
  out_fs->orientation = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStatePositionX, &dbl)) {
    Err("can't parse finger's position x");
    return false;
  }
  out_fs->position_x = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyFingerStatePositionY, &dbl)) {
    Err("can't parse finger's position y");
    return false;
  }
  out_fs->position_y = dbl;
  int tr_id;
  if (!entry->GetInteger(ActivityLog::kKeyFingerStateTrackingId, &tr_id)) {
    Err("can't parse finger's tracking id");
    string json;
    base::JSONWriter::WriteWithOptions(entry,
                                       base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                       &json);
    Err("fs: %s", json.c_str());
    return false;
  }
  out_fs->tracking_id = tr_id;
  int flags = 0;
  if (!entry->GetInteger(ActivityLog::kKeyFingerStateFlags, &flags))
    Err("can't parse finger's flags; continuing.");
  out_fs->flags = static_cast<unsigned>(flags);
  return true;
}

bool ActivityReplay::ParseTimerCallback(DictionaryValue* entry) {
  stime_t now = 0.0;
  if (!entry->GetDouble(ActivityLog::kKeyTimerCallbackNow, &now)) {
    Err("can't parse timercallback");
    return false;
  }
  log_.LogTimerCallback(now);
  return true;
}

bool ActivityReplay::ParseCallbackRequest(DictionaryValue* entry) {
  stime_t when = 0.0;
  if (!entry->GetDouble(ActivityLog::kKeyCallbackRequestWhen, &when)) {
    Err("can't parse callback request");
    return false;
  }
  log_.LogCallbackRequest(when);
  return true;
}

bool ActivityReplay::ParseGesture(DictionaryValue* entry) {
  string gesture_type;
  if (!entry->GetString(ActivityLog::kKeyGestureType, &gesture_type)) {
    Err("can't parse gesture type");
    return false;
  }
  Gesture gs;

  if (!entry->GetDouble(ActivityLog::kKeyGestureStartTime, &gs.start_time)) {
    Err("Failed to parse gesture start time");
    return false;
  }
  if (!entry->GetDouble(ActivityLog::kKeyGestureEndTime, &gs.end_time)) {
    Err("Failed to parse gesture end time");
    return false;
  }

  if (gesture_type == ActivityLog::kValueGestureTypeContactInitiated) {
    gs.type = kGestureTypeContactInitiated;
  } else if (gesture_type == ActivityLog::kValueGestureTypeMove) {
    if (!ParseGestureMove(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypeScroll) {
    if (!ParseGestureScroll(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypeSwipe) {
    if (!ParseGestureSwipe(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypeSwipeLift) {
    if (!ParseGestureSwipeLift(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypePinch) {
    if (!ParseGesturePinch(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypeButtonsChange) {
    if (!ParseGestureButtonsChange(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypeFling) {
    if (!ParseGestureFling(entry, &gs))
      return false;
  } else if (gesture_type == ActivityLog::kValueGestureTypeMetrics) {
    if (!ParseGestureMetrics(entry, &gs))
      return false;
  } else {
    gs.type = kGestureTypeNull;
  }
  log_.LogGesture(gs);
  return true;
}

bool ActivityReplay::ParseGestureMove(DictionaryValue* entry, Gesture* out_gs) {
  out_gs->type = kGestureTypeMove;
  double dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureMoveDX, &dbl)) {
    Err("can't parse move dx");
    return false;
  }
  out_gs->details.move.dx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureMoveDY, &dbl)) {
    Err("can't parse move dy");
    return false;
  }
  out_gs->details.move.dy = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureMoveOrdinalDX, &dbl)) {
    Err("can't parse move ordinal_dx");
    return false;
  }
  out_gs->details.move.ordinal_dx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureMoveOrdinalDY, &dbl)) {
    Err("can't parse move ordinal_dy");
    return false;
  }
  out_gs->details.move.ordinal_dy = dbl;
  return true;
}

bool ActivityReplay::ParseGestureScroll(DictionaryValue* entry,
                                        Gesture* out_gs) {
  out_gs->type = kGestureTypeScroll;
  double dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureScrollDX, &dbl)) {
    Err("can't parse scroll dx");
    return false;
  }
  out_gs->details.scroll.dx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureScrollDY, &dbl)) {
    Err("can't parse scroll dy");
    return false;
  }
  out_gs->details.scroll.dy = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureScrollOrdinalDX, &dbl)) {
    Err("can't parse scroll ordinal_dx");
    return false;
  }
  out_gs->details.scroll.ordinal_dx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureScrollOrdinalDY, &dbl)) {
    Err("can't parse scroll ordinal_dy");
    return false;
  }
  out_gs->details.scroll.ordinal_dy = dbl;
  return true;
}

bool ActivityReplay::ParseGestureSwipe(DictionaryValue* entry,
                                       Gesture* out_gs) {
  out_gs->type = kGestureTypeSwipe;
  double dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureSwipeDX, &dbl)) {
    Err("can't parse swipe dx");
    return false;
  }
  out_gs->details.swipe.dx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureSwipeDY, &dbl)) {
    Err("can't parse swipe dy");
    return false;
  }
  out_gs->details.swipe.dy = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureSwipeOrdinalDX, &dbl)) {
    Err("can't parse swipe ordinal_dx");
    return false;
  }
  out_gs->details.swipe.ordinal_dx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureSwipeOrdinalDY, &dbl)) {
    Err("can't parse swipe ordinal_dy");
    return false;
  }
  out_gs->details.swipe.ordinal_dy = dbl;
  return true;
}

bool ActivityReplay::ParseGestureSwipeLift(DictionaryValue* entry,
                                           Gesture* out_gs) {
  out_gs->type = kGestureTypeSwipeLift;
  return true;
}

bool ActivityReplay::ParseGesturePinch(DictionaryValue* entry,
                                       Gesture* out_gs) {
  out_gs->type = kGestureTypePinch;
  double dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGesturePinchDZ, &dbl)) {
    Err("can't parse pinch dz");
    return false;
  }
  out_gs->details.pinch.dz = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGesturePinchOrdinalDZ, &dbl)) {
    Err("can't parse pinch ordinal_dz");
    return false;
  }
  out_gs->details.pinch.ordinal_dz = dbl;
  return true;
}

bool ActivityReplay::ParseGestureButtonsChange(DictionaryValue* entry,
                                               Gesture* out_gs) {
  out_gs->type = kGestureTypeButtonsChange;
  int temp;
  if (!entry->GetInteger(ActivityLog::kKeyGestureButtonsChangeDown,
                         &temp)) {
    Err("can't parse buttons down");
    return false;
  }
  out_gs->details.buttons.down = temp;
  if (!entry->GetInteger(ActivityLog::kKeyGestureButtonsChangeUp,
                         &temp)) {
    Err("can't parse buttons up");
    return false;
  }
  out_gs->details.buttons.up = temp;
  return true;
}

bool ActivityReplay::ParseGestureFling(DictionaryValue* entry,
                                       Gesture* out_gs) {
  out_gs->type = kGestureTypeFling;
  double dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureFlingVX, &dbl)) {
    Err("can't parse fling vx");
    return false;
  }
  out_gs->details.fling.vx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureFlingVY, &dbl)) {
    Err("can't parse fling vy");
    return false;
  }
  out_gs->details.fling.vy = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureFlingOrdinalVX, &dbl)) {
    Err("can't parse fling ordinal_vx");
    return false;
  }
  out_gs->details.fling.ordinal_vx = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureFlingOrdinalVY, &dbl)) {
    Err("can't parse fling ordinal_vy");
    return false;
  }
  out_gs->details.fling.ordinal_vy = dbl;
  int state;
  if (!entry->GetInteger(ActivityLog::kKeyGestureFlingState, &state)) {
    Err("can't parse scroll is_scroll_begin");
    return false;
  }
  out_gs->details.fling.fling_state = state;
  return true;
}

bool ActivityReplay::ParseGestureMetrics(DictionaryValue* entry,
                                       Gesture* out_gs) {
  out_gs->type = kGestureTypeMetrics;
  double dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureMetricsData1, &dbl)) {
    Err("can't parse metrics data 1");
    return false;
  }
  out_gs->details.metrics.data[0] = dbl;
  if (!entry->GetDouble(ActivityLog::kKeyGestureMetricsData2, &dbl)) {
    Err("can't parse metrics data 2");
    return false;
  }
  out_gs->details.metrics.data[1] = dbl;
  int type;
  if (!entry->GetInteger(ActivityLog::kKeyGestureMetricsType, &type)) {
    Err("can't parse metrics type");
    return false;
  }
  if (type == 0) {
    out_gs->details.metrics.type = kGestureMetricsTypeNoisyGround;
    return true;
  }
  out_gs->details.metrics.type = kGestureMetricsTypeUnknown;
  return true;
}

bool ActivityReplay::ParsePropChange(DictionaryValue* entry) {
  ActivityLog::PropChangeEntry prop_change;
  string type;
  if (!entry->GetString(ActivityLog::kKeyPropChangeType, &type)) {
    Err("Can't get prop change type");
    return false;
  }

  if (type == ActivityLog::kValuePropChangeTypeBool) {
    prop_change.type = ActivityLog::PropChangeEntry::kBoolProp;
    bool val;
    if (!entry->GetBoolean(ActivityLog::kKeyPropChangeValue, &val)) {
      Err("Can't parse prop change value");
      return false;
    }
    prop_change.value.bool_val = val;
  } else if (type == ActivityLog::kValuePropChangeTypeDouble) {
    prop_change.type = ActivityLog::PropChangeEntry::kDoubleProp;
    if (!entry->GetDouble(ActivityLog::kKeyPropChangeValue,
                          &prop_change.value.double_val)) {
      Err("Can't parse prop change value");
      return false;
    }
  } else if (type == ActivityLog::kValuePropChangeTypeInt) {
    prop_change.type = ActivityLog::PropChangeEntry::kIntProp;
    if (!entry->GetInteger(ActivityLog::kKeyPropChangeValue,
                           &prop_change.value.int_val)) {
      Err("Can't parse prop change value");
      return false;
    }
  } else if (type == ActivityLog::kValuePropChangeTypeShort) {
    prop_change.type = ActivityLog::PropChangeEntry::kIntProp;
    int val;
    if (!entry->GetInteger(ActivityLog::kKeyPropChangeValue,
                           &val)) {
      Err("Can't parse prop change value");
      return false;
    }
    prop_change.value.short_val = val;
  } else {
    Err("Unable to parse prop change type %s", type.c_str());
    return false;
  }
  string name;
  if (!entry->GetString(ActivityLog::kKeyPropChangeName, &name)) {
    Err("Unable to parse prop change name.");
    return false;
  }
  const string* stored_name = new string(name.c_str());  // alloc
  // transfer ownership:
  names_.push_back(std::tr1::shared_ptr<const string>(stored_name));
  prop_change.name = stored_name->c_str();
  log_.LogPropChange(prop_change);
  return true;
}

// Replay the log and verify the output in a strict way.
void ActivityReplay::Replay(Interpreter* interpreter,
                            MetricsProperties* mprops) {
  interpreter->Initialize(&hwprops_, NULL, mprops, this);

  stime_t last_timeout_req = -1.0;
  // Use last_gs to save a copy of last gesture.
  Gesture last_gs;
  for (size_t i = 0; i < log_.size(); ++i) {
    ActivityLog::Entry* entry = log_.GetEntry(i);
    switch (entry->type) {
      case ActivityLog::kHardwareState: {
        last_timeout_req = -1.0;
        HardwareState hs = entry->details.hwstate;
        for (size_t i = 0; i < hs.finger_cnt; i++)
          Log("Input Finger ID: %d", hs.fingers[i].tracking_id);
        interpreter->SyncInterpret(&hs, &last_timeout_req);
        break;
      }
      case ActivityLog::kTimerCallback: {
        last_timeout_req = -1.0;
        interpreter->HandleTimer(entry->details.timestamp, &last_timeout_req);
        break;
      }
      case ActivityLog::kCallbackRequest:
        if (!DoubleEq(last_timeout_req, entry->details.timestamp)) {
          Err("Expected timeout request of %f, but log has %f (entry idx %zu)",
              last_timeout_req, entry->details.timestamp, i);
        }
        break;
      case ActivityLog::kGesture: {
        bool matched = false;
        while (!consumed_gestures_.empty() && !matched) {
          if (consumed_gestures_.front() == entry->details.gesture) {
            Log("Gesture matched:\n  Actual gesture: %s.\nExpected gesture: %s",
                consumed_gestures_.front().String().c_str(),
                entry->details.gesture.String().c_str());
            matched = true;
          } else {
            Log("Unmatched actual gesture: %s\n",
                consumed_gestures_.front().String().c_str());
            ADD_FAILURE();
          }
          consumed_gestures_.pop_front();
        }
        if (!matched) {
          Log("Missing logged gesture: %s",
              entry->details.gesture.String().c_str());
          ADD_FAILURE();
        }
        break;
      }
      case ActivityLog::kPropChange:
        ReplayPropChange(entry->details.prop_change);
        break;
    }
  }
  while (!consumed_gestures_.empty()) {
    Log("Unmatched actual gesture: %s\n",
        consumed_gestures_.front().String().c_str());
    ADD_FAILURE();
    consumed_gestures_.pop_front();
  }
}

void ActivityReplay::ConsumeGesture(const Gesture& gesture) {
  consumed_gestures_.push_back(gesture);
}

bool ActivityReplay::ReplayPropChange(
    const ActivityLog::PropChangeEntry& entry) {
  if (!prop_reg_) {
    Err("Missing prop registry.");
    return false;
  }
  ::set<Property*> props = prop_reg_->props();
  Property* prop = NULL;
  for (::set<Property*>::iterator it = props.begin(), e = props.end(); it != e;
       ++it) {
    prop = *it;
    if (strcmp(prop->name(), entry.name) == 0)
      break;
    prop = NULL;
  }
  if (!prop) {
    Err("Unable to find prop %s to set.", entry.name);
    return false;
  }
  scoped_ptr< ::Value> value;
  switch (entry.type) {
    case ActivityLog::PropChangeEntry::kBoolProp:
      value.reset(::Value::CreateBooleanValue(entry.value.bool_val));
      break;
    case ActivityLog::PropChangeEntry::kDoubleProp:
      value.reset(::Value::CreateDoubleValue(entry.value.double_val));
      break;
    case ActivityLog::PropChangeEntry::kIntProp:
      value.reset(::Value::CreateIntegerValue(entry.value.int_val));
      break;
    case ActivityLog::PropChangeEntry::kShortProp:
      value.reset(::Value::CreateIntegerValue(entry.value.short_val));
      break;
  }
  prop->SetValue(value.get());
  prop->HandleGesturesPropWritten();
  return true;
}

}  // namespace gestures