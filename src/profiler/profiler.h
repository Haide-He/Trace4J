#ifndef _PROFILER_H
#define _PROFILER_H

#include <signal.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include "perf_interface.h"
#include "stacktraces.h"
#include "code_cache.h"
#include "watchpoint.h"
#include "profiler_support.h"
#include "splay.h"
#include "lock.h"

extern "C" {
#include "xed/xed-interface.h"
#include "xed/xed-portability.h"
//#include "xed/xed_operand_enum.h"
// #include "xed/xed-common-hdrs.h"
//#include "xed/xed-agen.h"
}

class Profiler {
public:

  Profiler();
 
  void init();
  void shutdown();

  void threadStart();
  void threadEnd();
  void IncrementGCCouter();

  int output_method(const char *buf); 

  inline CodeCacheManager &getCodeCacheManager() {return _code_cache_manager;}

  inline MethodCache &getUnCompiledMethodCache() {return _uncompiled_method_cache;}

  static inline Profiler &getProfiler(){return _instance;}

  static ASGCT_FN _asgct;

private:
  static void OnSample(int eventID, perf_sample_data_t *sampleData, void *uCtxt);
  static WP_TriggerAction_t OnRetWatchPoint(WP_TriggerInfo_t *wpi);
  static bool OnBranchWatchPoint(WP_TriggerInfo_t *wpi);
  static void GenericAnalysis(perf_sample_data_t *sampleData, void *uCtxt, jmethodID method_id, uint32_t method_version, uint32_t threshold, int metric_id2);

  static void decoder(void *sampleIP, void *stackAddr, void *uCtxt);
  static void* get_branch_target(xed_operand_enum_t op0_name, void* TempIp, xed_decoded_inst_t* xedd, int inst_length,  void *uCtxt);
  static void print_operands(xed_decoded_inst_t* xedd);
  static void* readReg(xed_decoded_inst_t* xedd, xed_operand_enum_t op_name, void *uCtxt);

  inline void output_statistics(); 

  std::ofstream _method_file;
  std::ofstream _statistics_file;

  CodeCacheManager _code_cache_manager;
  MethodCache _uncompiled_method_cache; // used to store the methods which are never added but shown in context;

  static Profiler _instance;
};

#endif
