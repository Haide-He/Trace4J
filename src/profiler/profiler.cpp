#include <assert.h>
#include <errno.h>
#include <dlfcn.h>
#include <algorithm>
#include <iomanip> 
#include <stack>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sys/time.h>
#include "config.h"
#include "context.h"
#include "profiler.h"
#include "thread_data.h"
#include "perf_interface.h"
#include "stacktraces.h"
#include "agent.h"
#include "metrics.h"
#include "debug.h"
#include "x86-misc.h"
#include "context-pc.h" 
#include "safe-sampling.h"
#include "welford.h"

#define APPROX_RATE (0.01)
#define MAX_FRAME_NUM (128)
#define MAX_IP_DIFF (100000000)
#define THRESHOLD (100)

Profiler Profiler::_instance;
ASGCT_FN Profiler::_asgct = nullptr;
std::string clientName;

static SpinLock lock_map;
static std::unordered_map<Context*, Context*> map = {};

uint64_t GCCounter = 0;
// thread_local uint64_t localGCCounter = 0;

static SpinLock sk;

extern uint64_t decodeThreshold;
extern std::string selectedMethod;
uint64_t grandTotGenericCounter = 0;
thread_local uint64_t totalGenericCounter = 0;

typedef struct basic_block {
    xed_decoded_inst_t Entry_xedd;
    xed_decoded_inst_t WP_xedd;
    void* WP_IP;
    uint64_t Cnt;
} basic_block_t;

thread_local std::unordered_map<void*, basic_block_t> basic_block_map;


thread_local uint64_t decodeInsCounter = 0;

thread_local void *prevIP = (void *)0;

unsigned long rdpmc_instructions(){
    unsigned a, d, c;

    c = (1<<30);
    //__asm__ volatile("cpuid\n\t");
    __asm__ volatile("rdpmc" : "=a" (a), "=d" (d) : "c" (c));
    d <<= 17;
    return (((unsigned long)a) | ((unsigned long)d << 15));
}

namespace {

Context *constructContext(ASGCT_FN asgct, void *uCtxt, uint64_t ip, jmethodID method_id, uint32_t method_version, ContextFrame *callee_ctxt_frame) {
    ContextTree *ctxt_tree = reinterpret_cast<ContextTree *> (TD_GET(context_state));
    Context *last_ctxt = nullptr;

    ASGCT_CallFrame frames[MAX_FRAME_NUM];
    ASGCT_CallTrace trace;
    trace.frames = frames;
    trace.env_id = JVM::jni();
    asgct(&trace, MAX_FRAME_NUM, uCtxt);
    //todo:std::cerr << std::hex << ip << std::endl;
    //todo:if(trace.num_frames<1) std::cerr << "trace.num_frames = " << trace.num_frames << std::endl;
    for (int i = trace.num_frames - 1 ; i >= 1; i--) {
        // TODO: We need to consider how to include the native method.
        ContextFrame ctxt_frame;
        if (i == 0) {
            ctxt_frame.binary_addr = ip;
        }
        ctxt_frame = frames[i]; //set method_id and bci
        if (last_ctxt == nullptr) last_ctxt = ctxt_tree->addContext((uint32_t)CONTEXT_TREE_ROOT_ID, ctxt_frame);
        else last_ctxt = ctxt_tree->addContext(last_ctxt, ctxt_frame);
    }

    // leaf node 
    ContextFrame ctxt_frame;
    ctxt_frame.binary_addr = ip;
    ctxt_frame.method_id = method_id;
    ctxt_frame.method_version = method_version;
    if (last_ctxt != nullptr) last_ctxt = ctxt_tree->addContext(last_ctxt, ctxt_frame);
    else last_ctxt = ctxt_tree->addContext((uint32_t)CONTEXT_TREE_ROOT_ID, ctxt_frame);
    
    // the first instruction in the Callee
    last_ctxt = ctxt_tree->addContext(last_ctxt, *callee_ctxt_frame);
//    delete callee_ctxt_frame;
    
    return last_ctxt;
}

}

#define MAX_STACK 64                                                                              
#define MAX_EVENTS 5

typedef struct EventValueStack {                                                                   
    int top;                                                                                       
    uint64_t value[MAX_STACK][MAX_EVENTS];                                                         
} CPUEventStack_t;

//__thread struct timespec sampletimeEnd;
//__thread struct timespec sampletimeStart;
__thread int curEventId = 0;
__thread int eventFd[MAX_EVENTS];
__thread unsigned long eventRDCnt[MAX_EVENTS];
__thread unsigned long eventTempCnt;

thread_local uint64_t RuntimeCallTime[MAX_EVENTS];
thread_local uint64_t RuntimeCallCount[MAX_EVENTS];
thread_local std::stack<SampleData_t> WP_Stack;
thread_local std::stack<Context*> retCtxtStack;
thread_local CPUEventStack_t CPUEventStack = {.top = -1};
thread_local CPUEventStack_t CPUEventCntStack = {.top = -1};

void SetupEventFd(int fd) {
    eventFd[curEventId++] = fd;
}

int curWatermarkId = 0;
int sample_cnt_metric_id = -1;
// int pebs_metric_id[MAX_EVENTS];
int start_metric_id[MAX_EVENTS];
int end_metric_id[MAX_EVENTS];
int mean_metric_id[MAX_EVENTS];
int variance_metric_id[MAX_EVENTS];
int m2_metric_id[MAX_EVENTS];
int cv_metric_id[MAX_EVENTS];

