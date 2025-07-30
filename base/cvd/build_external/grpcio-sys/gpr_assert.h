
// grpcio-sys 0.13.0 uses grpc 1.56.2 which has GPR_ASSERT. According to
// https://github.com/grpc/grpc/commit/a3aa81e179c49d26d2604fcc4ffb97a099b6602f
// GPR_ASSERT is replaced with absl CHECK.

#include "absl/log/check.h"
#define GPR_ASSERT(...) CHECK(__VA_ARGS__)
