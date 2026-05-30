#define NOMINMAX 
#include "detector.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <dxgi.h> 

#pragma comment(lib, "dxgi.lib")

inline float CalculateIoU(const Detection& a, const Detection& b) {
    float x1 = (std::max)(a.box.x, b.box.x);
    float y1 = (std::max)(a.box.y, b.box.y);
    float x2 = (std::min)(a.box.x + a.box.w, b.box.x + b.box.w);
    float y2 = (std::min)(a.box.y + a.box.h, b.box.y + b.box.h);
    if (x2 < x1 || y2 < y1) return 0.0f;
    float intersection = (x2 - x1) * (y2 - y1);
    return intersection / (a.box.w * a.box.h + b.box.w * b.box.h - intersection);
}

void PreprocessDirect(const unsigned char* src, std::vector<float>& dst, int w, int h) {
    int channel_size = w * h;
    float* r_ptr = dst.data();
    float* g_ptr = dst.data() + channel_size;
    float* b_ptr = dst.data() + channel_size * 2;
    const float inv255 = 0.003921568f;
    for (int i = 0; i < channel_size; ++i) {
        int src_idx = i * 4;
        r_ptr[i] = src[src_idx + 2] * inv255;
        g_ptr[i] = src[src_idx + 1] * inv255;
        b_ptr[i] = src[src_idx + 0] * inv255;
    }
}

Detector::Detector() {}
Detector::~Detector() {}

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
    float /*nms_threshold*/, int max_det, bool elite_smoke_vision) {

    std::vector<Detection> final_results;
    if (!session || w != model_width || h != model_height) return final_results;

    float actual_body_thr = elite_smoke_vision ? (body_conf_threshold * 0.75f) : body_conf_threshold;
    float actual_head_thr = elite_smoke_vision ? (head_conf_threshold * 0.75f) : head_conf_threshold;
    if (actual_body_thr < 0.1f) actual_body_thr = 0.1f;
    if (actual_head_thr < 0.1f) actual_head_thr = 0.1f;

    try {
        PreprocessDirect(pixel_data, m_input_tensor_data, model_width, model_height);

        std::vector<int64_t> input_shape = { 1, 3, model_height, model_width };
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, m_input_tensor_data.data(),
            m_input_tensor_data.size(), input_shape.data(), input_shape.size());

        auto output_tensors = session->Run(Ort::RunOptions{ nullptr }, input_names.data(), &input_tensor, 1, output_names.data(), 1);

        float* data = output_tensors[0].GetTensorMutableData<float>();
        auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

        if (output_shape.size() != 3) return final_results;

        int64_t dim1 = output_shape[1];
        int64_t dim2 = output_shape[2];
        int num_detections, stride;
        bool transposed = false;
        if (dim2 == 6) {
            num_detections = (int)dim1;
            stride = (int)dim2;
            transposed = false;
        }
        else if (dim1 == 6) {
            num_detections = (int)dim2;
            stride = (int)dim1;
            transposed = true;
        }
        else {
            return final_results;
        }

        final_results.reserve(num_detections);
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
                cls_id = (int)std::round(p[i * stride + 5]);
            }
            else {
                x1 = p[i + 0 * num_detections];
                y1 = p[i + 1 * num_detections];
                x2 = p[i + 2 * num_detections];
                y2 = p[i + 3 * num_detections];
                conf = p[i + 4 * num_detections];
                cls_id = (int)std::round(p[i + 5 * num_detections]);
            }

            float thr = (cls_id == 0) ? actual_body_thr : (cls_id == 1) ? actual_head_thr : actual_body_thr;
            if (conf < thr) continue;

            float bw = x2 - x1;
            float bh = y2 - y1;
            if (bw < 2.0f || bh < 2.0f) continue;

            Detection det;
            det.class_id = cls_id;
            det.confidence = conf;
            det.box.x = x1;
            det.box.y = y1;
            det.box.w = bw;
            det.box.h = bh;
            det.track_id = -1;
            final_results.push_back(det);

            if ((int)final_results.size() >= max_det) break;
        }
    }
    catch (...) {}
    return final_results;
}