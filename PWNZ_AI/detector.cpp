#define NOMINMAX 
#include "detector.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <dxgi.h> 
#include <limits>
#include <chrono>
#include <span>
#include <cstring>

#pragma comment(lib, "dxgi.lib")

// ============================================================================
// СТРУКТУРЫ И ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================

struct DetectionExt {
    int class_id;
    float confidence;
    struct { float x, y, w, h; } box;
    int track_id = -1;
};

inline float CalculateIoU(const Detection& a, const Detection& b) {
    float x1 = (std::max)(a.box.x, b.box.x);
    float y1 = (std::max)(a.box.y, b.box.y);
    float x2 = (std::min)(a.box.x + a.box.w, b.box.x + b.box.w);
    float y2 = (std::min)(a.box.y + a.box.h, b.box.y + b.box.h);
    if (x2 < x1 || y2 < y1) return 0.0f;
    float intersection = (x2 - x1) * (y2 - y1);
    return intersection / (a.box.w * a.box.h + b.box.w * b.box.h - intersection);
}

// ============================================================================
// NMS (из source_logic/postProcess.cpp, адаптированный без OpenCV)
// ============================================================================
void NMS_Improved(std::vector<Detection>& detections, float nms_threshold, std::chrono::duration<double, std::milli>* nms_time = nullptr) {
    if (detections.empty() || nms_threshold <= 0.0f) {
        if (nms_time) *nms_time = std::chrono::duration<double, std::milli>(0);
        return;
    }

    auto t0 = std::chrono::steady_clock::now();

    const float pre_nms_conf_thresh = 0.15f;
    detections.erase(
        std::remove_if(detections.begin(), detections.end(),
            [pre_nms_conf_thresh](const Detection& d) { return d.confidence < pre_nms_conf_thresh; }),
        detections.end());

    if (detections.empty()) {
        if (nms_time) *nms_time = std::chrono::duration<double, std::milli>(0);
        return;
    }

    std::sort(detections.begin(), detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        });

    std::vector<bool> suppress(detections.size(), false);
    std::vector<Detection> result;
    result.reserve((std::min)(detections.size(), static_cast<size_t>(256)));

    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppress[i]) continue;
        result.push_back(detections[i]);

        const float area_i = detections[i].box.w * detections[i].box.h;

        if (result.size() >= 100) break;

        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (suppress[j]) continue;

            float dx = (std::max)(detections[i].box.x, detections[j].box.x) -
                (std::min)(detections[i].box.x + detections[i].box.w, detections[j].box.x + detections[j].box.w);
            float dy = (std::max)(detections[i].box.y, detections[j].box.y) -
                (std::min)(detections[i].box.y + detections[i].box.h, detections[j].box.y + detections[j].box.h);

            if (dx >= 0.0f || dy >= 0.0f) continue;

            float intersection = (-dx) * (-dy);
            float union_area = area_i + detections[j].box.w * detections[j].box.h - intersection;
            if (intersection / union_area > nms_threshold) {
                suppress[j] = true;
            }
        }
    }

    detections = std::move(result);
    if (nms_time) *nms_time = std::chrono::steady_clock::now() - t0;
}

// ============================================================================
// ПРЕПРОЦЕССИНГ
// ============================================================================
void PreprocessDirect(std::span<const unsigned char> src, std::vector<float>& dst, int w, int h) {
    int channel_size = w * h;
    float* r_ptr = dst.data();
    float* g_ptr = dst.data() + channel_size;
    float* b_ptr = dst.data() + channel_size * 2;
    const float inv255 = 0.003921568f;

    const std::size_t required_size = static_cast<std::size_t>(w) * h * 4;
    if (src.size_bytes() < required_size) {
        return;
    }

#pragma omp parallel for num_threads(4)
    for (int i = 0; i < channel_size; ++i) {
        int src_idx = i * 4;
        r_ptr[i] = src[src_idx + 2] * inv255;  // B -> R
        g_ptr[i] = src[src_idx + 1] * inv255;  // G -> G
        b_ptr[i] = src[src_idx + 0] * inv255;  // R -> B
    }
}

