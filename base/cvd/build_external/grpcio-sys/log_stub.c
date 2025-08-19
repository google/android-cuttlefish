typedef enum gpr_log_severity {
  GPR_LOG_SEVERITY_DEBUG,
  GPR_LOG_SEVERITY_INFO,
  GPR_LOG_SEVERITY_ERROR
} gpr_log_severity;

struct gpr_log_func_args {
  const char* file;
  int line;
  gpr_log_severity severity;
  const char* message;
};

typedef struct gpr_log_func_args gpr_log_func_args;

typedef void (*gpr_log_func)(gpr_log_func_args* args);
void gpr_set_log_function(gpr_log_func func) {

}

void gpr_set_log_verbosity(gpr_log_severity min_severity_to_print){

}