void SetupWatermarkMetric(std::string client_name, std::string event_name, int event_threshold) {
    if (curWatermarkId == MAX_EVENTS) {
        ERROR("curWatermarkId == MAX_EVENTS = %d", MAX_EVENTS);
        assert(false);
    }
   
    // pebs_metric_id[curWatermarkId] = metricId;
    if (curWatermarkId == 0) {
        metrics::metric_info_t metric_sample_cnt_info;
        metric_sample_cnt_info.client_name = client_name;
        metric_sample_cnt_info.event_name = event_name;
        metric_sample_cnt_info.event_measure = "COUNT";
        metric_sample_cnt_info.threshold = event_threshold;
        metric_sample_cnt_info.val_type = metrics::METRIC_VAL_INT;
        sample_cnt_metric_id = metrics::MetricInfoManager::registerMetric(metric_sample_cnt_info);
    } else {
        metrics::metric_info_t metric_start_info;
        metric_start_info.client_name = client_name;
        metric_start_info.event_name = event_name;
        metric_start_info.event_measure = "Start";
        metric_start_info.threshold = event_threshold;
        metric_start_info.val_type = metrics::METRIC_VAL_INT;
        start_metric_id[curWatermarkId] = metrics::MetricInfoManager::registerMetric(metric_start_info);

        metrics::metric_info_t metric_end_info;
        metric_end_info.client_name = client_name;
        metric_end_info.event_name = event_name;
        metric_end_info.event_measure = "End";
        metric_end_info.threshold = event_threshold;
        metric_end_info.val_type = metrics::METRIC_VAL_INT;
        end_metric_id[curWatermarkId] = metrics::MetricInfoManager::registerMetric(metric_end_info);

        metrics::metric_info_t metric_mean_info;
        metric_mean_info.client_name = client_name;
        metric_mean_info.event_name = event_name;
        metric_mean_info.event_measure = "MEAN";
        metric_mean_info.threshold = event_threshold;
        metric_mean_info.val_type = metrics::METRIC_VAL_REAL;
        mean_metric_id[curWatermarkId] = metrics::MetricInfoManager::registerMetric(metric_mean_info);
        
        metrics::metric_info_t metric_variance_info;
        metric_variance_info.client_name = client_name;
        metric_variance_info.event_name = event_name;
        metric_variance_info.event_measure = "VARIANCE";
        metric_variance_info.threshold = event_threshold;
        metric_variance_info.val_type = metrics::METRIC_VAL_REAL;
        variance_metric_id[curWatermarkId] = metrics::MetricInfoManager::registerMetric(metric_variance_info);
	
        metrics::metric_info_t metric_m2_info;
        metric_m2_info.client_name = client_name;
        metric_m2_info.event_name = event_name;
        metric_m2_info.event_measure = "M2";
        metric_m2_info.threshold = event_threshold;
        metric_m2_info.val_type = metrics::METRIC_VAL_REAL;
        m2_metric_id[curWatermarkId] = metrics::MetricInfoManager::registerMetric(metric_m2_info);
	
        metrics::metric_info_t metric_cv_info;
        metric_cv_info.client_name = client_name;
        metric_cv_info.event_name = event_name;
        metric_cv_info.event_measure = "CV";
        metric_cv_info.threshold = event_threshold;
        metric_cv_info.val_type = metrics::METRIC_VAL_REAL;
        cv_metric_id[curWatermarkId] = metrics::MetricInfoManager::registerMetric(metric_cv_info);
    }

    curWatermarkId++;
}

void Profiler::OnSample(int eventID, perf_sample_data_t *sampleData, void *uCtxt) {
    void *sampleIP = (void *)(sampleData->ip);
    void *stackAddr = (void **)getContextSP(uCtxt);

    jmethodID method_id = 0;
    uint32_t method_version = 0;
    CodeCacheManager &code_cache_manager = Profiler::getProfiler().getCodeCacheManager();
    
    CompiledMethod *method = code_cache_manager.getMethod(sampleData->ip, method_id, method_version);
    if (method == nullptr){
        return;
    }
    if (!selectedMethod.empty() && method->getMethodName().compare(selectedMethod) != 0) return;
    void *startAddr = nullptr, *endAddr = nullptr;
    decodeInsCounter = 0;
    for (int i = 1; i < curEventId; i++) {
        eventRDCnt[i] = 0;
    }
    while(!retCtxtStack.empty())retCtxtStack.pop();
    while(!WP_Stack.empty())WP_Stack.pop();

    //if (method->getMethodName().compare("put") != 0) return;

#ifdef PRINT_SAMPLED_INS
     std::ofstream *pmu_ins_output_stream = reinterpret_cast<std::ofstream *>(TD_GET(pmu_ins_output_stream));
     assert(pmu_ins_output_stream != nullptr); 
     print_single_instruction(pmu_ins_output_stream, (const void *)sampleIP);
#endif
    for (int i = 1; i < curEventId; i++){
        assert(read(eventFd[i], &eventTempCnt, sizeof(unsigned long)) > 0);
    }
    
    int top = ++CPUEventStack.top;
    ++CPUEventCntStack.top;
    assert(curEventId >= 1);
    //assert(top == 0);
    if(top != 0){
        CPUEventStack.top = 0;
        CPUEventCntStack.top = 0;
        top = 0;
    }
    for (int i = 1; i < curEventId; i++) {
        CPUEventCntStack.value[top][i] = 0;
    }

    ContextFrame *callee_ctxt_frame = new ContextFrame;
    callee_ctxt_frame->binary_addr = (uint64_t)sampleIP;
    callee_ctxt_frame->method_id = method_id;
    callee_ctxt_frame->method_version = method_version;

    SampleData_t sd= {
         .va = stackAddr,
         .watchLen = sizeof(uint64_t),
         .watchType = WP_RW,
         .accessLen = sizeof(uint64_t),
         .calleeCtxtFrame = callee_ctxt_frame
    };
    WP_Stack.push(sd);
    Context *watchCtxt = constructContext(_asgct, uCtxt, (uint64_t)sampleIP, method_id, method_version, callee_ctxt_frame);
    retCtxtStack.push(watchCtxt);
    decoder(sampleIP, stackAddr, uCtxt);

}