Detector::Detector() {}
Detector::~Detector() {
    for (auto* name : input_names) {
        if (name) free((void*)name);
    }
    for (auto* name : output_names) {
        if (name) free((void*)name);
    }
    input_names.clear();
    output_names.clear();
}

bool Detector::initialize(const std::string& model_path, int force_w, int force_h) {
    try {
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "BogX_Engine");
        session_options = Ort::SessionOptions();

        // Оптимизация потоков для CPU
        session_options.SetIntraOpNumThreads(4);
        session_options.SetInterOpNumThreads(4);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        // Отключаем паттерны памяти для DirectML
        session_options.DisableMemPattern();

        const OrtApi& ort_api = Ort::GetApi();
        const OrtDmlApi* dml_api = nullptr;

        if (ort_api.GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml_api)) == nullptr) {
            std::cout << "[Detector] DirectML not available, using CPU execution provider" << std::endl;
        }
        else if (dml_api != nullptr) {
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
                IDXGIAdapter1* adapter = nullptr;
                IDXGIAdapter1* bestAdapter = nullptr;
                SIZE_T maxVRAM = 0;
                int bestAdapterIndex = -1;

                // Поиск лучшей видеокарты (NVIDIA)
                for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    // Исправленный флаг программного адаптера
                    bool isDiscrete = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;

                    bool isNVIDIA = (wcsstr(desc.Description, L"NVIDIA") != nullptr);
                    bool isAMD = (wcsstr(desc.Description, L"AMD") != nullptr || wcsstr(desc.Description, L"Radeon") != nullptr);
                    bool isIntel = (wcsstr(desc.Description, L"Intel") != nullptr);

                    if (isNVIDIA && desc.DedicatedVideoMemory > maxVRAM) {
                        maxVRAM = desc.DedicatedVideoMemory;
                        if (bestAdapter) bestAdapter->Release();
                        bestAdapter = adapter;
                        bestAdapterIndex = i;

                        // Исправленный вывод названия видеокарты (конвертация WCHAR в string)
                        std::wstring ws(desc.Description);
                        std::string desc_str(ws.begin(), ws.end());
                        std::cout << "[Detector] Found NVIDIA GPU: " << desc_str
                            << " (VRAM: " << (desc.DedicatedVideoMemory / (1024 * 1024)) << " MB)" << std::endl;
                    }
                    else if (!isNVIDIA && !isIntel && isDiscrete && desc.DedicatedVideoMemory > maxVRAM && bestAdapterIndex == -1) {
                        maxVRAM = desc.DedicatedVideoMemory;
                        if (bestAdapter) bestAdapter->Release();
                        bestAdapter = adapter;
                        bestAdapterIndex = i;
                    }
                    else {
                        adapter->Release();
                    }
                }

                if (bestAdapter && bestAdapterIndex >= 0) {
                    // Инициализация DML на выбранной видеокарте (без OrtDmlApiOptions)
                    dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, bestAdapterIndex);
                    std::cout << "[Detector] DirectML initialized on adapter index " << bestAdapterIndex << std::endl;
                    bestAdapter->Release();
                }
                else {
                    dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, 0);
                    std::cout << "[Detector] DirectML initialized on default adapter (index 0)" << std::endl;
                }
                factory->Release();
            }
            else {
                dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, 0);
            }
        }

        std::wstring w_model_path(model_path.begin(), model_path.end());
        session = std::make_unique<Ort::Session>(*env, w_model_path.c_str(), session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        input_names.push_back(_strdup(session->GetInputNameAllocated(0, allocator).get()));
        output_names.push_back(_strdup(session->GetOutputNameAllocated(0, allocator).get()));

        auto input_info = session->GetInputTypeInfo(0);
        auto input_shape = input_info.GetTensorTypeAndShapeInfo().GetShape();

        // Динамическое определение разрешения модели
        if (input_shape.size() >= 4 && input_shape[2] > 0 && input_shape[3] > 0) {
            // Модель имеет статические размеры - берем их из ONNX
            model_height = static_cast<int>(input_shape[2]);
            model_width = static_cast<int>(input_shape[3]);
        } else {
            // Модель имеет динамические оси (-1) - используем переданные значения из UI
            model_width = (force_w > 0) ? force_w : 640;
            model_height = (force_h > 0) ? force_h : 640;
        }

        m_input_tensor_data.resize(3 * model_width * model_height);
        m_resized_tensor_data.resize(3 * model_width * model_height);
        m_final_tensor_data.resize(3 * model_width * model_height);
        return true;
    }
    catch (const Ort::Exception& e) {
        std::cerr << "ONNX Error: " << e.what() << std::endl;
        return false;
    }
    catch (...) {
        return false;
    }
}

