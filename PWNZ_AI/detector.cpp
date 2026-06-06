#define NOMINMAX
#include "detector.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>
#include <chrono>
#include <span>
#include <cstring>
#include <windows.h>

#pragma comment(lib, "dxgi.lib")

// ============================================================================
// Определение размеров моделей BogX
// ============================================================================
static void GetModelResolutionFromName(const std::string& model_path, int& out_w, int& out_h) {
    std::string model_name = model_path;
    size_t pos = model_name.find_last_of("/\\");
    if (pos != std::string::npos) model_name = model_name.substr(pos + 1);
    
    // Удаляем _fp16 из имени для корректного определения
    size_t fp16_pos = model_name.find("_fp16");
    if (fp16_pos != std::string::npos) {
        model_name = model_name.substr(0, fp16_pos) + ".onnx";
    }
    
    std::cout << "[Detector] Parsing model name: " << model_name << std::endl;
    
    // BogX-Nano.onnx / BogX-Pro.onnx: 512x288
    // BogX-Lite.onnx / BogX-Ultra.onnx: 736x416
    if (model_name.find("Nano") != std::string::npos || 
        model_name.find("Pro") != std::string::npos) {
        out_w = 512;
        out_h = 288;
        std::cout << "[Detector] Detected Nano/Pro model: 512x288" << std::endl;
    }
    else if (model_name.find("Lite") != std::string::npos || 
             model_name.find("Ultra") != std::string::npos) {
        out_w = 736;
        out_h = 416;
        std::cout << "[Detector] Detected Lite/Ultra model: 736x416" << std::endl;
    }
    else {
        // Неизвестная модель — дефолт 640x640
        out_w = 640;
        out_h = 640;
        std::cout << "[Detector] Unknown model, using default: 640x640" << std::endl;
    }
}

// ============================================================================
// NMS (Non-Maximum Suppression)
// ============================================================================
static void NMS_Improved(std::vector<Detection>& dets, float nms_threshold, float* nms_ms_out) {
    if (dets.empty() || nms_threshold <= 0.f) {
        if (nms_ms_out) *nms_ms_out = 0.f;
        return;
    }

    auto t0 = std::chrono::steady_clock::now();

    // Быстрая фильтрация мусора до тяжелой сортировки O(N^2)
    constexpr float PRE_THRESH = 0.15f;
    dets.erase(std::remove_if(dets.begin(), dets.end(),
        [](const Detection& d) { return d.confidence < PRE_THRESH; }), dets.end());

    if (dets.empty()) {
        if (nms_ms_out) *nms_ms_out = 0.f;
        return;
    }

    std::sort(dets.begin(), dets.end(),
        [](const Detection& a, const Detection& b) { return a.confidence > b.confidence; });

    std::vector<bool> suppress(dets.size(), false);
    std::vector<Detection> result;
    result.reserve((std::min)(dets.size(), static_cast<size_t>(256)));

    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppress[i]) continue;
        result.push_back(dets[i]);

        // Ранний выход, если собрали достаточно детектов
        if (result.size() >= 100) break;

        const float area_i = dets[i].box.w * dets[i].box.h;
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (suppress[j]) continue;

            float dx = (std::max)(dets[i].box.x, dets[j].box.x) -
                (std::min)(dets[i].box.x + dets[i].box.w, dets[j].box.x + dets[j].box.w);
            float dy = (std::max)(dets[i].box.y, dets[j].box.y) -
                (std::min)(dets[i].box.y + dets[i].box.h, dets[j].box.y + dets[j].box.h);

            if (dx >= 0.f || dy >= 0.f) continue; // Не пересекаются

            float inter = (-dx) * (-dy);
            float uni = area_i + dets[j].box.w * dets[j].box.h - inter;
            if (inter / uni > nms_threshold) suppress[j] = true;
        }
    }

    dets = std::move(result);
    if (nms_ms_out) {
        *nms_ms_out = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    }
}

