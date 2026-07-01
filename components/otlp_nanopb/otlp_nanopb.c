#include <stdio.h>
#include <stdint.h>
#include "sys/time.h"
#include "otlp_nanopb.h"

uint64_t time_unix_nano(void) {
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    return (uint64_t)tv_now.tv_sec * 1000000000UL + (uint64_t)tv_now.tv_usec * 1000;
}

bool encode_string(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
  const char *str = (const char *)(*arg);

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_string(stream, (const uint8_t *)str, strlen(str));
}

bool encode_attribute(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg) {
    KeyValue_t *input = (KeyValue_t *)(*arg);
    
    // while there are attributes
    while (input != NULL) {
        opentelemetry_proto_common_v1_KeyValue root = {
            .key.arg = input->key, 
            .key.funcs.encode = encode_string,
            .has_value = true,
        };

        if (input->which_value == opentelemetry_proto_common_v1_AnyValue_string_value_tag) {
            root.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
            root.value.value.string_value.arg = input->value.string_value;
            root.value.value.string_value.funcs.encode = encode_string;
        }
        
        if (input->which_value == opentelemetry_proto_common_v1_AnyValue_int_value_tag) {
            root.value.which_value = opentelemetry_proto_common_v1_AnyValue_int_value_tag;
            root.value.value.int_value = input->value.int_value;
        }

        // Add tag for the data
        if (!pb_encode_tag_for_field(ostream, field)) {
            return false;
        }
        // Add data
        if (!pb_encode_submessage(ostream, opentelemetry_proto_common_v1_KeyValue_fields, &root)) {
            return false;
        }

        input = input->next;
    }

    return true;
}

bool encode_number_data_point(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg) {
    NumberDataPoint_t *input = (NumberDataPoint_t *)(*arg);

    while (input != NULL) {
        opentelemetry_proto_metrics_v1_NumberDataPoint root = {
            .time_unix_nano = time_unix_nano(),
            .which_value = input->which_value,
            .attributes = {
                .arg = input->attributes,
                .funcs.encode = encode_attribute,
            },
        };

        switch (root.which_value)
        {
        case opentelemetry_proto_metrics_v1_NumberDataPoint_as_double_tag:
            root.value.as_double = input->value.as_double;
            break;
        case opentelemetry_proto_metrics_v1_NumberDataPoint_as_int_tag:
            root.value.as_int = input->value.as_int;
            break;
        
        default:
            return false;
            break;
        }

        if (!pb_encode_tag_for_field(ostream, field)) {
            return false;
        }
        if (!pb_encode_submessage(ostream, opentelemetry_proto_metrics_v1_NumberDataPoint_fields, &root)) {
            return false;
        }

        input = input->next;
    }

    return true;
}

bool encode_metrics(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg) {
    Metric_t *input = (Metric_t *)(*arg);

    while (input != NULL) {
        opentelemetry_proto_metrics_v1_Metric root = {
            .name = {
                .arg = input->name,
                .funcs.encode = encode_string,
            },
            .unit = {
                .arg = input->unit,
                .funcs.encode = encode_string,
            },
            .description = {
                .arg = input->description,
                .funcs.encode = encode_string,
            },

            .which_data = input->which_data,
            .data.gauge.data_points = {
                .arg = input->data.gauge->data_points,
                .funcs.encode = encode_number_data_point,
            }, 
        };

        if (!pb_encode_tag_for_field(ostream, field)) {
            return false;
        }
        if (!pb_encode_submessage(ostream, opentelemetry_proto_metrics_v1_Metric_fields, &root)) {
            return false;
        }

        input = input->next;
    }

    return true;
}

bool encode_scope_metrics(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg) {
    ScopeMetrics_t *input = (ScopeMetrics_t *)(*arg);

    while (input != NULL) {
        opentelemetry_proto_metrics_v1_ScopeMetrics root = {
            .has_scope = true,
            .scope = {
                .name = {
                    .arg = input->scope.name,
                    .funcs.encode = encode_string,
                },
                .version = {
                    .arg = input->scope.version,
                    .funcs.encode = encode_string,
                },
                .attributes = {
                    .arg = input->scope.attributes,
                    .funcs.encode = encode_attribute,
                },
            },
            .metrics = {
                .arg = input->metrics,
                .funcs.encode = encode_metrics,
            },
        };

        if (!pb_encode_tag_for_field(ostream, field)) {
            return false;
        }
        if (!pb_encode_submessage(ostream, opentelemetry_proto_metrics_v1_ScopeMetrics_fields, &root)) {
            return false;
        }

        input = input->next;
    }

    return true;
}

bool encode_resource_metrics(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg) {  
    ResourceMetrics_t *input = (ResourceMetrics_t *)(*arg);

    if (input == NULL) {
        return false;
    }

    opentelemetry_proto_metrics_v1_ResourceMetrics root = {
        .has_resource = true,
        .resource.attributes = {
            .arg = input->resource.attributes,
            .funcs.encode = encode_attribute,
        },
        .scope_metrics = {
            .arg = input->scope_metrics,
            .funcs.encode = encode_scope_metrics,
        },
    };

    if (!pb_encode_tag_for_field(ostream, field)) {
        return false;
    }
    if (!pb_encode_submessage(ostream, opentelemetry_proto_metrics_v1_ResourceMetrics_fields, &root)) {
        return false;
    }

    return true;
}

size_t otlp_nanopb_generate(ResourceMetrics_t *input, char *output) { 
    uint16_t output_len = 0;

    opentelemetry_proto_metrics_v1_MetricsData root = {
        .resource_metrics = {
            .arg = input,
            .funcs.encode = encode_resource_metrics,
        },
    };
    pb_ostream_t stream = pb_ostream_from_buffer((pb_byte_t *)output, 1024);
    int status = pb_encode(&stream, opentelemetry_proto_metrics_v1_MetricsData_fields, &root);

    return stream.bytes_written;
}

void otlp_nanopb_print(char *pb_data, size_t pb_len) {
    printf("[Protobuf] Output:\n");
    for (int x = 0; x < pb_len; x++) {
        if (pb_data[x] >= 0x20 && pb_data[x] < 0x7F) {
            printf("%c", pb_data[x]);
        } else {
            printf(".");
        }
        if ((x % 64) == 63) {
            printf("\n");
        }
    }
    printf("\nEOM\n");
}