std::vector<Detection> Detector::run_inference(std::span<const unsigned char> pixel_data, int w, int h,
    float body_conf_threshold, float head_conf_threshold,
    float nms_threshold, int max_det, bool elite_smoke_vision) {

    std::vector<Detection> final_results;
    if (!session) return final_results;

    const std::size_t required_size = static_cast<std::size_t>(w) * h * 4;
    if (pixel_data.size_bytes() < required_size) {
        return final_results;
    }

    const int orig_w = w;
    const int orig_h = h;

    float actual_body_thr = elite_smoke_vision ? (body_conf_threshold * 0.75f) : body_conf_threshold;
    float actual_head_thr = elite_smoke_vision ? (head_conf_threshold * 0.75f) : head_conf_threshold;
    if (actual_body_thr < 0.1f) actual_body_thr = 0.1f;
    if (actual_head_thr < 0.1f) actual_head_thr = 0.1f;

    try {
        auto t0 = std::chrono::steady_clock::now();

        const float* preprocess_ptr = nullptr;

        if (w != model_width || h != model_height) {
            m_resized_tensor_data.resize(3 * model_width * model_height);

            const float x_ratio = static_cast<float>(w) / model_width;
            const float y_ratio = static_cast<float>(h) / model_height;
            const float inv255 = 0.003921568f;

            float* r_ptr = m_resized_tensor_data.data();
            float* g_ptr = m_resized_tensor_data.data() + (model_width * model_height);
            float* b_ptr = m_resized_tensor_data.data() + 2 * (model_width * model_height);

#pragma omp parallel for num_threads(4)
            for (int my = 0; my < model_height; ++my) {
                for (int mx = 0; mx < model_width; ++mx) {
                    const float ox = x_ratio * mx;
                    const float oy = y_ratio * my;

                    const int ox_int = static_cast<int>(ox);
                    const int oy_int = static_cast<int>(oy);
                    const float ox_frac = ox - ox_int;
                    const float oy_frac = oy - oy_int;

                    const int x0 = (std::min)(ox_int, w - 2);
                    const int y0 = (std::min)(oy_int, h - 2);
                    const int x1 = x0 + 1;
                    const int y1 = y0 + 1;

                    const int idx00 = (y0 * w + x0) * 4;
                    const int idx01 = (y0 * w + x1) * 4;
                    const int idx10 = (y1 * w + x0) * 4;
                    const int idx11 = (y1 * w + x1) * 4;

                    for (int c = 0; c < 3; ++c) {
                        const float v00 = pixel_data[idx00 + (2 - c)];
                        const float v01 = pixel_data[idx01 + (2 - c)];
                        const float v10 = pixel_data[idx10 + (2 - c)];
                        const float v11 = pixel_data[idx11 + (2 - c)];

                        const float v0 = v00 * (1.0f - ox_frac) + v01 * ox_frac;
                        const float v1 = v10 * (1.0f - ox_frac) + v11 * ox_frac;
                        const float v = v0 * (1.0f - oy_frac) + v1 * oy_frac;

                        const int dst_idx = my * model_width + mx;
                        if (c == 0) r_ptr[dst_idx] = v * inv255;
                        else if (c == 1) g_ptr[dst_idx] = v * inv255;
                        else b_ptr[dst_idx] = v * inv255;
                    }
                }
            }
            preprocess_ptr = m_resized_tensor_data.data();
        }
        else {
            PreprocessDirect(pixel_data, m_input_tensor_data, model_width, model_height);
            preprocess_ptr = m_input_tensor_data.data();
        }

        if (preprocess_ptr == m_input_tensor_data.data()) {
            m_final_tensor_data = m_input_tensor_data;
        }
        else {
            m_final_tensor_data.assign(m_resized_tensor_data.begin(), m_resized_tensor_data.end());
        }

        std::vector<int64_t> input_shape = { 1, 3, model_height, model_width };
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            m_final_tensor_data.data(),
            3 * model_width * model_height,
            input_shape.data(),
            input_shape.size());

        auto output_tensors = session->Run(
            Ort::RunOptions{ nullptr },
            input_names.data(),
            &input_tensor, 1,
            output_names.data(), 1);

        float* data = output_tensors[0].GetTensorMutableData<float>();
        auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

        if (output_shape.size() != 3) {
            std::cerr << "[Detector] Unexpected output shape size: " << output_shape.size() << std::endl;
            return final_results;
        }

        int64_t batch_dim = output_shape[0];
        int64_t dim1 = output_shape[1];
        int64_t dim2 = output_shape[2];

        if (batch_dim != 1) return final_results;

        int num_detections = 0;
        int stride = 0;
        bool transposed = false;

        if (dim2 == 6) {
            num_detections = static_cast<int>(dim1);
            stride = 6;
            transposed = false;
        }
        else if (dim1 == 6) {
            num_detections = static_cast<int>(dim2);
            stride = 6;
            transposed = true;
        }
        else {
            return final_results;
        }

        final_results.reserve((std::min)(num_detections, 1024));

        const float* p = data;
        for (int i = 0; i < num_detections; ++i) {
            float x1, y1, x2, y2, conf;
            int cls_id;

            if (!transposed) {
                x1 = p[i * stride + 0];
                y1 = p[i * stride + 1];
                x2 = p[i * stride + 2];
                y2 = p[i * stride + 3];
                conf = p[i * stride + 4];
                cls_id = static_cast<int>(std::round(p[i * stride + 5]));
            }
            else {
                x1 = p[i + 0 * num_detections];
                y1 = p[i + 1 * num_detections];
                x2 = p[i + 2 * num_detections];
                y2 = p[i + 3 * num_detections];
                conf = p[i + 4 * num_detections];
                cls_id = static_cast<int>(std::round(p[i + 5 * num_detections]));
            }

            float thr = (cls_id == 1) ? actual_head_thr : actual_body_thr;
            if (conf < thr) continue;

            float bw = x2 - x1;
            float bh = y2 - y1;
            if (bw < 2.0f || bh < 2.0f) continue;

            const float scale_x = static_cast<float>(orig_w) / model_width;
            const float scale_y = static_cast<float>(orig_h) / model_height;

            Detection det;
            det.class_id = cls_id;
            det.confidence = conf;
            det.box.x = x1 * scale_x;
            det.box.y = y1 * scale_y;
            det.box.w = bw * scale_x;
            det.box.h = bh * scale_y;
            det.track_id = -1;
            final_results.push_back(det);
        }

        std::chrono::duration<double, std::milli> nms_time;
        NMS_Improved(final_results, nms_threshold, &nms_time);

        if (static_cast<int>(final_results.size()) > max_det) {
            final_results.resize(max_det);
        }
    }
    catch (const Ort::Exception& e) {
        std::cerr << "[Detector] ONNX Exception: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "[Detector] Unknown exception" << std::endl;
    }

    return final_results;
}
