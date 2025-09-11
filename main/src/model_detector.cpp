#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdio.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <sscma.h>
#include <vector>
#include <thread>
#include <iterator>
#include <ClassMapper.h>   
#include <unistd.h>
#include <filesystem> 
#include <nlohmann/json.hpp>
#include "global_cfg.h"
using json = nlohmann::json;
#include "cvi_sys.h"
#include "cvi_comm_video.h"
#include <regex>
#include "config.h"
#include <opencv2/imgproc.hpp>
class ColorPalette {
public:
    static std::vector<cv::Scalar> getPalette() {
        return palette;
    }

    static cv::Scalar getColor(int index) {
        return palette[index % palette.size()];
    }

private:
    static const std::vector<cv::Scalar> palette;
};

const std::vector<cv::Scalar> ColorPalette::palette = {
    cv::Scalar(0, 255, 0),     cv::Scalar(0, 170, 255), cv::Scalar(0, 128, 255), cv::Scalar(0, 64, 255),  cv::Scalar(0, 0, 255),     cv::Scalar(170, 0, 255),   cv::Scalar(128, 0, 255),
    cv::Scalar(64, 0, 255),    cv::Scalar(0, 0, 255),   cv::Scalar(255, 0, 170), cv::Scalar(255, 0, 128), cv::Scalar(255, 0, 64),    cv::Scalar(255, 128, 0),   cv::Scalar(255, 255, 0),
    cv::Scalar(128, 255, 0),   cv::Scalar(0, 255, 128), cv::Scalar(0, 255, 255), cv::Scalar(0, 128, 128), cv::Scalar(128, 0, 255),   cv::Scalar(255, 0, 255),   cv::Scalar(128, 128, 255),
    cv::Scalar(255, 128, 128), cv::Scalar(255, 64, 64), cv::Scalar(64, 255, 64), cv::Scalar(64, 64, 255), cv::Scalar(128, 255, 255), cv::Scalar(255, 255, 128),
};
/*
const std::vector<std::string> ClassMapper::classes = {
    "helmet",     // ID 0
    "no_helmet",  // ID 1
    "nose",       // ID 2
    "person",     // ID 3
    "vest"        // ID 4
};*/
const std::vector<std::string> ClassMapper::classes= {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
        "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"
    };


