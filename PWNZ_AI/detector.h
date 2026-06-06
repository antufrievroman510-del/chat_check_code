#pragma once
#include <vector>
#include <string>
#include <memory>
#include <span>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

struct Detection {
    int class_id;
    float confidence;
    struct { float x, y, w, h; } box;
    int track_id = -1;
};

class Detector {
public:
    Detector();
    ~Detector();
    bool initialize(const std::string& model_path, int force_w = 0, int force_h = 0, int gpu_index = 0);
    std::vector<Detection> run_inference(std::span<const unsigned char> pixel_data, int w, int h,
        float body_conf_threshold, float head_conf_threshold,
        float nms_threshold, int max_det,
        bool elite_smoke_vision = false);

    int get_width() const { return model_width; }
    int get_height() const { return model_height; }

private:
    std::unique_ptr<Ort::Env> env;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;

    std::vector<const char*> input_names;
    std::vector<const char*> output_names;
    std::vector<float> m_input_tensor_data;
    std::vector<float> m_resized_tensor_data;
    std::vector<float> m_final_tensor_data;

    int model_width = 736;   // Стандартное значение YOLO для оптимальной производительности
    int model_height = 416;  // Стандартное значение YOLO для оптимальной производительности
};