// ============================================================================
// КОНСТРУКТОР / ДЕСТРУКТОР
// ============================================================================
Detector::Detector() {}
Detector::~Detector() {
    for (auto* n : input_names)  if (n) free((void*)n);
    for (auto* n : output_names) if (n) free((void*)n);
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ ONNX И DIRECTML
// ============================================================================
bool Detector::initialize(const std::string& model_path, int force_w, int force_h, int gpu_index) {
    try {
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "BogX_Engine");
        session_options = Ort::SessionOptions();

        // Максимальная оптимизация под DirectML (1 поток, так как DML сам нагружает GPU)
        session_options.SetIntraOpNumThreads(1);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        session_options.DisableMemPattern();    // Обязательно для DirectML

        const OrtApi& ort_api = Ort::GetApi();
        const OrtDmlApi* dml_api = nullptr;
        if (ort_api.GetExecutionProviderApi("DML", ORT_API_VERSION,
            reinterpret_cast<const void**>(&dml_api)) == nullptr) {
            std::cout << "[Detector] DirectML API available" << std::endl;
        }
        else {
            std::cerr << "[Detector] WARNING: DirectML API not available!" << std::endl;
        }
        
        if (dml_api) {
            dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, gpu_index);
            std::cout << "[Detector] DirectML successfully attached to GPU index: " << gpu_index << std::endl;
        }
        else {
            std::cerr << "[Detector] ERROR: DirectML provider NOT attached! Will use CPU (slow)." << std::endl;
        }

        std::wstring wpath(model_path.begin(), model_path.end());
        
        // Проверка существования файла модели
        if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::cerr << "[Detector] ERROR: Model file NOT FOUND: " << model_path << std::endl;
            return false;
        }
        
        std::wcout << L"[Detector] Loading model: " << wpath << std::endl;
        session = std::make_unique<Ort::Session>(*env, wpath.c_str(), session_options);
        
        if (!session) {
            std::cerr << "[Detector] ERROR: Session is null after loading!" << std::endl;
            return false;
        }
        
        std::cout << "[Detector] Model loaded successfully." << std::endl;

        Ort::AllocatorWithDefaultOptions alloc;
        input_names.push_back(_strdup(session->GetInputNameAllocated(0, alloc).get()));
        output_names.push_back(_strdup(session->GetOutputNameAllocated(0, alloc).get()));

        // Автоматическое определение размера модели
        // Приоритет 1: читаем из shape ONNX (если статический)
        // Приоритет 2: определяем по имени модели (для динамических shape)
        auto input_info = session->GetInputTypeInfo(0);
        auto input_shape = input_info.GetTensorTypeAndShapeInfo().GetShape();
        
        std::cout << "[Detector] Raw ONNX input shape: [";
        for (size_t i = 0; i < input_shape.size(); ++i) {
            std::cout << input_shape[i];
            if (i + 1 < input_shape.size()) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
        
        bool shape_valid = (input_shape.size() >= 4 && 
                           input_shape[2] > 0 && input_shape[2] < 5000 &&
                           input_shape[3] > 0 && input_shape[3] < 5000);
        
        if (shape_valid) {
            model_height = static_cast<int>(input_shape[2]);
            model_width = static_cast<int>(input_shape[3]);
            std::cout << "[Detector] Model resolution from ONNX shape: " 
                      << model_width << "x" << model_height << std::endl;
        }
        else {
            // Shape содержит -1 (динамический) или некорректен — определяем по имени модели
            GetModelResolutionFromName(model_path, model_width, model_height);
            std::cout << "[Detector] Model resolution from filename: " 
                      << model_width << "x" << model_height << std::endl;
        }

        // Предварительное выделение памяти (избегаем аллокаций в цикле)
        const int n = 3 * model_width * model_height;
        m_preprocess_buf.resize(n);

        std::cout << "[Detector] Loaded Model: " << model_width << "x" << model_height << std::endl;
        return true;
    }
    catch (const Ort::Exception& e) { std::cerr << "ONNX Error: " << e.what() << std::endl; }
    catch (...) { std::cerr << "[Detector] Unknown exception during initialization" << std::endl; }
    return false;
}