cv::Mat preprocessImage(cv::Mat& image, ma::Model* model) {
    int ih = image.rows;
    int iw = image.cols;
    int oh = 0;
    int ow = 0;

    if (model->getInputType() == MA_INPUT_TYPE_IMAGE) {
        oh = reinterpret_cast<const ma_img_t*>(model->getInput())->height;
        ow = reinterpret_cast<const ma_img_t*>(model->getInput())->width;
    }

    cv::Mat resizedImage;
    double resize_scale = std::min((double)oh / ih, (double)ow / iw);
    int nh              = (int)(ih * resize_scale);
    int nw              = (int)(iw * resize_scale);
    cv::resize(image, resizedImage, cv::Size(nw, nh));
    int top    = (oh - nh) / 2;
    int bottom = (oh - nh) - top;
    int left   = (ow - nw) / 2;
    int right  = (ow - nw) - left;

    cv::Mat paddedImage;
    cv::copyMakeBorder(resizedImage, paddedImage, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::cvtColor(paddedImage, paddedImage, cv::COLOR_BGR2RGB);

    return paddedImage;
}

void release_camera(ma::Camera*& camera) noexcept {
    
    camera->stopStream();
    camera = nullptr; // 确保线程安全
}

void release_model(ma::Model*& model, ma::engine::EngineCVI*& engine) {
    if (model) ma::ModelFactory::remove(model);  // 先释放model
    if (engine) delete engine; 

    model = nullptr;
    engine = nullptr;
}

ma::Camera* initialize_camera() noexcept{

    ma::Device* device = ma::Device::getInstance();
    ma::Camera* camera = nullptr;

    for (auto& sensor : device->getSensors()) {
        if (sensor->getType() == ma::Sensor::Type::kCamera) {
            camera = static_cast<ma::Camera*>(sensor);
            break;
        }
    }
    if (!camera) {
        MA_LOGE(TAG, "No camera sensor found");
        return camera;
    }
    if (camera->init(0) != MA_OK) {  // 假设0是默认模式
        MA_LOGE(TAG, "Camera initialization failed");
        return camera;
    }

    ma::Camera::CtrlValue value;
    value.i32 = 0;
    if (camera->commandCtrl(ma::Camera::CtrlType::kChannel, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera channel");
        return camera;
    }

    value.u16s[0] = 1920;
    value.u16s[1] = 1080;
    if (camera->commandCtrl(ma::Camera::CtrlType::kWindow, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera resolution");
        return camera;
    }

    value.i32 = 5;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kWrite, value);

    value.i32 = 0;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kRead, value);
    
    MA_LOGI(MA_TAG, "The value of kFps is: %d",  value.i32);

    value.i32 = 1;
    if (camera->commandCtrl(ma::Camera::CtrlType::kChannel, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera channel");
        return camera;
    }
    value.u16s[0] = 1920;
    value.u16s[1] = 1080;
    if (camera->commandCtrl(ma::Camera::CtrlType::kWindow, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera resolution");
        return camera;
    }
    value.i32 = 5;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kWrite, value);

    value.i32 = 0;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kRead, value);
    
    MA_LOGI(MA_TAG, "The value of kFps is: %d",  value.i32);
    value.i32 = 2;
    if (camera->commandCtrl(ma::Camera::CtrlType::kChannel, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera channel");
        return camera;
    }
    value.i32 = 5;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kWrite, value);

    value.i32 = 0;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kRead, value);
    
    MA_LOGI(MA_TAG, "The value of kFps is: %d",  value.i32);


    camera->startStream(ma::Camera::StreamMode::kRefreshOnReturn);

    return camera;

}

size_t getCurrentRSS() {
    long rss = 0L;
    FILE* fp = nullptr;
    if ((fp = fopen("/proc/self/statm", "r")) == nullptr)
        return 0; // 无法获取
    
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return rss * sysconf(_SC_PAGESIZE) / 1024; // 转换为 KB
}

ma::Model* initialize_model(const std::string& model_path) noexcept{
    ma::Model* model_null = nullptr;
    ma_err_t ret = MA_OK;
    using namespace ma;
    auto* engine = new ma::engine::EngineCVI();
    ret          = engine->init();
    if (ret != MA_OK) {
        MA_LOGE(TAG, "engine init failed");
        delete engine;
        return model_null;
    }
    ret = engine->load(model_path);

    MA_LOGI(TAG, "engine load model %s", model_path);
    if (ret != MA_OK) {
        MA_LOGE(TAG, "engine load model failed");
        delete engine;
        return model_null;
    }

    ma::Model* model = ma::ModelFactory::create(engine);

    if (model == nullptr) {
        MA_LOGE(TAG, "model not supported");
        ma::ModelFactory::remove(model);
        delete engine;
        return model_null;
    }

    MA_LOGI(TAG, "model type: %d", model->getType());

    return model;
}

void flush_buffer(ma::Camera* camera) {
    ma_img_t tmp_frame;
    while (camera->retrieveFrame(tmp_frame, MA_PIXEL_FORMAT_JPEG) == MA_OK) {
        camera->returnFrame(tmp_frame); // Libera inmediatamente los frames acumulados
    }
}

int extract_number(const std::string& filename) {
    auto dot_pos = filename.find_last_of('.');
    std::string name = (dot_pos == std::string::npos) ? filename : filename.substr(0, dot_pos);
    auto underscore_pos = name.find_last_of('_');
    if (underscore_pos == std::string::npos) return -1;
    std::string num_str = name.substr(underscore_pos + 1);
    if (num_str.empty()) return -1;
    try { return std::stoi(num_str); } catch (...) { return -1; }
}