void Profiler::decoder(void *sampleIP, void *stackAddr, void *uCtxt){
    xed_decoded_inst_t xedd;
    void *TempIp = sampleIP;
    auto BBinfo = basic_block_map.find(sampleIP);
    while(true) {
        if(decodeInsCounter >= decodeThreshold){
            SampleData_t sd = WP_Stack.top();
            if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
            WP_Stack.pop();
            return;
        }
        if(BBinfo != basic_block_map.end() && BBinfo->second.WP_IP != (void *)-1){
            xedd = BBinfo->second.WP_xedd;
            TempIp = BBinfo->second.WP_IP;
        }
        else{
            if (XED_ERROR_NONE != get_decode_xedd(&xedd, TempIp)) {
                assert(0);
                return;
            }
            if(TempIp == sampleIP){
                basic_block_t BBinsert = {
                        .Entry_xedd = xedd,
                        .WP_xedd = xedd,
                        .WP_IP = (void *)-1
                };
                basic_block_map.insert({sampleIP, BBinsert});
            }
        }
        xed_iclass_enum_t iclass = xed_decoded_inst_get_iclass(&xedd);
        int inst_length = xed_decoded_inst_get_length(&xedd);
#ifdef PRINT_INS
        std::ofstream *pmu_ins_output_stream = reinterpret_cast<std::ofstream *>(TD_GET(pmu_ins_output_stream));
	    *pmu_ins_output_stream << "ip:" << TempIp << ", iclass:" << xed_iclass_enum_t2str(iclass) << ", len:" << inst_length << std::endl;
#endif
        const xed_operand_t *op0 =  xed_inst_operand(xed_decoded_inst_inst(&xedd), 0);
        xed_operand_enum_t   op0_name = xed_operand_name(op0);
        switch (iclass) {
            case XED_ICLASS_RET_NEAR:
            case XED_ICLASS_RET_FAR:{
                if(BBinfo != basic_block_map.end() && BBinfo->second.WP_IP == (void *)-1){
                    BBinfo->second.WP_xedd = xedd;
                    BBinfo->second.WP_IP = TempIp;
                }
                if(WP_Stack.size() == 1){
                    SampleData_t sd = WP_Stack.top();
                    WP_Stack.pop();
                    retCtxtStack.pop();
                    if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                    return;
                }
                ContextFrame *callee_ctxt_frame = (ContextFrame *)WP_Stack.top().calleeCtxtFrame;

                SampleData_t sd = {
                        .va = TempIp,
                        .watchLen = sizeof(long),
                        .watchType = WP_EXEC,
                        .accessLen = sizeof(long),
                        .calleeCtxtFrame = callee_ctxt_frame
                };

                WP_Stack.pop();
                if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                return;
            }
            case XED_ICLASS_JB:
            case XED_ICLASS_JBE:
            case XED_ICLASS_JL:
            case XED_ICLASS_JLE:
            case XED_ICLASS_JNB:
            case XED_ICLASS_JNBE:
            case XED_ICLASS_JNL:
            case XED_ICLASS_JNLE:
            case XED_ICLASS_JNO:
            case XED_ICLASS_JNP:
            case XED_ICLASS_JNS:
            case XED_ICLASS_JNZ:
            case XED_ICLASS_JO:
            case XED_ICLASS_JP:
            case XED_ICLASS_JRCXZ:
            case XED_ICLASS_JS:
            case XED_ICLASS_JZ:{
                if(BBinfo != basic_block_map.end() && BBinfo->second.WP_IP == (void *)-1){
                    BBinfo->second.WP_xedd = xedd;
                    BBinfo->second.WP_IP = TempIp;
                }

                ContextFrame *callee_ctxt_frame = new ContextFrame;
                callee_ctxt_frame->binary_addr = (uint64_t)sampleIP;
                callee_ctxt_frame->method_id = (jmethodID)-2;
                SampleData_t sd= {
                        .va = TempIp,
                        .watchLen = sizeof(long),
                        .watchType = WP_EXEC,
                        .accessLen = sizeof(long),
                        .calleeCtxtFrame = callee_ctxt_frame
                };

                if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                return;
            }

            case XED_ICLASS_LOOP:
            case XED_ICLASS_LOOPE:
            case XED_ICLASS_LOOPNE:{
                if(BBinfo != basic_block_map.end() && BBinfo->second.WP_IP == (void *)-1){
                    BBinfo->second.WP_xedd = xedd;
                    BBinfo->second.WP_IP = TempIp;
                }
                ContextFrame *callee_ctxt_frame = new ContextFrame;
                callee_ctxt_frame->binary_addr = (uint64_t)sampleIP;
                callee_ctxt_frame->method_id = (jmethodID)-2;
                SampleData_t sd= {
                        .va = TempIp,
                        .watchLen = sizeof(long),
                        .watchType = WP_EXEC,
                        .accessLen = sizeof(long),
                        .calleeCtxtFrame = callee_ctxt_frame
                };
                if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                return;

            }

            case XED_ICLASS_JMP:
            case XED_ICLASS_JMP_FAR:{
                if (op0_name == XED_OPERAND_RELBR) {
                    xed_operand_values_t *vals = xed_decoded_inst_operands(&xedd);
                    if (xed_operand_values_has_branch_displacement(vals)) {
                        TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,uCtxt);
                    }
                    break;
                }else{
                    if(BBinfo != basic_block_map.end() && BBinfo->second.WP_IP == (void *)-1){
                        BBinfo->second.WP_xedd = xedd;
                        BBinfo->second.WP_IP = TempIp;
                    }
                    ContextFrame *callee_ctxt_frame = new ContextFrame;
                    callee_ctxt_frame->binary_addr = (uint64_t)sampleIP;
                    callee_ctxt_frame->method_id = (jmethodID)-2;
                    SampleData_t sd= {
                            .va = TempIp,
                            .watchLen = sizeof(long),
                            .watchType = WP_EXEC,
                            .accessLen = sizeof(long),
                            .calleeCtxtFrame = callee_ctxt_frame
                    };
                    if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                    return;
                }
            }
            case XED_ICLASS_CALL_FAR:
            case XED_ICLASS_CALL_NEAR:{
                decodeInsCounter++;
                if(BBinfo != basic_block_map.end() && BBinfo->second.WP_IP == (void *)-1){
                    BBinfo->second.WP_xedd = xedd;
                    BBinfo->second.WP_IP = TempIp;
                }
                ContextFrame *callee_ctxt_frame = new ContextFrame;
                callee_ctxt_frame->binary_addr = (uint64_t)sampleIP;
                SampleData_t sd= {
                        .va = TempIp,
                        .watchLen = sizeof(long),
                        .watchType = WP_EXEC,
                        .accessLen = sizeof(long),
                        .calleeCtxtFrame = callee_ctxt_frame
                };
                if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                return;

            }
            default:
                TempIp = (void*)((char *)TempIp + inst_length);
        }
    }
    return;

}

