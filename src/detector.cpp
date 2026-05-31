#define NOMINMAX 
#include "detector.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <dxgi.h> 
#include <limits>
#include <chrono>

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

    std::sort(detections.begin(), detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        });

    std::vector<bool> suppress(detections.size(), false);
    std::vector<Detection> result;
    result.reserve(detections.size());

    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppress[i]) continue;
        result.push_back(detections[i]);

        const float area_i = detections[i].box.w * detections[i].box.h;
        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (suppress[j]) continue;

            float x1 = (std::max)(detections[i].box.x, detections[j].box.x);
            float y1 = (std::max)(detections[i].box.y, detections[j].box.y);
            float x2 = (std::min)(detections[i].box.x + detections[i].box.w, detections[j].box.x + detections[j].box.w);
            float y2 = (std::min)(detections[i].box.y + detections[i].box.h, detections[j].box.y + detections[j].box.h);

            if (x2 > x1 && y2 > y1) {
                float intersection = (x2 - x1) * (y2 - y1);
                float union_area = area_i + detections[j].box.w * detections[j].box.h - intersection;
                if (intersection / union_area > nms_threshold) {
                    suppress[j] = true;
                }
            }
        }
    }

    detections = std::move(result);
    if (nms_time) *nms_time = std::chrono::steady_clock::now() - t0;
}

// ============================================================================
// ПРЕПРОЦЕССИНГ (прямая конвертация BGRA → CHW float [0..1])
// ============================================================================
void PreprocessDirect(const unsigned char* src, std::vector<float>& dst, int w, int h) {
    int channel_size = w * h;
    float* r_ptr = dst.data();
    float* g_ptr = dst.data() + channel_size;
    float* b_ptr = dst.data() + channel_size * 2;
    const float inv255 = 0.003921568f;
    
    // Входное изображение в формате BGRA (4 канала)
    // Конвертируем в RGB планарный формат (3 канала, float нормализованный)
    for (int i = 0; i < channel_size; ++i) {
        int src_idx = i * 4;
        r_ptr[i] = src[src_idx + 2] * inv255;  // B -> R
        g_ptr[i] = src[src_idx + 1] * inv255;  // G -> G
        b_ptr[i] = src[src_idx + 0] * inv255;  // R -> B
    }
}

Detector::Detector() {}
Detector::~Detector() {
    // Освобождаем память, выделенную через _strdup
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
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        const OrtApi& ort_api = Ort::GetApi();
        const OrtDmlApi* dml_api = nullptr;
        if (ort_api.GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml_api)) == nullptr && dml_api != nullptr) {
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
                IDXGIAdapter1* adapter = nullptr;
                IDXGIAdapter1* bestAdapter = nullptr;
                SIZE_T maxVRAM = 0;
                int bestAdapterIndex = 0;
                for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);
                    if (desc.DedicatedVideoMemory > maxVRAM) {
                        maxVRAM = desc.DedicatedVideoMemory;
                        if (bestAdapter) bestAdapter->Release();
                        bestAdapter = adapter;
                        bestAdapterIndex = i;
                    }
                    else {
                        adapter->Release();
                    }
                }
                if (bestAdapter) {
                    dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, bestAdapterIndex);
                    bestAdapter->Release();
                }
                else {
                    dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, 0);
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
        if (force_w > 0 && force_h > 0) {
            model_width = force_w;
            model_height = force_h;
        }
        else if (input_shape.size() >= 4 && input_shape[2] != -1 && input_shape[3] != -1) {
            model_height = (int)input_shape[2];
            model_width = (int)input_shape[3];
        }

        m_input_tensor_data.resize(3 * model_width * model_height);
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