void cleanup_old_images(const std::string& folder_path, int max_images_to_keep) {
    namespace fs = std::filesystem;
    if (max_images_to_keep <= 0) return;

    try {
        struct Item {
            fs::path path;
            fs::file_time_type t;
            int num;
        };

        std::vector<Item> items;
        for (const auto& entry : fs::directory_iterator(folder_path)) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg") {
                std::error_code ec;
                auto t = fs::last_write_time(entry, ec);
                if (ec) t = fs::file_time_type::min(); // si falla, lo tratamos como muy antiguo
                items.push_back({entry.path(), t, extract_number(entry.path().filename().string())});
            }
        }

        // Orden: más antiguo primero; si empata tiempo, usar número; si empata, por nombre.
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b){
            if (a.t != b.t) return a.t < b.t;
            if ((a.num >= 0) && (b.num >= 0) && a.num != b.num) return a.num < b.num;
            return a.path.filename().string() < b.path.filename().string();
        });
        if (items.size() > (size_t)max_images_to_keep) {
            size_t to_delete = items.size() - (size_t)max_images_to_keep;
            for (size_t i = 0; i < to_delete; ++i) {
                std::error_code ec;
                fs::remove(items[i].path, ec);
               
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "cleanup_old_images error: " << e.what() << "\n";
    }
}

int find_max_image_number(const std::string& folder_path) {
    namespace fs = std::filesystem;
    int max_num = 0;
    try {
        for (const auto& entry : fs::directory_iterator(folder_path)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.find("alarm_") == 0 && 
                    (filename.find(".jpg") != std::string::npos || 
                     filename.find(".jpeg") != std::string::npos)) {
                    int num = extract_number(filename);
                    if (num > max_num) {
                        max_num = num;
                    }
                }
            }
        }
    } catch (...) {
        // Si hay error al leer el directorio, empezamos desde 0
    }
    return max_num;
}

