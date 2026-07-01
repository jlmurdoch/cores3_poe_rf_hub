idf_component_register(SRCS "otlp_nanopb.c" 
                    "nanopb/pb_common.c" "nanopb/pb_encode.c" 
                    "opentelemetry/proto/common/v1/common.pb.c" "opentelemetry/proto/metrics/v1/metrics.pb.c" "opentelemetry/proto/resource/v1/resource.pb.c"
                    INCLUDE_DIRS "include" ".")
