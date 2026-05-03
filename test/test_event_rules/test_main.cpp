#include <unity.h>

#include "event_rules.h"

namespace {

SensorSample BuildSample(std::uint32_t timestamp, float current, float power) {
    SensorSample sample = {};
    sample.timestamp = timestamp;
    sample.current = current;
    sample.power = power;
    sample.valid = true;
    return sample;
}

void test_power_spike_detected_on_large_positive_delta() {
    const SensorSample previous = BuildSample(1, 1.0F, 100.0F);
    const SensorSample current = BuildSample(2, 1.2F, 450.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_TRUE(evaluation.emit_power_spike);
    TEST_ASSERT_FALSE(evaluation.emit_overload_warning);
    TEST_ASSERT_FALSE(evaluation.emit_power_down);
}

void test_overload_warning_only_on_rising_edge() {
    const SensorSample previous = BuildSample(1, 7.5F, 200.0F);
    const SensorSample current = BuildSample(2, 8.5F, 220.0F);

    const EventEvaluation first = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_TRUE(first.emit_overload_warning);
    TEST_ASSERT_TRUE(first.overload_active);

    const EventEvaluation second = EvaluateEventTransitions(current, current, 8.0F, 300.0F, true);
    TEST_ASSERT_FALSE(second.emit_overload_warning);
    TEST_ASSERT_TRUE(second.overload_active);
}

void test_power_down_detected_from_active_to_zero() {
    const SensorSample previous = BuildSample(1, 1.4F, 320.0F);
    const SensorSample current = BuildSample(2, 0.0F, 0.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_TRUE(evaluation.emit_power_down);
}

void test_invalid_samples_do_not_emit_events() {
    SensorSample previous = BuildSample(1, 1.0F, 100.0F);
    SensorSample current = BuildSample(2, 9.0F, 500.0F);
    current.valid = false;

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_FALSE(evaluation.emit_power_spike);
    TEST_ASSERT_FALSE(evaluation.emit_overload_warning);
    TEST_ASSERT_FALSE(evaluation.emit_power_down);
    TEST_ASSERT_FALSE(evaluation.overload_active);
}

void test_power_spike_not_triggered_on_small_delta() {
    const SensorSample previous = BuildSample(1, 1.0F, 100.0F);
    const SensorSample current = BuildSample(2, 1.1F, 150.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_FALSE(evaluation.emit_power_spike);
}

void test_overload_not_triggered_below_threshold() {
    const SensorSample previous = BuildSample(1, 6.0F, 200.0F);
    const SensorSample current = BuildSample(2, 7.9F, 210.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_FALSE(evaluation.emit_overload_warning);
    TEST_ASSERT_FALSE(evaluation.overload_active);
}

void test_power_down_not_triggered_when_load_stays_active() {
    const SensorSample previous = BuildSample(1, 1.4F, 320.0F);
    const SensorSample current = BuildSample(2, 1.3F, 310.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_FALSE(evaluation.emit_power_down);
}

void test_power_down_not_triggered_when_load_was_already_zero() {
    const SensorSample previous = BuildSample(1, 0.0F, 0.0F);
    const SensorSample current = BuildSample(2, 0.0F, 0.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_FALSE(evaluation.emit_power_down);
}

void test_spike_and_overload_can_trigger_simultaneously() {
    const SensorSample previous = BuildSample(1, 7.5F, 100.0F);
    const SensorSample current = BuildSample(2, 9.0F, 450.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_TRUE(evaluation.emit_power_spike);
    TEST_ASSERT_TRUE(evaluation.emit_overload_warning);
}

void test_power_spike_not_triggered_on_power_decrease() {
    const SensorSample previous = BuildSample(1, 1.0F, 500.0F);
    const SensorSample current = BuildSample(2, 0.5F, 100.0F);

    const EventEvaluation evaluation = EvaluateEventTransitions(previous, current, 8.0F, 300.0F, false);
    TEST_ASSERT_FALSE(evaluation.emit_power_spike);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_power_spike_detected_on_large_positive_delta);
    RUN_TEST(test_overload_warning_only_on_rising_edge);
    RUN_TEST(test_power_down_detected_from_active_to_zero);
    RUN_TEST(test_invalid_samples_do_not_emit_events);
    RUN_TEST(test_power_spike_not_triggered_on_small_delta);
    RUN_TEST(test_overload_not_triggered_below_threshold);
    RUN_TEST(test_power_down_not_triggered_when_load_stays_active);
    RUN_TEST(test_power_down_not_triggered_when_load_was_already_zero);
    RUN_TEST(test_spike_and_overload_can_trigger_simultaneously);
    RUN_TEST(test_power_spike_not_triggered_on_power_decrease);
    return UNITY_END();
}