std::vector<Detection> Detector::run_inference(const unsigned char* pixel_data, int w, int h,
    float body_conf_threshold, float head_conf_threshold,
    float nms_threshold, int max_det, bool elite_smoke_vision) {

    std::vector<Detection> final_results;
    if (!session) return final_results;

    // Проверка размера изображения (должно совпадать с размером модели)
    if (w != model_width || h != model_height) {
        std::cerr << "[Detector] Size mismatch: input " << w << "x" << h 
                  << ", model " << model_width << "x" << model_height << std::endl;
        return final_results;
    }

    // Применяем снижение порога для Elite Smoke Vision
    float actual_body_thr = elite_smoke_vision ? (body_conf_threshold * 0.75f) : body_conf_threshold;
    float actual_head_thr = elite_smoke_vision ? (head_conf_threshold * 0.75f) : head_conf_threshold;
    if (actual_body_thr < 0.1f) actual_body_thr = 0.1f;
    if (actual_head_thr < 0.1f) actual_head_thr = 0.1f;

    try {
        auto t0 = std::chrono::steady_clock::now();

        // ========================================================================
        // 1. ПРЕПРОЦЕССИНГ: BGRA -> RGB Planar Float32 [0..1]
        // ========================================================================
        PreprocessDirect(pixel_data, m_input_tensor_data, model_width, model_height);
        auto t1 = std::chrono::steady_clock::now();

        // ========================================================================
        // 2. ПОДГОТОВКА ТЕНЗОРА И ЗАПУСК ИНФЕРЕНСА
        // ========================================================================
        std::vector<int64_t> input_shape = { 1, 3, model_height, model_width };
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, 
            m_input_tensor_data.data(),
            m_input_tensor_data.size(), 
            input_shape.data(), 
            input_shape.size());

        auto output_tensors = session->Run(
            Ort::RunOptions{ nullptr }, 
            input_names.data(), 
            &input_tensor, 1, 
            output_names.data(), 1);

        auto t2 = std::chrono::steady_clock::now();

        // ========================================================================
        // 3. ПОСТПРОЦЕССИНГ: Извлечение детектов из выходного тензора
        // ========================================================================
        float* data = output_tensors[0].GetTensorMutableData<float>();
        auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

        // Ожидаем формат [1, rows, cols] или [1, 6, num_dets]
        if (output_shape.size() != 3) {
            std::cerr << "[Detector] Unexpected output shape size: " << output_shape.size() << std::endl;
            return final_results;
        }

        int64_t batch_dim = output_shape[0];
        int64_t dim1 = output_shape[1];
        int64_t dim2 = output_shape[2];

        if (batch_dim != 1) {
            std::cerr << "[Detector] Expected batch=1, got " << batch_dim << std::endl;
            return final_results;
        }

        // Определяем формат выхода
        // Формат 1: [1, num_dets, 6] - каждый детект: [x1, y1, x2, y2, conf, class]
        // Формат 2: [1, 6, num_dets] - транспонированный
        int num_detections = 0;
        int stride = 0;
        bool transposed = false;

        if (dim2 == 6) {
            // Формат [1, num_dets, 6]
            num_detections = static_cast<int>(dim1);
            stride = 6;
            transposed = false;
        }
        else if (dim1 == 6) {
            // Формат [1, 6, num_dets] (транспонированный)
            num_detections = static_cast<int>(dim2);
            stride = 6;
            transposed = true;
        }
        else {
            std::cerr << "[Detector] Unknown output format: [" << dim1 << ", " << dim2 << "]" << std::endl;
            return final_results;
        }

        final_results.reserve(std::min(num_detections, 1024));

        const float* p = data;
        for (int i = 0; i < num_detections; ++i) {
            float x1, y1, x2, y2, conf;
            int cls_id;

            if (!transposed) {
                // Формат [num_dets, 6]: x1, y1, x2, y2, conf, class
                x1 = p[i * stride + 0];
                y1 = p[i * stride + 1];
                x2 = p[i * stride + 2];
                y2 = p[i * stride + 3];
                conf = p[i * stride + 4];
                cls_id = static_cast<int>(std::round(p[i * stride + 5]));
            }
            else {
                // Формат [6, num_dets]: транспонированный
                x1 = p[i + 0 * num_detections];
                y1 = p[i + 1 * num_detections];
                x2 = p[i + 2 * num_detections];
                y2 = p[i + 3 * num_detections];
                conf = p[i + 4 * num_detections];
                cls_id = static_cast<int>(std::round(p[i + 5 * num_detections]));
            }

            // Выбор порога в зависимости от класса (0=body, 1=head)
            float thr = (cls_id == 1) ? actual_head_thr : actual_body_thr;
            
            // Фильтрация по уверенности
            if (conf < thr) continue;

            // Вычисляем ширину и высоту
            float bw = x2 - x1;
            float bh = y2 - y1;

            // Фильтрация слишком мелких детектов
            if (bw < 2.0f || bh < 2.0f) continue;

            // Создаем детект
            Detection det;
            det.class_id = cls_id;
            det.confidence = conf;
            det.box.x = x1;
            det.box.y = y1;
            det.box.w = bw;
            det.box.h = bh;
            det.track_id = -1;  // Будет назначен ByteTrack'ом
            final_results.push_back(det);
        }

        auto t3 = std::chrono::steady_clock::now();

        // ========================================================================
        // 4. NMS (Non-Maximum Suppression)
        // ========================================================================
        std::chrono::duration<double, std::milli> nms_time;
        NMS_Improved(final_results, nms_threshold, &nms_time);
        auto t4 = std::chrono::steady_clock::now();

        // ========================================================================
        // 5. Ограничение количества детектов
        // ========================================================================
        if (static_cast<int>(final_results.size()) > max_det) {
            final_results.resize(max_det);
        }

        // Вывод статистики по времени (для отладки)
        #ifdef _DEBUG
        double preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double inference_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        double postprocess_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
        double nms_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();
        double total_ms = std::chrono::duration<double, std::milli>(t4 - t0).count();
        
        static int frame_count = 0;
        if (++frame_count % 30 == 0) {
            std::cout << "[Detector Timing] Total: " << total_ms << "ms | "
                      << "Preproc: " << preprocess_ms << "ms | "
                      << "Infer: " << inference_ms << "ms | "
                      << "Post: " << postprocess_ms << "ms | "
                      << "NMS: " << nms_ms << "ms | "
                      << "Dets: " << final_results.size() << std::endl;
        }
        #endif

    }
    catch (const Ort::Exception& e) {
        std::cerr << "[Detector] ONNX Exception: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[Detector] Exception: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "[Detector] Unknown exception" << std::endl;
    }

    return final_results;
}