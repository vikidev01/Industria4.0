#include <thread>       // 解决std::this_thread错误
#include <mutex>
#include <memory>
#include <sscma.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <syslog.h>
#include <model_detector.h>
#include "hv/HttpServer.h"
#include "hv/hasync.h"   // import hv::async
#include "hv/hthread.h"  // import hv_gettid
#include "hv/requests.h" 
#include "common.h"
#include "connect.h"
#include "daemon.h"
#include "utils_device.h"
#include "utils_file.h"
#include "utils_led.h"
#include "utils_user.h"
#include "utils_wifi.h"
#include "version.h"
#include "frame_builder.h"
#include "hv/mqtt_client.h"
#include "hv/hssl.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <atomic>
#include "cvi_sys.h"
#include "cvi_comm_video.h"
#include "global_cfg.h"
#include "config.h"
using nlohmann_json = nlohmann::json;
using namespace hv;

hv::MqttClient cli;
auto internal_mode = 0; //0: mqtt - 1:http
uint64_t getUptime() {
    std::ifstream uptime_file("/proc/uptime");
    if (uptime_file.is_open()) {
        double uptime_seconds;
        uptime_file >> uptime_seconds;
        return static_cast<uint64_t>(uptime_seconds * 1000);
    } else {
        std::cerr << "Failed to open /proc/uptime." << std::endl;
        return 0;
    }
}

uint64_t getTimestamp() {
    auto now       = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return timestamp.count();
}

void initMqtt() {
    hssl_ctx_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.ca_file   = "/etc/ssl/certs/cam_1/AmazonRootCA1.pem";
    opt.crt_file  = "/etc/ssl/certs/cam_1/cam_1-certificate.pem.crt";
    opt.key_file  = "/etc/ssl/certs/cam_1/cam_1-private.pem.key";
    opt.verify_peer = 1;

    std::cout << "[MQTT] Creando SSL context..." << std::endl;
    int sslRet = cli.newSslCtx(&opt);
    
    // Mensaje más descriptivo para SSL
    if (sslRet == 0) {
        std::cout << "[MQTT] SSL context creado exitosamente" << std::endl;
    } else {
        std::cout << "[MQTT] ERROR creando SSL context (código: " << sslRet << ")" << std::endl;
        std::cout << "[MQTT] Verifique los archivos de certificados en /etc/ssl/certs/cam_1/" << std::endl;
        return;
    }

    cli.setConnectTimeout(3000); 
    const char* host = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
    int port = 8883;

    cli.onConnect = [](hv::MqttClient* cli) {
        std::cout << "[MQTT] CONEXIÓN EXITOSA al broker MQTT" << std::endl;
    };

    cli.onClose = [](hv::MqttClient* cli) {
        std::cout << "[MQTT] DESCONECTADO del broker, intentando reconectar..." << std::endl;
    };

    cli.onMessage = [](hv::MqttClient* cli, mqtt_message_t* msg) {
        std::cout << "[MQTT] Mensaje recibido en topic: "
                  << std::string(msg->topic, msg->topic_len) 
                  << ", tamaño: " << msg->payload_len << " bytes" << std::endl;
    };

    // Conexión inicial
    std::cout << "[MQTT] Intentando conectar a " << host << ":" << port << " con SSL..." << std::endl;
    int connRet = cli.connect(host, port, 1); // 1 = SSL
    
    // Mensajes descriptivos para la conexión
    if (connRet == 0) {
        std::cout << "[MQTT] CONEXIÓN INICIAL EXITOSA" << std::endl;
    } else {
        std::cout << "[MQTT] ERROR en conexión inicial (código: " << connRet << ")" << std::endl;
        std::cout << "[MQTT] Posibles causas: " << std::endl;
        std::cout << "[MQTT]   - Problemas de red/resolución DNS" << std::endl;
        std::cout << "[MQTT]   - Certificados SSL inválidos o expirados" << std::endl;
        std::cout << "[MQTT]   - Broker no disponible" << std::endl;
        std::cout << "[MQTT]   - Problemas de autenticación" << std::endl;
    }

    // Hilo de reconexión con mensajes mejorados
    std::thread([host, port]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Aumentado a 5 segundos
            
            if (!cli.isConnected()) {
                std::cout << "[MQTT] INTENTANDO RECONEXIÓN..." << std::endl;
                int r = cli.connect(host, port, 1);
                
                if (r == 0) {
                    std::cout << "[MQTT] RECONEXIÓN EXITOSA" << std::endl;
                } else {
                    std::cout << "[MQTT] FALLA en reconexión (código: " << r << ")" << std::endl;
                    
                    // Mensajes específicos basados en códigos de error comunes
                    switch (r) {
                        case -1:
                            std::cout << "[MQTT] Error: Timeout de conexión" << std::endl;
                            break;
                        case -2:
                            std::cout << "[MQTT] Error: Resolución DNS fallida" << std::endl;
                            break;
                        case -3:
                            std::cout << "[MQTT] Error: Conexión rechazada" << std::endl;
                            break;
                        default:
                            std::cout << "[MQTT] Error desconocido, verifique configuración" << std::endl;
                            break;
                    }
                }
            }
        }
    }).detach();

    // Hilo principal del loop de MQTT
    std::thread([]() {
        std::cout << "[MQTT] Iniciando loop MQTT..." << std::endl;
        cli.run();
    }).detach();
}



