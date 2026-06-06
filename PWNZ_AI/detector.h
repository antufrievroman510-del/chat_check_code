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

// Тайминги одного кадра (для отладки и UI)
struct FrameTimings {
    float preprocess_ms = 0.f;
    float inference_ms = 0.f;
    float nms_ms = 0.f;
    float total_ms = 0.f;
};

class Detector {
public:
    Detector();
    ~Detector();

    bool initialize(const std::string& model_path,
        int force_w = 0, int force_h = 0,
        int gpu_index = 0);

    std::vector<Detection> run_inference(
        std::span<const unsigned char> pixel_data,
        int w, int h,
        float body_conf_threshold,
        float head_conf_threshold,
        float nms_threshold,
        int   max_det,
        bool  elite_smoke_vision = false,
        FrameTimings* out_timings = nullptr);

    int get_width()  const { return model_width; }
    int get_height() const { return model_height; }
    bool is_fp16()   const { return m_is_fp16; }

private:
    std::unique_ptr<Ort::Env>     env;
    Ort::SessionOptions           session_options;
    std::unique_ptr<Ort::Session> session;

    std::vector<const char*> input_names;
    std::vector<const char*> output_names;

    // Буферы preprocessing (float32 — всегда, независимо от модели)
    std::vector<float>           m_preprocess_buf;   // BGR→RGB float, CHW
    // Буфер входа для модели: float16 если m_is_fp16, иначе используем m_preprocess_buf напрямую
    std::vector<Ort::Float16_t>  m_fp16_input_buf;

    bool m_is_fp16 = false;
    int  model_width = 736;
    int  model_height = 416;
};
