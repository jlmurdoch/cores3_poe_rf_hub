## OTLP via nanopb

In this directory, to set up nanopb and opentelemetry, do the following:
```
pip3 install protobuf grpcio-tools
nanopb/generator/nanopb_generator.py -L '#include "nanopb/%s"' -I opentelemetry-proto opentelemetry/proto/metrics/v1/metrics.proto
nanopb/generator/nanopb_generator.py -L '#include "nanopb/%s"' -I opentelemetry-proto opentelemetry/proto/common/v1/common.proto
nanopb/generator/nanopb_generator.py -L '#include "nanopb/%s"' -I opentelemetry-proto opentelemetry/proto/resource/v1/resource.proto
```

This will create an opentelemetry directory with the relevant headers now nanopb compliant. 

TODO:
- Make the generation of 
