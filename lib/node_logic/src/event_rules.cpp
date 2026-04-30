#include "event_rules.h"

namespace {

constexpr float kLoadActiveThreshold = 0.05F;
constexpr float kLoadZeroThreshold = 0.01F;

bool IsLoadActive(const SensorSample& sample) {
    return sample.current > kLoadActiveThreshold || sample.power > kLoadActiveThreshold;
}

bool IsLoadZero(const SensorSample& sample) {
    return sample.current <= kLoadZeroThreshold && sample.power <= kLoadZeroThreshold;
}

}  // namespace

EventEvaluation EvaluateEventTransitions(const SensorSample& previous_sample,
                                         const SensorSample& current_sample,
                                         float warning_current_threshold,
                                         float power_spike_delta,
                                         bool previous_overload_active) {
    EventEvaluation evaluation = {};

    if (!previous_sample.valid || !current_sample.valid) {
        evaluation.overload_active = previous_overload_active;
        return evaluation;
    }

    evaluation.emit_power_spike = (current_sample.power - previous_sample.power) > power_spike_delta;

    const bool overload_now = current_sample.current > warning_current_threshold;
    evaluation.emit_overload_warning = overload_now && !previous_overload_active;
    evaluation.overload_active = overload_now;

    evaluation.emit_power_down = IsLoadActive(previous_sample) && IsLoadZero(current_sample);
    return evaluation;
}
