#ifndef OTLP_CLIMATE_H
#define OTLP_CLIMATE_H

#include "otlp_nanopb.h"

size_t otlp_climate(char *buf, int64_t sensor_id, double temp, int64_t rhumid);

#endif