void* Profiler::readReg(xed_decoded_inst_t* xedd, xed_operand_enum_t op_name, void *uCtxt){
    xed_reg_enum_t reg;
if(op_name == XED_OPERAND_INVALID){
        reg = xed_decoded_inst_get_base_reg(xedd, 0);
    }
    else{
        reg = xed_decoded_inst_get_reg(xedd, op_name);
    }
    switch(reg){
        case XED_REG_RCX:{
            return getContextRCX(uCtxt);
        }
        case XED_REG_RBX:{
            return getContextRBX(uCtxt);
        }
        case XED_REG_R10:{
            return getContextR10(uCtxt);
        }
        case XED_REG_RAX:{
            return getContextRAX(uCtxt);
        }
        case XED_REG_RDX:{
            return getContextRDX(uCtxt);
        }
        case XED_REG_R8:{
            return getContextR8(uCtxt);
        }
        case XED_REG_R9:{
            return getContextR9(uCtxt);
        }
        case XED_REG_R11:{
            return getContextR11(uCtxt);
        }
        case XED_REG_R12:{
            return getContextR12(uCtxt);
        }
        case XED_REG_R13:{
            return getContextR13(uCtxt);
        }
        case XED_REG_R14:{
            return getContextR14(uCtxt);
        }
        case XED_REG_R15:{
            return getContextR15(uCtxt);
        }
        case XED_REG_RSI:{
            return getContextRSI(uCtxt);
        }
        case XED_REG_RDI:{
            return getContextRDI(uCtxt);
        }
        case XED_REG_RBP:{
            return getContextRDI(uCtxt);
        }
        default:{
            std::cerr << "xed_decoded_inst_get_reg: " << xed_reg_enum_t2str(reg) << std::endl;
            assert(0);
        }
    }
    return 0;
}

void* Profiler::get_branch_target(xed_operand_enum_t op0_name, void* TempIp, xed_decoded_inst_t* xedd, int inst_length, void *uCtxt){
    if (op0_name == XED_OPERAND_RELBR) {// || op0_name == XED_OPERAND_PTR || op0_name == XED_OPERAND_ABSBR ) {
        xed_int64_t disp = xed_decoded_inst_get_branch_displacement(xedd);
        char* insn_end = (char*)TempIp + inst_length;
        TempIp = (void *) (insn_end + disp);
    }
    else if(xed_operand_is_register(op0_name)){
        TempIp = readReg(xedd, op0_name, uCtxt);
    }
    else if(op0_name == XED_OPERAND_MEM0){
        xed_uint64_t out_address;
        xed_error_enum_t debug_temp = xed_agen(xedd, 0, uCtxt, &out_address);
        TempIp = (void*)*(uint64_t*)out_address;
    }
    else{
        assert(0);
    }
    return TempIp;

}

