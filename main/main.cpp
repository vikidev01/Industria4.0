#include <iostream>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <mutex>
#include "version.h"
#include "daemon.h"
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include "rtsp_demo.h"
#include "video.h"
#include "cvi_sys.h"
#include "cvi_comm_video.h"
#include "model_detector.h"  
#include "connect.h"    
#include "hv/hlog.h"
#include "global_cfg.h"
#include "config.h"

using json = nlohmann::json;
static ma::Model* g_model = nullptr;
static std::mutex g_det_mutex;
connectivity_mode_t connectivity_mode = CONNECTIVITY_MODE_MQTT;  

static std::chrono::steady_clock::time_point g_last_object_alert = std::chrono::steady_clock::now();
static std::chrono::steady_clock::time_point g_last_zone_alert   = std::chrono::steady_clock::now();
static std::chrono::steady_clock::time_point g_last_person_report= std::chrono::steady_clock::now();

static CVI_VOID app_ipcam_ExitSig_handle(CVI_S32 signo) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if ((SIGINT == signo) || (SIGTERM == signo)) {
        deinitVideo();
        deinitRtsp();

    }
    exit(-1);
}

static int fpRunYolo_CH0(void* pData, void* pArgs, void* pUserData) {
    if (!g_model) return CVI_FAILURE;

    VIDEO_FRAME_INFO_S* vinfo = (VIDEO_FRAME_INFO_S*)pData;
    if (!vinfo) return CVI_FAILURE;
    VIDEO_FRAME_S* f = &vinfo->stVFrame;

    if (f->u32Length[0] == 0) return CVI_FAILURE;

    f->pu8VirAddr[0] = (CVI_U8*)CVI_SYS_Mmap(f->u64PhyAddr[0], f->u32Length[0]);
    if (!f->pu8VirAddr[0]) {
        CVI_TRACE_LOG(CVI_DBG_ERR, "CVI_SYS_Mmap failed\n");
        return CVI_FAILURE;
    }

    cv::Mat frame_rgb888(f->u32Height, f->u32Width, CV_8UC3, f->pu8VirAddr[0]);

    auto now = std::chrono::steady_clock::now();
    bool report_person  = (now - g_last_person_report)  >= std::chrono::milliseconds(g_cfg.report_ms);
    bool can_alert      = (now - g_last_object_alert)   >= std::chrono::milliseconds(g_cfg.cooldown_ms);
    bool can_zone       = (now - g_last_zone_alert)     >= std::chrono::milliseconds(g_cfg.cooldown_ms);
    
    std::string result_json_str;
    {
        std::lock_guard<std::mutex> lk(g_det_mutex);
        result_json_str = model_detector_from_mat(g_model, frame_rgb888, report_person, can_alert, can_zone);
    }
    try {
        json parsed = json::parse(result_json_str);
        process_detection_results(parsed, f->pu8VirAddr[0], g_last_object_alert, g_last_zone_alert, g_last_person_report);
    } catch (...) {}

    CVI_SYS_Munmap(f->pu8VirAddr[0], f->u32Length[0]);
    return CVI_SUCCESS;
}

connectivity_mode_t parse_mode(int argc, char* argv[]) {
    std::string mode = "mqtt";  // valor por defecto

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--mode") == 0) && (i + 1 < argc)) {
            mode = argv[i + 1];
        }
    }
    if (mode == "http") return CONNECTIVITY_MODE_HTTP;
    if (mode == "mqtt") return CONNECTIVITY_MODE_MQTT;

    std::cerr << "Error: modo inválido '" << mode << "'. Use --mode mqtt|http\n";
    exit(1);
}


int main(int argc, char* argv[]) {
    load_config(PATH_CONF);
    connectivity_mode = parse_mode(argc, argv);
    signal(SIGINT, app_ipcam_ExitSig_handle);
    signal(SIGTERM, app_ipcam_ExitSig_handle);
    initWiFi();
    initHttpd();
    initConnectivity(connectivity_mode); 
    if (initVideo()) return -1;

    video_ch_param_t param;
    
    param.format = VIDEO_FORMAT_DEFAULT;
    param.width  = VIDEO_WIDTH_DEFAULT;
    param.height = VIDEO_HEIGHT_DEFAULT;
    param.fps    = VIDEO_FPS_DEFAULT;
    
    setupVideo(VIDEO_CH0, &param);
    registerVideoFrameHandler(VIDEO_CH0, 0, fpRunYolo_CH0, NULL);

    // Usar la constante definida para la ruta del modelo
    g_model = initialize_model(g_cfg.model_yolo);
    if (!g_model) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Model initialization failed");

        deinitRtsp();
        deinitVideo();
        return -1;
    }

    startVideo();
    while (1) sleep(1);

    return 0;
}