// ============================================================================
// ИНФЕРЕНС (ОСНОВНОЙ ЦИКЛ)
// ============================================================================
std::vector<Detection> Detector::run_inference(
    std::span<const unsigned char> pixel_data,
    int w, int h,
    float body_conf_threshold,
    float head_conf_threshold,
    float nms_threshold,
    int   max_det,
    bool  elite_smoke_vision,
    FrameTimings* out_timings)
{
    std::vector<Detection> results;
    if (!session) return results;

    const size_t required = static_cast<size_t>(w) * h * 4;
    if (pixel_data.size_bytes() < required) return results;

    // ZERO-RESIZE: Если разрешение ROI не совпадает с моделью — инференс запрещён.
    // Разрешение захвата должно строго соответствовать разрешению модели.
    if (w != model_width || h != model_height) {
        static int mismatch_log_count = 0;
        if (mismatch_log_count < 10) {
            std::cerr << "[Detector] Resolution MISMATCH: ROI=" << w << "x" << h
                      << " Model=" << model_width << "x" << model_height
                      << ". Skipping inference (zero-resize policy)." << std::endl;
            mismatch_log_count++;
        }
        return results;
    }
    
    // Логирование первого кадра для отладки
    static int first_frame_log = 0;
    if (first_frame_log < 3) {
        std::cout << "[Detector] Running inference #" << (first_frame_log + 1) 
                  << " with ROI=" << w << "x" << h << std::endl;
        first_frame_log++;
    }

    float actual_body_thr = elite_smoke_vision ? (body_conf_threshold * 0.75f) : body_conf_threshold;
    float actual_head_thr = elite_smoke_vision ? (head_conf_threshold * 0.75f) : head_conf_threshold;
    actual_body_thr = (std::max)(actual_body_thr, 0.1f);
    actual_head_thr = (std::max)(actual_head_thr, 0.1f);

    float t_pre = 0.f, t_inf = 0.f, t_nms = 0.f;

    try {
        // ── 1. PREPROCESSING ───────────────────────────
        auto t_pre_start = std::chrono::steady_clock::now();

        float* r_ptr = m_preprocess_buf.data();
        float* g_ptr = m_preprocess_buf.data() + model_width * model_height;
        float* b_ptr = m_preprocess_buf.data() + 2 * model_width * model_height;
        constexpr float inv255 = 1.f / 255.f;

        // Быстрое копирование + нормализация BGR -> RGB (без ресайза!)
        const int n = model_width * model_height;
#pragma omp parallel for num_threads(4)
        for (int i = 0; i < n; ++i) {
            int s = i * 4;
            r_ptr[i] = pixel_data[s + 2] * inv255;
            g_ptr[i] = pixel_data[s + 1] * inv255;
            b_ptr[i] = pixel_data[s + 0] * inv255;
        }

        auto t_pre_end = std::chrono::steady_clock::now();
        t_pre = std::chrono::duration<float, std::milli>(t_pre_end - t_pre_start).count();

        // ── 2. СОЗДАНИЕ ТЕНЗОРА И ИНФЕРЕНС ───────────────────────────────────
        auto t_inf_start = std::chrono::steady_clock::now();

        std::vector<int64_t> input_shape = { 1, 3, model_height, model_width };
        const int64_t n_elems = 3LL * model_width * model_height;
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // ZERO-COPY: Передаем float напрямую через const_cast
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            const_cast<float*>(m_preprocess_buf.data()),
            static_cast<size_t>(n_elems),
            input_shape.data(),
            input_shape.size());

        std::vector<Ort::Value> output_tensors = session->Run(
            Ort::RunOptions{ nullptr },
            input_names.data(), &input_tensor, 1,
            output_names.data(), 1);

        auto t_inf_end = std::chrono::steady_clock::now();
        t_inf = std::chrono::duration<float, std::milli>(t_inf_end - t_inf_start).count();

        // ── 3. ПОСТОБРАБОТКА (Чтение данных с GPU) ───────────────────────────
        auto& out = output_tensors[0];
        auto  out_shape = out.GetTensorTypeAndShapeInfo().GetShape();
        if (out_shape.size() != 3 || out_shape[0] != 1) return results;

        int64_t dim1 = out_shape[1];
        int64_t dim2 = out_shape[2];
        int num_det = 0;
        bool transposed = false;

        if (dim2 == 6) { num_det = static_cast<int>(dim1); transposed = false; }
        else if (dim1 == 6) { num_det = static_cast<int>(dim2); transposed = true; }
        else { return results; }

        float* data = out.GetTensorMutableData<float>();
        results.reserve((std::min)(num_det, 1024));

        // scale_x и scale_y = 1.0f, так как ROI == model resolution
        const float scale_x = 1.0f;
        const float scale_y = 1.0f;

        for (int i = 0; i < num_det; ++i) {
            float x1, y1, x2, y2, conf;
            int   cls_id;

            if (!transposed) {
                x1 = data[i * 6 + 0];
                y1 = data[i * 6 + 1];
                x2 = data[i * 6 + 2];
                y2 = data[i * 6 + 3];
                conf = data[i * 6 + 4];
                cls_id = static_cast<int>(std::round(data[i * 6 + 5]));
            }
            else {
                x1 = data[i + 0 * num_det];
                y1 = data[i + 1 * num_det];
                x2 = data[i + 2 * num_det];
                y2 = data[i + 3 * num_det];
                conf = data[i + 4 * num_det];
                cls_id = static_cast<int>(std::round(data[i + 5 * num_det]));
            }

            float thr = (cls_id == 1) ? actual_head_thr : actual_body_thr;
            if (conf < thr) continue;

            float bw = x2 - x1, bh = y2 - y1;
            if (bw < 2.f || bh < 2.f) continue;

            Detection det;
            det.class_id = cls_id;
            det.confidence = conf;
            det.box.x = x1 * scale_x;
            det.box.y = y1 * scale_y;
            det.box.w = bw * scale_x;
            det.box.h = bh * scale_y;
            det.track_id = -1;
            results.push_back(det);
        }

        NMS_Improved(results, nms_threshold, &t_nms);

        if (static_cast<int>(results.size()) > max_det) {
            results.resize(max_det);
        }
    }
    catch (const Ort::Exception& e) { std::cerr << "[Detector] ORT: " << e.what() << std::endl; }
    catch (...) { std::cerr << "[Detector] Unknown exception" << std::endl; }

    // Логирование таймингов
    if (out_timings) {
        out_timings->preprocess_ms = t_pre;
        out_timings->inference_ms = t_inf;
        out_timings->nms_ms = t_nms;
        out_timings->total_ms = t_pre + t_inf + t_nms;
    }

    static int frame_cnt = 0;
    if (++frame_cnt % 60 == 0) {
        std::cout << "[Timings] PreProc=" << t_pre << "ms | Infer=" << t_inf
            << "ms | NMS=" << t_nms << "ms | Total=" << (t_pre + t_inf + t_nms) << "ms\n";
    }

    return results;
}