void draw_restricted_zone(cv::Mat& frame, const cv::Scalar& color) {
    std::string label = g_cfg.ev_zone;

    auto get_label_position = [&](const cv::Point& top_left, const cv::Point& bottom_right, bool inside = true) {
        int x, y;
        
        if (inside) {
            // Posición dentro del rectángulo (centrado)
            x = top_left.x + (bottom_right.x - top_left.x) / 2 - 40;
            y = top_left.y + (bottom_right.y - top_left.y) / 2 + 10;
        } else {
            // Posición fuera del rectángulo (esquina superior derecha)
            x = std::min(top_left.x, bottom_right.x);
            y = std::min(top_left.y, bottom_right.y);
            x = std::max(x, 10);
            y = std::max(y, 30);
        }
        return cv::Point(x, y);
    };

    if (g_cfg.zone_type == "LINE") {
        int divider = static_cast<int>(g_cfg.z_coords[0] * (g_cfg.orientation == "VERT" ? frame.cols : frame.rows));
        
        if (g_cfg.orientation == "VERT") {
            cv::line(frame, {divider, 0}, {divider, frame.rows}, color, 2);

            // Posicionar label según el side
            if (g_cfg.side == "IZQ") {
                cv::putText(frame, label, {divider - 200, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
                cv::arrowedLine(frame, {divider - 40, frame.rows / 2}, {divider - 10, frame.rows / 2}, color, 2);
            } else if (g_cfg.side == "DER") {
                cv::putText(frame, label, {divider + 10, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
                cv::arrowedLine(frame, {divider + 40, frame.rows / 2}, {divider + 10, frame.rows / 2}, color, 2);
            }

        } else {
            cv::line(frame, {0, divider}, {frame.cols, divider}, color, 2);

            // Posicionar label según el side
            if (g_cfg.side == "ARRIBA") {
                cv::putText(frame, label, {frame.cols - 150, divider - 20}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
                cv::arrowedLine(frame, {frame.cols / 2, divider - 40}, {frame.cols / 2, divider - 10}, color, 2);
            } else if (g_cfg.side == "ABAJO") {
                cv::putText(frame, label, {frame.cols - 150, divider + 40}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
                cv::arrowedLine(frame, {frame.cols / 2, divider + 40}, {frame.cols / 2, divider + 10}, color, 2);
            }
        }

    } else if (g_cfg.zone_type == "LINE_ADV") {
        int x1 = static_cast<int>(g_cfg.z_coords[0] * frame.cols);
        int y1 = static_cast<int>(g_cfg.z_coords[1] * frame.rows);
        int x2 = static_cast<int>(g_cfg.z_coords[2] * frame.cols);
        int y2 = static_cast<int>(g_cfg.z_coords[3] * frame.rows);

        cv::line(frame, {x1, y1}, {x2, y2}, color, 2);

        // Calcular punto medio y posición del label
        cv::Point mid_point((x1 + x2) / 2, (y1 + y2) / 2);
        cv::Point label_pos;
        
        // Determinar posición basada en la orientación de la línea
        if (abs(x2 - x1) > abs(y2 - y1)) { // Línea más horizontal
            label_pos = cv::Point(mid_point.x - 40, mid_point.y - 15);
        } else { // Línea más vertical
            label_pos = cv::Point(mid_point.x + 10, mid_point.y);
        }
        
        cv::putText(frame, label, label_pos, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

    } else if (g_cfg.zone_type == "RECT") {
        int x1 = static_cast<int>(g_cfg.z_coords[0] * frame.cols);
        int y1 = static_cast<int>(g_cfg.z_coords[1] * frame.rows);
        int x2 = static_cast<int>(g_cfg.z_coords[2] * frame.cols);
        int y2 = static_cast<int>(g_cfg.z_coords[3] * frame.rows);

        cv::rectangle(frame, {x1, y1}, {x2, y2}, color, 2);
        
        // Posicionar label según el side (inside/outside)
        cv::Point label_pos = get_label_position({x1, y1}, {x2, y2}, g_cfg.side == "DENTRO");
        cv::putText(frame, label, label_pos, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

    } else if (g_cfg.zone_type == "POLY") {
        std::vector<cv::Point> polygon_points;
        int min_x = frame.cols, min_y = frame.rows, max_x = 0, max_y = 0;

        for (size_t i = 0; i < g_cfg.z_coords.size(); i += 2) {
            int px = static_cast<int>(g_cfg.z_coords[i] * frame.cols);
            int py = static_cast<int>(g_cfg.z_coords[i+1] * frame.rows);
            polygon_points.push_back(cv::Point(px, py));
            min_x = std::min(min_x, px);
            min_y = std::min(min_y, py);
            max_x = std::max(max_x, px);
            max_y = std::max(max_y, py);
        }

        cv::polylines(frame, polygon_points, true, color, 2);
        
        // Posicionar label según el side (inside/outside)
        cv::Point label_pos = get_label_position({min_x, min_y}, {max_x, max_y}, g_cfg.side == "DENTRO");
        cv::putText(frame, label, label_pos, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
    }
}

bool is_inside_restricted_zone(const cv::Point& object_center, const cv::Mat& frame) {
    if (!g_cfg.zone_enabled) return false;

    if (g_cfg.zone_type == "LINE") {
        int divider = static_cast<int>(g_cfg.z_coords[0] * (g_cfg.orientation == "VERT" ? frame.cols : frame.rows));

        if (g_cfg.orientation == "VERT") {
            if (g_cfg.side == "IZQ")  return (object_center.x < divider);
            if (g_cfg.side == "DER") return (object_center.x > divider);

        } else if (g_cfg.orientation == "HORIZ") {
            if (g_cfg.side == "ARRIBA")   return (object_center.y < divider);
            if (g_cfg.side == "ABAJO") return (object_center.y > divider);
        }

    } else if (g_cfg.zone_type == "LINE_ADV") {
        int x1 = static_cast<int>(g_cfg.z_coords[0] * frame.cols);
        int y1 = static_cast<int>(g_cfg.z_coords[1] * frame.rows);
        int x2 = static_cast<int>(g_cfg.z_coords[2] * frame.cols);
        int y2 = static_cast<int>(g_cfg.z_coords[3] * frame.rows);

        // Ecuación de línea general
        float A = y2 - y1;
        float B = x1 - x2;
        float C = x2*y1 - x1*y2;

        float side_val = A * object_center.x + B * object_center.y + C;

        // Decidir qué lado es válido según orientación
        if (abs(x2 - x1) > abs(y2 - y1)) { // tendencia horizontal
            if (g_cfg.side == "ARRIBA")   return (side_val < 0);
            if (g_cfg.side == "ABAJO") return (side_val > 0);
        } else { // tendencia vertical
            if (g_cfg.side == "IZQ")  return (side_val > 0);
            if (g_cfg.side == "DER") return (side_val < 0);
        }

    }  else if (g_cfg.zone_type == "RECT") {
    int x1 = static_cast<int>(g_cfg.z_coords[0] * frame.cols);
    int y1 = static_cast<int>(g_cfg.z_coords[1] * frame.rows);
    int x2 = static_cast<int>(g_cfg.z_coords[2] * frame.cols);
    int y2 = static_cast<int>(g_cfg.z_coords[3] * frame.rows);

    cv::Rect zone_rect(x1, y1, x2 - x1, y2 - y1);
    bool inside = zone_rect.contains(object_center);

    if (g_cfg.side == "DENTRO") {
        if (inside) {
            cv::Point label_pos(
                zone_rect.x + zone_rect.width / 2,
                zone_rect.y + 5
            );
            // acá en vez de usar object_center para el label,
            // usás label_pos
        }
        return inside;
    }
    if (g_cfg.side == "FUERA") return !inside;

    } else if (g_cfg.zone_type == "POLY") {
        std::vector<cv::Point> polygon;
        for (size_t i = 0; i < g_cfg.z_coords.size(); i += 2) {
            int px = static_cast<int>(g_cfg.z_coords[i] * frame.cols);
            int py = static_cast<int>(g_cfg.z_coords[i+1] * frame.rows);
            polygon.push_back(cv::Point(px, py));
        }

        bool inside = (cv::pointPolygonTest(polygon, object_center, false) >= 0);

        if (g_cfg.side == "DENTRO") {
            if (inside && !polygon.empty()) {
                cv::Point label_pos(
                    polygon[0].x + 5,
                    polygon[0].y + 5
                );
                // igual que arriba, usás label_pos para dibujar el texto
            }
            return inside;
        }
        if (g_cfg.side == "FUERA") return !inside;
    }

    return false;
}


std::string model_detector_from_mat(ma::Model*& model, cv::Mat& frame,bool person_count, bool can_alert, bool can_zone) {
    auto start = std::chrono::high_resolution_clock::now();
    if (!model) return R"({"error":"model is null"})";
    if (frame.empty()) return R"({"error":"input frame is empty"})";

    ma_img_t img;
    img.data   = (uint8_t*)frame.data;
    img.size   = static_cast<uint32_t>(frame.rows * frame.cols * frame.channels());
    img.width  = static_cast<uint32_t>(frame.cols);
    img.height = static_cast<uint32_t>(frame.rows);
    img.format = MA_PIXEL_FORMAT_RGB888;
    img.rotate = MA_PIXEL_ROTATE_0;
    
    auto* detector = static_cast<ma::model::Detector*>(model); 
    detector->run(&img);
    
    auto results = detector->getResults();
     
    bool detected_obj = false, zone_detect = false, save = false;
    int count = 0;

    if (can_zone && g_cfg.zone_enabled) {
        draw_restricted_zone(frame, cv::Scalar(255, 0, 0));
    }
    
    for (const auto& r : results) {
        std::string cls = ClassMapper::get_class(r.target);
        float x1 = (r.x - r.w / 2.0f) * frame.cols;
        float y1 = (r.y - r.h / 2.0f) * frame.rows;
        float x2 = (r.x + r.w / 2.0f) * frame.cols;
        float y2 = (r.y + r.h / 2.0f) * frame.rows;
        cv::Rect rect(cv::Point((int)x1, (int)y1), cv::Point((int)x2, (int)y2));

        /*
        if (cls == "person") {
            count++;
        }
        */
        if (cls == "person") {
            count++;
            if (person_count) {
                save = true;
                cv::rectangle(frame, rect, cv::Scalar(0, 0, 255), 3);
                //cv::putText(frame, "person",{std::max(0, (int)x1), std::max(15, (int)y1 - 10)}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
            }
        }

        if (std::find(g_cfg.cls_detect.begin(),  g_cfg.cls_detect.end(), cls) != g_cfg.cls_detect.end()){
            detected_obj = true; 
            if (can_zone && g_cfg.zone_enabled) {
                cv::Point ob_center(static_cast<int>(r.x * frame.cols), static_cast<int>(r.y * frame.rows));
                if (is_inside_restricted_zone(ob_center, frame)) {
                    zone_detect = true;
                    save = true;
                    cv::rectangle(frame, rect, cv::Scalar(0, 0, 255), 3);
                    cv::putText(frame, g_cfg.ev_zone,{std::max(0, (int)x1), std::max(15, (int)y1 - 10)}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
                }
            } else if (can_zone && !g_cfg.zone_enabled) {
                cv::Point ob_center(static_cast<int>(r.x * frame.cols), static_cast<int>(r.y * frame.rows));
                save = true;
                cv::rectangle(frame, rect, cv::Scalar(0, 0, 255), 3);
                //cv::putText(frame, g_cfg.ev_detect, {std::max(0, (int)x1), std::max(15, (int)y1 - 10)}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
                }
            }
        }
    json result_json;
    if (person_count) result_json["person_count"] = count;
    if (detected_obj) result_json[g_cfg.ev_detect] = true;
    if (zone_detect) result_json[g_cfg.ev_zone] = true;
    result_json["cooldown"] = false;

    if (save) {
        namespace fs = std::filesystem;
        std::string folder_name = g_cfg.dir_images;
        std::string folder_name_bak = g_cfg.dir_images_bak;
        
        std::string target_folder = folder_name;
        bool use_backup = false;
        bool can_save_image = true;

        try {
            if (!fs::exists(folder_name)) {

                fs::create_directory(folder_name);
            }
            if (!fs::is_directory(folder_name)) {
                throw std::runtime_error("No es un directorio válido");
            }
        } catch (...) {
            use_backup = true;
            target_folder = folder_name_bak;
            
            try {
                if (!fs::exists(folder_name_bak)) {
                    fs::create_directory(folder_name_bak);
                }
            } catch (...) {
                std::cout << "Error: No se pudo acceder a ninguna carpeta de imágenes" << std::endl;
                can_save_image = false;
            }
        }
        
       if (can_save_image) {
            try {
                static std::mutex counter_mutex;
                static int image_counter = find_max_image_number(target_folder);
                
                std::lock_guard<std::mutex> lock(counter_mutex);
                image_counter++;

                std::string filename = target_folder + "/alarm_" + std::to_string(image_counter) + ".jpg";
                cv::Mat image_bgr;
                if (frame.channels() == 3) {
                    cv::cvtColor(frame, image_bgr, cv::COLOR_RGB2BGR);
                } else {
                    image_bgr = frame.clone();
                }

                if (cv::imwrite(filename, image_bgr)) {
                    cleanup_old_images(target_folder, g_cfg.max_images);
                    
                    if (use_backup) {
                        std::cout << "Imagen guardada en respaldo: " << filename << std::endl;
                    } else {
                        std::cout << "Imagen guardada en principal: " << filename << std::endl;
                    }
                }
            } catch (...) {
                std::cout << "Error al guardar la imagen" << std::endl;
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return result_json.dump();
}