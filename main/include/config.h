#include <sstream>
#include <vector>

struct Config {
    // devices
    std::string mac;

    // eventos
    std::string ev_detect;
    std::string ev_zone;

    // MQTT / HTTP
    std::string mqtt_ev_topic;
    std::string mqtt_st_topic;
    std::string http_url;

    // timers
    int cooldown_ms;
    int report_ms;

    // paths
    std::string dir_images;
    std::string dir_images_bak;
    std::string model_yolo;
    std::string ssl_certs;

    // zona
    bool zone_enabled;
    std::string zone_type;
    std::vector<float> z_coords;

    std::string side;        // ARRIBA, ABAJO, IZQ, DER, DENTRO, FUERA
    std::string orientation; // VERT, HORIZ (solo aplica a LINE y LINE_ADV)

    int zone_r, zone_g, zone_b;
    int zone_thick;

    // almacenamiento
    int max_images;

    // clases
    std::vector<std::string>  cls_detect;
    std::string reporte_personas;
};

bool load_config(const std::string& filename);
extern Config g_cfg;
