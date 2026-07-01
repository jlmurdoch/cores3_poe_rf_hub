#include "otlp_climate.h"

size_t otlp_climate(char *buf, int64_t sensor_id, double temp, int64_t rhumid) {
    KeyValue_t sensor_attr = {
        .has_value = true,
        .which_value = opentelemetry_proto_common_v1_AnyValue_int_value_tag,
        .key = "sensor_id",
        .value.int_value = sensor_id,
        .next = NULL,
    };

    /*
     * Humidity Sensor
     */
    NumberDataPoint_t humidity_data_point = {
        .which_value = opentelemetry_proto_metrics_v1_NumberDataPoint_as_double_tag,
        .value.as_double = rhumid,
        .time_unix_nano = time_unix_nano(),
        .attributes = &sensor_attr,
        .next = NULL,
    };

    Gauge_t humidity_gauge = {
        .data_points = &humidity_data_point,
    };

    Metric_t humidity_metric = {
        .name = "climate.humidity",
        .unit = "%rH",
        .description = "Humidity Sensor",
        .which_data = opentelemetry_proto_metrics_v1_Metric_gauge_tag,
        .data.gauge = &humidity_gauge,
        .next = NULL,
    };

    /*
     * Temperature Sensor
     */
    NumberDataPoint_t temperature_data_point = {
        .which_value = opentelemetry_proto_metrics_v1_NumberDataPoint_as_int_tag,
        .value.as_int = temp,
        .time_unix_nano = time_unix_nano(),
        .attributes = &sensor_attr,
        .next = NULL,
    };

    Gauge_t temperature_gauge = {
        .data_points = &temperature_data_point,
    };

    Metric_t temperature_metric = {
        .name = "climate.temperature",
        .unit = "°C",
        .description = "Temperature Sensor",
        .which_data = opentelemetry_proto_metrics_v1_Metric_gauge_tag,
        .data.gauge = &temperature_gauge,
        .next = &humidity_metric, // <<------ Add other metric
    };

    /**
     * SCOPE
     */
    KeyValue_t example_sm_attr = {
        .key = "architecture",
        .has_value = true,
        .which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag,
        .value.string_value = "esp32s3",
        .next = NULL,
    };

    ScopeMetrics_t scope_metrics = {
        .scope = {
            .name = "jlm.iot.otlp",
            .version = "0.0.1",
            .attributes = &example_sm_attr,
        },
        .metrics = &temperature_metric, // <<------ Add root metric
        .next = NULL,
    };

    KeyValue_t example_ra_attr = {
        .has_value = true,
        .which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag,
        .key = "service.name",
        .value.string_value = "johns.house",
        .next = NULL,
    };

    ResourceMetrics_t resource_metrics = {
        .resource.attributes = &example_ra_attr,
        .scope_metrics = &scope_metrics,
    };

    return otlp_nanopb_generate(&resource_metrics, buf);
}