bool Profiler::OnBranchWatchPoint(WP_TriggerInfo_t *wpt){
    void* TempIp = wpt->sd->va;
    xed_decoded_inst_t xedd ;

    if (XED_ERROR_NONE != get_decode_xedd(&xedd, TempIp)) {
        std::cerr << "get_decode_xedd failed" << std::endl;
        return true;
    }

    xed_iclass_enum_t iclass = xed_decoded_inst_get_iclass(&xedd);
    int inst_length = xed_decoded_inst_get_length(&xedd);
    const xed_operand_t *op0 = xed_inst_operand(xed_decoded_inst_inst(&xedd), 0);
    xed_operand_enum_t op0_name = xed_operand_name(op0);
    xed_flag_set_t const *read_set;
    if (xed_decoded_inst_uses_rflags(&xedd)) {
        xed_simple_flag_t const *rfi = xed_decoded_inst_get_rflags_info(&xedd);
        read_set = xed_simple_flag_get_read_flag_set(rfi);
    }
    void *stackAddr = (void **)getContextSP(wpt->uCtxt);
    int64_t flag_set = (int64_t)getContextFL(wpt->uCtxt);
    int64_t rcx_value = (int64_t)getContextRCX(wpt->uCtxt);
    int64_t rax_value = (int64_t)getContextRAX(wpt->uCtxt);
    int64_t r10_value = (int64_t)getContextR10(wpt->uCtxt);
    ASGCT_CallFrame frames[MAX_FRAME_NUM];
    ASGCT_CallTrace trace;
    trace.frames = frames;
    trace.env_id = JVM::jni();
    switch (iclass) {
        case XED_ICLASS_RET_FAR:
        case XED_ICLASS_RET_NEAR:{
            int top = CPUEventStack.top--;
            --CPUEventCntStack.top;
            if (wpt->pc == 0) wpt->pc = getContextPC(wpt->uCtxt);

            jmethodID method_id = 0;
            uint32_t method_version = 0;
            CodeCacheManager &code_cache_manager = Profiler::getProfiler().getCodeCacheManager();

            CompiledMethod *method = code_cache_manager.getMethod((uint64_t)(wpt->pc), method_id, method_version);

            prevIP = wpt->pc;
            
            ContextFrame *calleeCtxtFrame = (ContextFrame *)wpt->sd->calleeCtxtFrame;
            Context *trapCtxt = constructContext(_asgct, wpt->uCtxt, (uint64_t)wpt->pc, method_id, method_version, calleeCtxtFrame);
            assert(trapCtxt != nullptr);
            delete calleeCtxtFrame;

            assert(trapCtxt != nullptr);

            metrics::ContextMetrics *metrics = trapCtxt->getMetrics();
            if (metrics == nullptr) {
                metrics = new metrics::ContextMetrics();
                trapCtxt->setMetrics(metrics);
            }
            metrics::metric_val_t metric_val;
            metric_val.i = 1;
            assert(metrics->increment(sample_cnt_metric_id, metric_val));
            uint64_t sample_cnt = metrics->getMetricVal(sample_cnt_metric_id)->i;
            assert(curEventId == curWatermarkId);
            std::string start_str = "";
            std::string end_str = "";
            for (int i = 1; i < curWatermarkId; i++) {
                CPUEventCntStack.value[top][i] += eventRDCnt[i];
                if(top != 0){
                    CPUEventCntStack.value[top-1][i] += eventRDCnt[i];
                }
                eventRDCnt[i] = 0;
                uint64_t new_value = CPUEventCntStack.value[top][i];
                uint64_t start = CPUEventStack.value[top][i];
                uint64_t end = start + new_value;
                if(top == 0){
                    CPUEventStack.value[top][i] = end;
                }
                double mean = metrics->getMetricVal(mean_metric_id[i])->r;
                double variance = metrics->getMetricVal(variance_metric_id[i])->r;
                double m2 = metrics->getMetricVal(m2_metric_id[i])->r;
                UpdateVarianceAndMean(sample_cnt, new_value, &mean, &variance, &m2);
                double cv = sqrt(variance) / mean;
                std::string start_str = metrics->getMetricVal(start_metric_id[i])->str;//std::bad_alloc
                std::string end_str = metrics->getMetricVal(end_metric_id[i])->str;
                start_str += std::to_string(start) + " ";//SF
                end_str += std::to_string(end) + " ";
                metrics::metric_val_t metric_val;
                metric_val.str = start_str;
                assert(metrics->setMetricVal(start_metric_id[i], metric_val));
                metric_val.str = end_str;
                assert(metrics->setMetricVal(end_metric_id[i], metric_val));
                metric_val.str = "";
                metric_val.r = mean;
                assert(metrics->setMetricVal(mean_metric_id[i], metric_val));
                metric_val.r = variance;
                assert(metrics->setMetricVal(variance_metric_id[i], metric_val));
                metric_val.r = m2;
                assert(metrics->setMetricVal(m2_metric_id[i], metric_val));
                metric_val.r = cv;
                assert(metrics->setMetricVal(cv_metric_id[i], metric_val));

            }
            retCtxtStack.pop();

            if(WP_Stack.empty()) {
                return true;
            }
            decoder((void *)*(uint64_t *)stackAddr,stackAddr,wpt->uCtxt);
            return true;
        }

        case XED_ICLASS_JB: {
            if (flag_set & 0x0001) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JBE: {
            if (flag_set & 0x0041) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JL: {
            if (!(flag_set & 0x0800) != !(flag_set & 0x0080)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JLE: {
            if ((!(flag_set & 0x0800) != !(flag_set & 0x0080)) || (flag_set & 0x0040)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNB: {
            if (!(flag_set & 0x0001)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNBE: {
            if (!(flag_set & 0x0041)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNL: {
            if (!(flag_set & 0x0800) == !(flag_set & 0x0080)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNLE: {
            if ((!(flag_set & 0x0800) == !(flag_set & 0x0080)) && (!(flag_set & 0x0040))) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNO: {
            if (!(flag_set & 0x0800)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNP: {
            if (!(flag_set & 0x0004)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNS: {
            if (!(flag_set & 0x0080)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JNZ:{
            if (!(flag_set & 0x0040)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JO: {
            if (flag_set & 0x0800) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JP: {
            if (flag_set & 0x0004) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JRCXZ: {
            if(!rcx_value){
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JS: {
            if (flag_set & 0x0080) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JZ: {
            if (flag_set & 0x0040) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }

        case XED_ICLASS_LOOP: {
            if(rcx_value > 1) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_LOOPE: {
            if(rcx_value > 1 && (flag_set & 0x0040)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_LOOPNE: {
            if(rcx_value > 1 && !(flag_set & 0x0040)) {
                TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            } else {
                TempIp = (void *) ((char *) TempIp + inst_length);
                decoder(TempIp,stackAddr,wpt->uCtxt);
            }
            break;
        }
        case XED_ICLASS_JMP:
        case XED_ICLASS_JMP_FAR: {
            TempIp = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
            decoder(TempIp,stackAddr,wpt->uCtxt);
            break;
        }
        case XED_ICLASS_CALL_FAR:
        case XED_ICLASS_CALL_NEAR: {
            //For Runtime Call only
            void * RETtoIp = (void *) ((char *) TempIp + inst_length);
            jmethodID caller_method_id = 0;
            uint32_t caller_method_version = 0;
            CodeCacheManager &caller_code_cache_manager = Profiler::getProfiler().getCodeCacheManager();
            CompiledMethod *caller_method = caller_code_cache_manager.getMethod((uint64_t)TempIp, caller_method_id, caller_method_version);
            //
            void* CalleeIP = get_branch_target(op0_name, TempIp, &xedd, inst_length,wpt->uCtxt);
            jmethodID callee_method_id = 0;
            uint32_t callee_method_version = 0;
            CodeCacheManager &code_cache_manager = Profiler::getProfiler().getCodeCacheManager();
            CompiledMethod *method = code_cache_manager.getMethod((uint64_t)CalleeIP, callee_method_id, callee_method_version);

            if (method == nullptr) {
                assert(curEventId >= 1);
                for (int i = 1; i < curEventId; i++) {
                    RuntimeCallCount[i] = eventRDCnt[i] = 0;
                }
                ContextFrame *callee_ctxt_frame = new ContextFrame;
                callee_ctxt_frame->binary_addr = (uint64_t)-3;
                callee_ctxt_frame->method_id = (jmethodID)-3;
                callee_ctxt_frame->method_version = (uint32_t)-3;
                SampleData_t sd= {
                        .va = RETtoIp,
                        .watchLen = sizeof(long),
                        .watchType = WP_EXEC,
                        .accessLen = sizeof(long),
                        .calleeCtxtFrame = callee_ctxt_frame
                };
                if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/)){}
                return true;
            }

            int top = ++CPUEventStack.top;
            ++CPUEventCntStack.top;
            assert(curEventId >= 1);
            for (int i = 1; i < curEventId; i++) {
                CPUEventCntStack.value[top - 1][i] += eventRDCnt[i];
                eventRDCnt[i] = 0;
            }
            for (int i = 1; i < curEventId; i++) {
                CPUEventStack.value[top][i] = CPUEventStack.value[top - 1][i] + CPUEventCntStack.value[top - 1][i];
                CPUEventCntStack.value[top][i] = 0;
            }
            ContextFrame *callee_ctxt_frame = new ContextFrame;
            callee_ctxt_frame->binary_addr = (uint64_t)CalleeIP;
            callee_ctxt_frame->method_id = callee_method_id;
            callee_ctxt_frame->method_version = callee_method_version;

            Context *trapCtxt = constructContext(_asgct, wpt->uCtxt, (uint64_t)CalleeIP, callee_method_id, callee_method_version, callee_ctxt_frame);
            uint64_t * newStackAddr = (uint64_t *)stackAddr;
            SampleData_t sd= {
                    .va = (void*)--newStackAddr,
                    .watchLen = sizeof(uint64_t),
                    .watchType = WP_RW,
                    .accessLen = sizeof(uint64_t),
                    .calleeCtxtFrame = callee_ctxt_frame
            };
            WP_Stack.push(sd);
            retCtxtStack.push(trapCtxt);
            decoder(CalleeIP,stackAddr,wpt->uCtxt);
            break;
        }
        default:
            break;
    }
    return true;
}


WP_TriggerAction_t Profiler::OnRetWatchPoint(WP_TriggerInfo_t *wpt) {
    if (!profiler_safe_enter()) return WP_DISABLE;
    if (wpt->pc == 0) wpt->pc = getContextPC(wpt->uCtxt);
    if (wpt->pc == 0) {//todo:need check
        CPUEventStack.top = -1;
        CPUEventCntStack.top = -1;
        profiler_safe_exit();
        return WP_DISABLE;
    }

    jmethodID method_id = 0;
    uint32_t method_version = 0;
    CodeCacheManager &code_cache_manager = Profiler::getProfiler().getCodeCacheManager();

    CompiledMethod *method = code_cache_manager.getMethod((uint64_t)(wpt->pc), method_id, method_version);
    if(method == nullptr) {
        --CPUEventStack.top;
        --CPUEventCntStack.top;
        profiler_safe_exit();
        return WP_DISABLE;
    }

    for (int i = 1; i < curWatermarkId; i++) {
        uint64_t debugOutput;
        assert(read(eventFd[i], &debugOutput, sizeof(uint64_t)) > 0);
    }
    // For run time call
    if(((ContextFrame *)wpt->sd->calleeCtxtFrame)->binary_addr == (uint64_t)-3){
        void * stackAddr;
        for (int i = 1; i < curWatermarkId; i++) {
            RuntimeCallTime[i] += eventRDCnt[i] - RuntimeCallCount[i];
            eventRDCnt[i] = RuntimeCallCount[i];
        }
        decoder(wpt->va,stackAddr,wpt->uCtxt);
        profiler_safe_exit();
        return WP_TRIGGERED;
    }
    //For over Threshold
    if(decodeInsCounter >= decodeThreshold){//
        if(retCtxtStack.empty()){
            assert(0);
        }
        if(!WP_Stack.empty()){
            SampleData_t sd = WP_Stack.top();
            if(WP_Subscribe(&sd, false /* capture value */, false /* Function Varianc*/))
            WP_Stack.pop();
        }
        int top = CPUEventStack.top--;
        --CPUEventCntStack.top;
        if (wpt->pc == 0) wpt->pc = getContextPC(wpt->uCtxt);
        void *patchedIP = wpt->pc;
        if (!wpt->pcPrecise) {
            void *startAddr = nullptr, *endAddr = nullptr;
            method->getMethodBoundary(&startAddr, &endAddr);
            if (prevIP > startAddr && prevIP < patchedIP)
                patchedIP = GetPatchedIP(prevIP, endAddr, wpt->pc);
            else
                patchedIP = GetPatchedIP(startAddr, endAddr, wpt->pc);
            if (!IsPCSane(wpt->pc, patchedIP)) {
                profiler_safe_exit();
                return WP_DISABLE;
            }
            wpt->pc = patchedIP;
            prevIP = patchedIP;
        }

        ContextFrame *calleeCtxtFrame = (ContextFrame *)wpt->sd->calleeCtxtFrame;
        Context *trapCtxt = constructContext(_asgct, wpt->uCtxt, (uint64_t)wpt->pc, method_id, method_version, calleeCtxtFrame);
        assert(trapCtxt != nullptr);

        metrics::ContextMetrics *metrics = trapCtxt->getMetrics();
        if (metrics == nullptr) {
            metrics = new metrics::ContextMetrics();
            trapCtxt->setMetrics(metrics);
        }
        metrics::metric_val_t metric_val;
        metric_val.i = 1;
        assert(metrics->increment(sample_cnt_metric_id, metric_val));
        uint64_t sample_cnt = metrics->getMetricVal(sample_cnt_metric_id)->i;
        assert(curEventId == curWatermarkId);

        for (int i = 1; i < curWatermarkId; i++) {
            CPUEventCntStack.value[top][i] += eventRDCnt[i];
            if(top != 0) {
                CPUEventCntStack.value[top - 1][i] += eventRDCnt[i];
            }
            eventRDCnt[i] = 0;
            uint64_t new_value = CPUEventCntStack.value[top][i];
            uint64_t start = CPUEventStack.value[top][i];
            uint64_t end = start + new_value;
            if(top == 0){
                CPUEventStack.value[top][i] = end;
            }
            double mean = metrics->getMetricVal(mean_metric_id[i])->r;
            double variance = metrics->getMetricVal(variance_metric_id[i])->r;
            double m2 = metrics->getMetricVal(m2_metric_id[i])->r;
            UpdateVarianceAndMean(sample_cnt, new_value, &mean, &variance, &m2);
            double cv = sqrt(variance) / mean;

            std::string start_str = metrics->getMetricVal(start_metric_id[i])->str;
            std::string end_str = metrics->getMetricVal(end_metric_id[i])->str;
            start_str += std::to_string(start) + " ";
            end_str += std::to_string(end) + " ";
            metrics::metric_val_t metric_val;
            metric_val.str = start_str;
            assert(metrics->setMetricVal(start_metric_id[i], metric_val));
            metric_val.str = end_str;
            assert(metrics->setMetricVal(end_metric_id[i], metric_val));
            metric_val.str = "";
            metric_val.r = mean;
            assert(metrics->setMetricVal(mean_metric_id[i], metric_val));
            metric_val.r = variance;
            assert(metrics->setMetricVal(variance_metric_id[i], metric_val));
            metric_val.r = m2;
            assert(metrics->setMetricVal(m2_metric_id[i], metric_val));
            metric_val.r = cv;
            assert(metrics->setMetricVal(cv_metric_id[i], metric_val));
        }
        retCtxtStack.pop();
        profiler_safe_exit();
        return WP_TRIGGERED;
    }
    // OnBranchWatchPoint
    if(wpt->sd->watchType == WP_EXEC && OnBranchWatchPoint(wpt)){
        profiler_safe_exit();
        return WP_TRIGGERED;
    }

    int top = CPUEventStack.top--;
    --CPUEventCntStack.top;


     // fix the imprecise IP 
     void *patchedIP = wpt->pc;
     if (!wpt->pcPrecise) {
         void *startAddr = nullptr, *endAddr = nullptr; 
         method->getMethodBoundary(&startAddr, &endAddr);
         if (prevIP > startAddr && prevIP < patchedIP) 
             patchedIP = GetPatchedIP(prevIP, endAddr, wpt->pc);
         else
             patchedIP = GetPatchedIP(startAddr, endAddr, wpt->pc);
         if (!IsPCSane(wpt->pc, patchedIP)) {
             profiler_safe_exit();
             return WP_DISABLE;
         }
         wpt->pc = patchedIP;
         prevIP = patchedIP;
     }

    ContextFrame *calleeCtxtFrame = (ContextFrame *)wpt->sd->calleeCtxtFrame;
    Context *trapCtxt = constructContext(_asgct, wpt->uCtxt, (uint64_t)wpt->pc, method_id, method_version, calleeCtxtFrame);
    assert(trapCtxt != nullptr);
    delete calleeCtxtFrame;
    
    metrics::ContextMetrics *metrics = trapCtxt->getMetrics();
    if (metrics == nullptr) {
        metrics = new metrics::ContextMetrics();
        trapCtxt->setMetrics(metrics); 
    }
    metrics::metric_val_t metric_val;
    metric_val.i = 1;
    assert(metrics->increment(sample_cnt_metric_id, metric_val));
    uint64_t sample_cnt = metrics->getMetricVal(sample_cnt_metric_id)->i;
   
#if defined(PRINT_TRAPPED_INS) || defined(PLOT_VARIANCE)
    std::ofstream *pmu_ins_output_stream = reinterpret_cast<std::ofstream *>(TD_GET(pmu_ins_output_stream));
#endif
    assert(curEventId == curWatermarkId);
    for (int i = 1; i < curWatermarkId; i++) {
        CPUEventCntStack.value[top][i] += eventRDCnt[i];
        if(top != 0){
            assert(0);
        }
        uint64_t new_value = CPUEventCntStack.value[top][i];
        uint64_t start = CPUEventStack.value[top][i];
        uint64_t end = start + new_value;
        CPUEventStack.value[top][i] = end;
    	double mean = metrics->getMetricVal(mean_metric_id[i])->r;
    	double variance = metrics->getMetricVal(variance_metric_id[i])->r; 
    	double m2 = metrics->getMetricVal(m2_metric_id[i])->r; 
    	UpdateVarianceAndMean(sample_cnt, new_value, &mean, &variance, &m2); 
    	double cv = sqrt(variance) / mean;
        std::string start_str = metrics->getMetricVal(start_metric_id[i])->str;
        std::string end_str = metrics->getMetricVal(end_metric_id[i])->str;

        start_str += std::to_string(start) + " ";
        end_str += std::to_string(end) + " ";
    	metrics::metric_val_t metric_val;
        metric_val.str = start_str;
        assert(metrics->setMetricVal(start_metric_id[i], metric_val));
        metric_val.str = end_str;
        assert(metrics->setMetricVal(end_metric_id[i], metric_val));
        metric_val.str = "";
    	metric_val.r = mean;
    	assert(metrics->setMetricVal(mean_metric_id[i], metric_val));
    	metric_val.r = variance;
    	assert(metrics->setMetricVal(variance_metric_id[i], metric_val));
    	metric_val.r = m2;
    	assert(metrics->setMetricVal(m2_metric_id[i], metric_val));
    	metric_val.r = cv;
    	assert(metrics->setMetricVal(cv_metric_id[i], metric_val));

#ifdef PLOT_VARIANCE
//    	 if (method->getMethodName().compare("force") == 0 && decodeThreshold == 0)
	    *pmu_ins_output_stream << CPUEventCntStack.value[top][i] << " ";
#endif
    }    
#ifdef PLOT_VARIANCE
//    if (decodeThreshold == 0)
    *pmu_ins_output_stream << std::endl;
#endif

    
#ifdef PRINT_TRAPPED_INS
    std::ofstream *pmu_ins_output_stream = reinterpret_cast<std::ofstream *>(TD_GET(pmu_ins_output_stream));
    assert(pmu_ins_output_stream != nullptr); 
    print_single_instruction(pmu_ins_output_stream, wpt->pc);
#endif
    //linux_perf_events_reset();
    profiler_safe_exit();
    return WP_DISABLE;
}


void Profiler::GenericAnalysis(perf_sample_data_t *sampleData, void *uCtxt, jmethodID method_id, uint32_t method_version, uint32_t threshold, int metric_id2) {
    /*Context *ctxt_access = constructContext(_asgct, uCtxt, sampleData->ip, nullptr, method_id, method_version);
    if (ctxt_access != nullptr && sampleData->ip != 0) {
	metrics::ContextMetrics *metrics = ctxt_access->getMetrics();
	if (metrics == nullptr) {
	    metrics = new metrics::ContextMetrics();
	    ctxt_access->setMetrics(metrics);
	}
	metrics::metric_val_t metric_val;
	metric_val.i = 1;
	assert(metrics->increment(metric_id2, metric_val));
        totalGenericCounter += 1;
    }*/
}

Profiler::Profiler() { 
    _asgct = (ASGCT_FN)dlsym(RTLD_DEFAULT, "AsyncGetCallTrace"); 
    assert(_asgct);
}


void Profiler::init() {

std::fill_n (mean_metric_id, MAX_EVENTS, -1);	
std::fill_n (variance_metric_id, MAX_EVENTS, -1);	
std::fill_n (m2_metric_id, MAX_EVENTS, -1);	
std::fill_n (cv_metric_id, MAX_EVENTS, -1);
std::fill_n (RuntimeCallTime, MAX_EVENTS, 0);

#ifndef COUNT_OVERHEAD
    _method_file.open("agent-trace-method.run");
    _method_file << XML_FILE_HEADER << std::endl;
#endif

    _statistics_file.open("agent-statistics.run");
    ThreadData::thread_data_init();
    
    assert(PerfManager::processInit(JVM::getArgument()->getPerfEventList(), Profiler::OnSample));
    assert(WP_Init());
    assert(WP_SetPerfPauseAndResumeFunctions(PerfManager::perf_stop_all_wrapper, PerfManager::perf_start_all_wrapper));
    // std::string client_name = GetClientName();
    // std::transform(client_name.begin(), client_name.end(), std::back_inserter(clientName), ::toupper);
}


void Profiler::shutdown() {
    WP_Shutdown();
    PerfManager::processShutdown();
    ThreadData::thread_data_shutdown();
    output_statistics(); 
    _statistics_file.close();
    _method_file.close();
}

void Profiler::IncrementGCCouter() {
    __sync_fetch_and_add(&GCCounter, 1);    
}

void Profiler::threadStart() { 
    std::fill_n (eventFd, MAX_EVENTS, -1);	
    // totalGenericCounter = 0;

    ThreadData::thread_data_alloc();
    ContextTree *ct_tree = new(std::nothrow) ContextTree();
    assert(ct_tree);
    TD_GET(context_state) = reinterpret_cast<void *>(ct_tree);
  
    // settup the output
    {
#ifndef COUNT_OVERHEAD
        char name_buffer[128];
        snprintf(name_buffer, 128, "agent-trace-%u.run", TD_GET(tid));
        OUTPUT *output_stream = new(std::nothrow) OUTPUT();
        assert(output_stream);
        output_stream->setFileName(name_buffer);
        output_stream->writef("%s\n", XML_FILE_HEADER);
        TD_GET(output_state) = reinterpret_cast<void *> (output_stream);
#endif
#if defined(PRINT_SAMPLED_INS) || defined(PRINT_TRAPPED_INS) || defined(PLOT_VARIANCE) || defined(PRINT_INS)
        std::ofstream *pmu_ins_output_stream = new(std::nothrow) std::ofstream();
        char file_name[128];
        snprintf(file_name, 128, "pmu-instruction-%u", TD_GET(tid));
        pmu_ins_output_stream->open(file_name, std::ios::app); 
        TD_GET(pmu_ins_output_stream) = reinterpret_cast<void *>(pmu_ins_output_stream);
#endif
    }
    // if (clientName.compare(VARIANCE_CLIENT_NAME) == 0) assert(WP_ThreadInit(Profiler::OnRetWatchPoint));
    assert(WP_ThreadInit(Profiler::OnRetWatchPoint));
    /*else if (clientName.compare(GENERIC) != 0) { 
        ERROR("Can't decode client %s", clientName.c_str());
        assert(false);
    }*/
    PopulateBlackListAddresses();
    // assert(WP_SetPerfPauseAndResumeFunctions(PerfManager::perf_start_all, PerfManager::perf_start_all));
    PerfManager::setupEvents();
}


void Profiler::threadEnd() {
    PerfManager::closeEvents();
    WP_ThreadTerminate();
    // if (clientName.compare(GENERIC) != 0) {
    //	 WP_ThreadTerminate();
    // }
    ContextTree *ctxt_tree = reinterpret_cast<ContextTree *>(TD_GET(context_state));
        
#ifndef COUNT_OVERHEAD    
    OUTPUT *output_stream = reinterpret_cast<OUTPUT *>(TD_GET(output_state));
    std::unordered_set<Context *> dump_ctxt = {};
    
    if (ctxt_tree != nullptr) {
        for (auto elem : (*ctxt_tree)) {
            Context *ctxt_ptr = elem;

	    jmethodID method_id = ctxt_ptr->getFrame().method_id;
            _code_cache_manager.checkAndMoveMethodToUncompiledSet(method_id);
    
            if (ctxt_ptr->getMetrics() != nullptr && dump_ctxt.find(ctxt_ptr) == dump_ctxt.end()) { // leaf node of the redundancy pair
                dump_ctxt.insert(ctxt_ptr);
                xml::XMLObj *obj;
                obj = xml::createXMLObj(ctxt_ptr);
                if (obj != nullptr) {
                    output_stream->writef("%s", obj->getXMLStr().c_str());
                    delete obj;
                } else continue;
        
                ctxt_ptr = ctxt_ptr->getParent();
                while (ctxt_ptr != nullptr && dump_ctxt.find(ctxt_ptr) == dump_ctxt.end()) {
                    dump_ctxt.insert(ctxt_ptr);
                    obj = xml::createXMLObj(ctxt_ptr);
                    if (obj != nullptr) {
                        output_stream->writef("%s", obj->getXMLStr().c_str());
                        delete obj;
                    }
                    ctxt_ptr = ctxt_ptr->getParent();
                }
            }
        }
    }
    
    //clean up the output stream
    delete output_stream;
    TD_GET(output_state) = nullptr;
#endif
    
    //clean up the context state
    delete ctxt_tree;
    TD_GET(context_state) = nullptr;
    
#if defined(PRINT_SAMPLED_INS) || defined(PRINT_TRAPPED_INS) || defined(PLOT_VARIANCE) || defined(PRINT_INS)
    std::ofstream *pmu_ins_output_stream = reinterpret_cast<std::ofstream *>(TD_GET(pmu_ins_output_stream));
    pmu_ins_output_stream->close();
    delete pmu_ins_output_stream;
    TD_GET(pmu_ins_output_stream) = nullptr;
#endif    
    /*
    // clear up context-sample tables 
    for (int i = 0; i < MAX_EVENTS; i++) {
        std::unordered_map<Context *, SampleNum> *ctxtSampleTable = reinterpret_cast<std::unordered_map<Context *, SampleNum> *> (TD_GET(ctxt_sample_state)[i]);
        if (ctxtSampleTable != nullptr) {
            delete ctxtSampleTable;
            TD_GET(ctxt_sample_state)[i] = nullptr;
        }
    }
    */
    ThreadData::thread_data_dealloc();

    __sync_fetch_and_add(&grandTotGenericCounter, totalGenericCounter);
}


int Profiler::output_method(const char *buf) {
  _method_file << buf;
  return 0;
}


void Profiler::output_statistics() {
    
    if (clientName.compare(GENERIC) == 0) {
        _statistics_file << clientName << std::endl;
        _statistics_file << grandTotGenericCounter << std::endl;
    }
}