extern "C" {

#define API_STR(api, func)  "/api/" #api "/" #func
#define API_GET(api, func)  router.GET(API_STR(api, func), func)
#define API_POST(api, func) router.POST(API_STR(api, func), func)


std::mutex detector_mutex;
ma::Camera* global_camera = nullptr;
ma::Model* global_model = nullptr;
ma::engine::EngineCVI* global_engine = nullptr;
bool isInitialized = false;
static HttpServer server;
std::string url = "http://192.168.4.1/report";

static void registerHttpRedirect(HttpService& router) {
    router.GET("/hotspot-detect*", [](HttpRequest* req, HttpResponse* resp) {  // IOS
        syslog(LOG_DEBUG, "[/hotspot-detect*]current url: %s -> redirect to %s\n", req->Url().c_str(), REDIRECT_URL);

        return resp->Redirect(REDIRECT_URL);
    });

    router.GET("/generate*", [](HttpRequest* req, HttpResponse* resp) {  // android
        syslog(LOG_DEBUG, "[/generate*]current url: %s -> redirect to %s\n", req->Url().c_str(), REDIRECT_URL);

        return resp->Redirect(REDIRECT_URL);
    });

    router.GET("/*.txt", [](HttpRequest* req, HttpResponse* resp) {  // windows
        syslog(LOG_DEBUG, "[/*.txt]current url: %s -> redirect to %s\n", req->Url().c_str(), REDIRECT_URL);

        return resp->Redirect(REDIRECT_URL);
    });

    router.GET("/index.html", [](HttpRequest* req, HttpResponse* resp) { return resp->File(WWW("index.html")); });
}

static void registerUserApi(HttpService& router) {
    API_POST(userMgr, login);
    API_GET(userMgr, queryUserInfo);
    // API_POST(userMgr, updateUserName); # disabled
    API_POST(userMgr, updatePassword);
    API_POST(userMgr, addSShkey);
    API_POST(userMgr, deleteSShkey);
    API_POST(userMgr, setSShStatus);
}

static void registerWiFiApi(HttpService& router) {
    API_GET(wifiMgr, queryWiFiInfo);
    API_POST(wifiMgr, scanWiFi);
    API_GET(wifiMgr, getWiFiScanResults);
    API_POST(wifiMgr, connectWiFi);
    API_POST(wifiMgr, disconnectWiFi);
    API_POST(wifiMgr, switchWiFi);
    API_GET(wifiMgr, getWifiStatus);
    API_POST(wifiMgr, autoConnectWiFi);
    API_POST(wifiMgr, forgetWiFi);
}

static void registerDeviceApi(HttpService& router) {
    API_GET(deviceMgr, getSystemStatus);
    API_GET(deviceMgr, queryServiceStatus);
    API_POST(deviceMgr, getSystemUpdateVesionInfo);
    API_GET(deviceMgr, queryDeviceInfo);
    API_POST(deviceMgr, updateDeviceName);
    API_POST(deviceMgr, updateChannel);
    API_POST(deviceMgr, setPower);
    API_POST(deviceMgr, updateSystem);
    API_GET(deviceMgr, getUpdateProgress);
    API_POST(deviceMgr, cancelUpdate);

    API_GET(deviceMgr, getDeviceList);
    API_GET(deviceMgr, getDeviceInfo);

    API_GET(deviceMgr, getAppInfo);
    API_POST(deviceMgr, uploadApp);

    API_GET(deviceMgr, getModelInfo);
    API_GET(deviceMgr, getModelFile);
    API_POST(deviceMgr, uploadModel);
    API_GET(deviceMgr, getModelList);

    API_POST(deviceMgr, savePlatformInfo);
    API_GET(deviceMgr, getPlatformInfo);
}

static void registerFileApi(HttpService& router) {
    API_GET(fileMgr, queryFileList);
    API_POST(fileMgr, uploadFile);
    API_POST(fileMgr, deleteFile);
}

static void registerLedApi(HttpService& router) {
    router.POST("/api/led/{led}/on", ledOn);
    router.POST("/api/led/{led}/off", ledOff);
    router.POST("/api/led/{led}/brightness", ledBrightness);
}

static void registerWebSocket(HttpService& router) {
    router.GET(API_STR(deviceMgr, getCameraWebsocketUrl), [](HttpRequest* req, HttpResponse* resp) {
        hv::Json data;
        data["websocketUrl"] = "ws://" + req->host + ":" + std::to_string(WS_PORT);
        hv::Json res;
        res["code"] = 0;
        res["msg"]  = "";
        res["data"] = data;

        std::string s_time = req->GetParam("time");  // req->GetString("time");
        int64_t time       = std::stoll(s_time);
        time /= 1000;  // to sec
        std::string cmd = "date -s @" + std::to_string(time);
        system(cmd.c_str());

        syslog(LOG_INFO, "WebSocket: %s\n", data["websocketUrl"]);
        return resp->Json(res);
    });
}

int initWiFi() {
    char cmd[128]        = SCRIPT_WIFI_START;
    std::string wifiName = getWiFiName("wlan0");
    
    std::thread th;

    initUserInfo();
    initSystemStatus();
    getSnCode();

    strcat(cmd, wifiName.c_str());
    strcat(cmd, " ");
    strcat(cmd, std::to_string(TTYD_PORT).c_str());
    strcat(cmd, " ");
    strcat(cmd, std::to_string(g_userId).c_str());
    system(cmd);

    char result[8] = "";
    if (0 == exec_cmd(SCRIPT_WIFI("wifi_valid"), result, NULL)) {
        if (strcmp(result, "0") == 0) {
            g_wifiMode = 4;  // No wifi module
        }
    }

    if (4 != g_wifiMode) {
        th = std::thread(monitorWifiStatusThread);
        th.detach();
    }

    th = std::thread(updateSystemThread);
    th.detach();

    return 0;
}

int stopWifi() {
    g_wifiStatus   = false;
    g_updateStatus = false;
    system(SCRIPT_WIFI_STOP);

    return 0;
}

static void initHttpsService() {
    if (0 != access(PATH_SERVER_CRT, F_OK)) {
        syslog(LOG_ERR, "The crt file does not exist\n");
        return;
    }

    if (0 != access(PATH_SERVER_KEY, F_OK)) {
        syslog(LOG_ERR, "The key file does not exist\n");
        return;
    }

    server.https_port = HTTPS_PORT;
    hssl_ctx_opt_t param;

    memset(&param, 0, sizeof(param));
    param.crt_file = PATH_SERVER_CRT;
    param.key_file = PATH_SERVER_KEY;
    param.endpoint = HSSL_SERVER;

    if (server.newSslCtx(&param) != 0) {
        syslog(LOG_ERR, "new SSL_CTX failed!\n");
        return;
    }

    syslog(LOG_INFO, "https service open successful!\n");
}

int initHttpd() {
    
    static HttpService router;
    router.AllowCORS();
    router.Static("/", WWW(""));
    router.Use(authorization);
    router.GET("/api/version", [](HttpRequest* req, HttpResponse* resp) {
        hv::Json res;
        res["code"]      = 0;
        res["msg"]       = "";
        res["data"]      = PROJECT_VERSION;
        res["uptime"]    = getUptime();
        res["timestamp"] = getTimestamp();
        return resp->Json(res);
    });


    registerHttpRedirect(router);
    registerUserApi(router);
    registerWiFiApi(router);
    registerDeviceApi(router);
    registerFileApi(router);
    registerLedApi(router);
    registerWebSocket(router);
    
    

#if HTTPS_SUPPORT
    initHttpsService();
#endif

    // server.worker_threads = 3;
    server.service = &router;
    server.port    = HTTPD_PORT;
    server.start();

    return 0;
}

int deinitHttpd() {
    server.stop();
    hv::async::cleanup();
    return 0;
}

void sendDetectionByMqtt(const std::string& payload, const std::string& topic) {
    if (cli.isConnected()) {
        cli.publish(topic, payload);
    } else {
        printf("Cliente NO conectado\n");
        syslog(LOG_WARNING, "MQTT client not connected. Skipping publish.");
    }
}

void sendTestHttpPost(const std::string& payload) {
    const std::string url = g_cfg.http_url;
    const char* url_cstr = url.c_str();

    http_headers headers;
    headers["Content-Type"] = "text/plain";

    MA_LOGI("LOG_INFO", "Enviando prueba HTTP POST a %s", url_cstr);
    MA_LOGI("LOG_DEBUG", "Payload: %s", payload.c_str());
    
    auto resp = requests::post(url_cstr, payload, headers);

    if (resp == nullptr) {
        MA_LOGI("LOG_ERR", "Error: No se recibió respuesta del servidor");
    } else {
        MA_LOGI("LOG_INFO", "Respuesta HTTP %d - Contenido: %s", 
              resp->status_code, resp->body.c_str());
    }
}

std::string timestampToHexString(uint64_t timestamp) {
    std::stringstream hex_stream;
    uint32_t timestamp_32 = static_cast<uint32_t>(timestamp);
    hex_stream << "02";
    hex_stream << std::setw(8) << std::setfill('0') << std::hex << std::uppercase << timestamp_32;
    return hex_stream.str();
}

struct AlarmConfig {
    std::string json_key; 
    std::string base_payload; // Parte fija del payload
    std::string topic;     
    std::chrono::steady_clock::time_point* last_alert; 
};


void process_detection_results(nlohmann_json& parsed,
                               uint8_t* frame,
                               std::chrono::steady_clock::time_point& last_object_alert,
                               std::chrono::steady_clock::time_point& last_zone_alert,
                               std::chrono::steady_clock::time_point& last_person_report) 
{
    auto now = std::chrono::steady_clock::now();
    uint64_t current_timestamp = getTimestamp(); // Usar tu función existente

    try {
        std::vector<AlarmConfig> alarms = {
            { g_cfg.ev_detect, std::string("E114014000010000") + g_cfg.mac, g_cfg.mqtt_ev_topic, &last_object_alert },
            { g_cfg.ev_zone,   std::string("E114014000010000") + g_cfg.mac, g_cfg.mqtt_ev_topic, &last_zone_alert }
        };

        for (const auto& alarm : alarms) {
            if (parsed.contains(alarm.json_key)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *(alarm.last_alert)).count();
                if (elapsed < g_cfg.cooldown_ms) {
                    continue; 
                }
                *(alarm.last_alert) = now;

                // Construir el payload con el formato requerido
                std::string final_payload = alarm.base_payload;
                final_payload += timestampToHexString(current_timestamp);
                
                // Agregar el código final según el tipo de alarma
                if (alarm.json_key == g_cfg.ev_detect) {
                    final_payload += "2103"; // 2103 para detección
                } else if (alarm.json_key == g_cfg.ev_zone) {
                    final_payload += "2102"; // 2102 para zona
                }

                MA_LOGD(TAG, "Enviando alerta %s: %s", alarm.json_key.c_str(), final_payload.c_str());

                if (internal_mode == CONNECTIVITY_MODE_MQTT) {
                    sendDetectionByMqtt(final_payload, alarm.topic);
                } else if (internal_mode == CONNECTIVITY_MODE_HTTP) {
                    sendTestHttpPost(final_payload);
                }
            }
        }
        
        if (parsed.contains("person_count") && g_cfg.reporte_personas == "si") {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_person_report).count();
            if (elapsed >= g_cfg.cooldown_ms) {
                last_person_report = now;
                int person_count = parsed["person_count"].get<int>();
                
                // Construir el payload para el contador de personas
                std::string final_payload = std::string("E114010000010000") + g_cfg.mac;
                final_payload += timestampToHexString(current_timestamp);
                final_payload += "4001";
                
                // Formatear el contador de personas como hexadecimal de 4 dígitos
                std::stringstream count_ss;
                count_ss << std::setw(4) << std::setfill('0') << std::hex << person_count;
                final_payload += count_ss.str();

                MA_LOGD(TAG, "Enviando reporte de personas: %s", final_payload.c_str());

                if (internal_mode == CONNECTIVITY_MODE_MQTT) {
                    sendDetectionByMqtt(final_payload, g_cfg.mqtt_st_topic);
                } else if (internal_mode == CONNECTIVITY_MODE_HTTP) {
                    sendTestHttpPost(final_payload);
                }
            }
        }
    }
    catch (const nlohmann_json::exception& e) {
        MA_LOGE(TAG, "Error procesando JSON: %s", e.what());
    }
    catch (const std::exception& e) {
        MA_LOGE(TAG, "Error inesperado: %s", e.what());
    }
}

void initConnectivity(connectivity_mode_t& connectivity_mode) {

    if (connectivity_mode == CONNECTIVITY_MODE_MQTT){
        std::thread th;
        MA_LOGI("System", "Iniciando modo MQTT...");
        th           = std::thread(initMqtt);
        th.detach();
        internal_mode = 0;
    } 
    if (connectivity_mode == CONNECTIVITY_MODE_HTTP){
        MA_LOGI("System", "Iniciando modo WiFi...");
        internal_mode = 1;
    } 
}
}  // extern "C" {