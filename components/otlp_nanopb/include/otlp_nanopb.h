// Standard nanopb 
#include "nanopb/pb.h"
#include "nanopb/pb_common.h"
#include "nanopb/pb_encode.h"

// Generated opentelemetry-proto files
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"

#include "otel_structs_metrics.h"

uint64_t time_unix_nano(void);

bool encode_string(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);
bool encode_attribute(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg);
bool encode_number_data_point(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg);
bool encode_metrics(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg);
bool encode_scope_metrics(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg);
bool encode_resource_metrics(pb_ostream_t *ostream, const pb_field_iter_t *field, void *const *arg);
size_t otlp_nanopb_generate(ResourceMetrics_t *input, char *output);
void otlp_nanopb_print(char *pb_data, size_t pb_len);