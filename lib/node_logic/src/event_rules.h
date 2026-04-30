#pragma once

#include "types.h"

struct EventEvaluation {
    bool emit_power_spike;
    bool emit_overload_warning;
    bool emit_power_down;
    bool overload_active;
};

EventEvaluation EvaluateEventTransitions(const SensorSample& previous_sample,
                                         const SensorSample& current_sample,
                                         float warning_current_threshold,
                                         float power_spike_delta,
                                         bool previous_overload_active);
