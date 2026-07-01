#include "otel_structs_common.h"
#include "otel_structs_resource.h"

typedef struct Exemplar {
    struct KeyValue *filtered_attributes;
    uint64_t time_unix_nano;
    uint8_t which_value;
    union {
        double as_double;
        int64_t as_int;
    } value;
    uint8_t *span_id;
    uint8_t *trace_id;
} Exemplar_t;

typedef struct NumberDataPoint {
    KeyValue_t *attributes;

    uint64_t start_time_unix_nano;
    uint64_t time_unix_nano;

    uint8_t which_value;
    union {
        double as_double;
        int64_t as_int;
    } value;
    Exemplar_t exemplars;
    uint32_t flags;
    
    struct NumberDataPoint *next;
} NumberDataPoint_t;

typedef struct Gauge {
    NumberDataPoint_t *data_points;
} Gauge_t;

typedef struct Sum {
    NumberDataPoint_t *data_points;
    
    uint8_t aggregationTemporality;
    bool is_monotonic;
} Sum_t;

typedef struct HistogramDataPoint {
    KeyValue_t *attributes;
    uint64_t start_time_unix_nano;
    uint64_t time_unix_nano;
    uint64_t count;
    double sum;
    uint64_t bucket_counts;
    double *explicit_bounds;
    Exemplar_t exemplars;
    uint32_t flags;
    double min;
    double max;
    struct HistogramDataPoint *next;
} HistogramDataPoint_t;

typedef struct Histogram {
    HistogramDataPoint_t *data_points;
    
    uint8_t aggregationTemporality;
} Histogram_t;

typedef struct Buckets {
    int32_t offset;
    uint64_t *bucket_counts;
} Buckets_t;

typedef struct ExponentialHistogramDataPoint {
    KeyValue_t *attributes;
    uint64_t start_time_unix_nano;
    uint64_t time_unix_nano;
    uint64_t count;
    double sum;
    int32_t scale;
    uint64_t zero_count;
    Buckets_t positive;
    Buckets_t negative;
    uint32_t flags;
    Exemplar_t exemplars;
    double min;
    double max;
    double zero_threshold;

    struct HistogramDataPoint *next;
} ExponentialHistogramDataPoint_t;

typedef struct ExponentialHistogram {
    ExponentialHistogramDataPoint_t *data_points;
    
    uint8_t aggregationTemporality;
} ExponentialHistogram_t;

typedef struct ValueAtQuantile {
    double quantile;
    double value;
} ValueAtQuantile_t;

typedef struct SummaryDataPoint {
    KeyValue_t *attributes;

    uint64_t start_time_unix_nano;
    uint64_t time_unix_nano;
    uint64_t count;
    double sum;

    ValueAtQuantile_t *quantile_values;
    uint32_t flags;
    struct SummaryDataPoint *next;
} SummaryDataPoint_t;

typedef struct Summary {
    SummaryDataPoint_t *data_points;
} Summary_t;

typedef struct Metric {
    char *name;
    char *unit;
    char *description;

    uint8_t which_data;
    union {
        Sum_t *sum;
        Gauge_t *gauge;
        Histogram_t histogram;
        ExponentialHistogram_t *exponential_histogram;
        Summary_t *summary;
    } data;
    
    KeyValue_t *metadata;

    // For "repeated"
    struct Metric *next;
} Metric_t;

typedef struct ScopeMetrics {
    struct {
        // From Common
        char *name;
        char *version;
        KeyValue_t *attributes;
        uint32_t dropped_attributes_count;
    } scope; 
    Metric_t *metrics;
    char *schema_url;
    struct ScopeMetrics *next;
} ScopeMetrics_t;

typedef struct ResourceMetrics {
    Resource_t resource;
    ScopeMetrics_t *scope_metrics;
    char *schema_url;
} ResourceMetrics_t;
