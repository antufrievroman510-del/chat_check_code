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
                    std::wstring descStr(desc.Description);

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
        else {
            // Динамический shape - определяем по имени модели
            std::string model_name = model_path;
            size_t pos = model_name.find_last_of("/\\");
            if (pos != std::string::npos) model_name = model_name.substr(pos + 1);
            
            size_t fp16_pos = model_name.find("_fp16");
            if (fp16_pos != std::string::npos) {
                model_name = model_name.substr(0, fp16_pos) + ".onnx";
            }
            
            if (model_name.find("Nano") != std::string::npos || model_name.find("Pro") != std::string::npos) {
                model_width = 512;
                model_height = 288;
            }
            else if (model_name.find("Lite") != std::string::npos || model_name.find("Ultra") != std::string::npos) {
                model_width = 736;
                model_height = 416;
            }
        }

        m_input_tensor_data.resize(3 * model_width * model_height);
        
        std::cout << "[+] Detector initialized: " << model_width << "x" << model_height << std::endl;
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
    if (!session || w != model_width || h != model_height) return final_results;

    float actual_body_thr = elite_smoke_vision ? (body_conf_threshold * 0.75f) : body_conf_threshold;
    float actual_head_thr = elite_smoke_vision ? (head_conf_threshold * 0.75f) : head_conf_threshold;
    if (actual_body_thr < 0.1f) actual_body_thr = 0.1f;
    if (actual_head_thr < 0.1f) actual_head_thr = 0.1f;

    try {
        PreprocessDirect(pixel_data, m_input_tensor_data, model_width, model_height);

        std::vector<int64_t> input_shape = { 1, 3, model_height, model_width };
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, m_input_tensor_data.data(), m_input_tensor_data.size(), input_shape.data(), input_shape.size());

        auto output_tensors = session->Run(Ort::RunOptions{ nullptr }, input_names.data(), &input_tensor, 1, output_names.data(), 1);

        float* data = output_tensors[0].GetTensorMutableData<float>();
        auto type_info = output_tensors[0].GetTensorTypeAndShapeInfo();
        auto output_shape = type_info.GetShape();

        int num_channels = 0, num_anchors = 0;
        bool is_transposed = false;

        if (output_shape.size() == 3) {
            if (output_shape[1] > output_shape[2]) {
                num_channels = (int)output_shape[2];
                num_anchors = (int)output_shape[1];
                is_transposed = true;
            }
            else {
                num_channels = (int)output_shape[1];
                num_anchors = (int)output_shape[2];
            }
        }

        std::vector<Detection> raw_results;
        raw_results.reserve(200);

        bool is_yolo10_nms = false;
        if (is_transposed && num_channels == 6) {
            float frac_sum = 0.0f;
            int valid_count = 0;
            for (int i = 0; i < (std::min)(num_anchors, 20); ++i) {
                float val = data[i * num_channels + 5];
                frac_sum += std::abs(val - std::round(val));
                valid_count++;
            }
            if (valid_count > 0 && frac_sum < 0.01f) {
                is_yolo10_nms = true;
            }
        }

        bool is_yolov5 = (is_transposed && num_channels > 6);

        for (int i = 0; i < num_anchors; ++i) {
            if (is_yolo10_nms) {
                float conf = data[i * num_channels + 4];
                int cls_id = (int)std::round(data[i * num_channels + 5]);
                float thr = (cls_id == 0) ? actual_body_thr : (cls_id == 1) ? actual_head_thr : actual_body_thr;
                if (conf >= thr) {
                    float x1 = data[i * num_channels + 0];
                    float y1 = data[i * num_channels + 1];
                    float x2 = data[i * num_channels + 2];
                    float y2 = data[i * num_channels + 3];
                    float bw = x2 - x1;
                    float bh = y2 - y1;
                    if (bw > 2.0f && bh > 2.0f) {
                        raw_results.push_back({ cls_id, conf, { x1, y1, bw, bh } });
                    }
                }
            }
            else if (is_yolov5) {
                float obj_conf = data[i * num_channels + 4];
                if (obj_conf >= std::min(actual_body_thr, actual_head_thr)) {
                    float max_class_conf = 0.0f;
                    int best_class_id = -1;
                    for (int c = 0; c < num_channels - 5; ++c) {
                        float cls_conf = data[i * num_channels + 5 + c];
                        if (cls_conf > max_class_conf) {
                            max_class_conf = cls_conf;
                            best_class_id = c;
                        }
                    }
                    float final_conf = obj_conf * max_class_conf;
                    float thr = (best_class_id == 0) ? actual_body_thr : (best_class_id == 1) ? actual_head_thr : actual_body_thr;
                    if (final_conf >= thr) {
                        float cx = data[i * num_channels + 0];
                        float cy = data[i * num_channels + 1];
                        float dw = data[i * num_channels + 2];
                        float dh = data[i * num_channels + 3];
                        raw_results.push_back({ best_class_id, final_conf, { cx - dw / 2.0f, cy - dh / 2.0f, dw, dh } });
                    }
                }
            }
            else {
                float max_conf = 0.0f;
                int best_class_id = -1;
                int num_classes = num_channels - 4;

                for (int c = 0; c < num_classes; ++c) {
                    float conf = is_transposed ? data[i * num_channels + 4 + c] : data[(4 + c) * num_anchors + i];
                    if (conf > max_conf) {
                        max_conf = conf;
                        best_class_id = c;
                    }
                }

                float thr = (best_class_id == 0) ? actual_body_thr : (best_class_id == 1) ? actual_head_thr : actual_body_thr;
                if (max_conf >= thr) {
                    float cx = is_transposed ? data[i * num_channels + 0] : data[0 * num_anchors + i];
                    float cy = is_transposed ? data[i * num_channels + 1] : data[1 * num_anchors + i];
                    float dw = is_transposed ? data[i * num_channels + 2] : data[2 * num_anchors + i];
                    float dh = is_transposed ? data[i * num_channels + 3] : data[3 * num_anchors + i];

                    if (std::isnan(cx) || std::isnan(cy) || std::isnan(dw) || std::isnan(dh)) continue;
                    if (dw < 2.0f || dh < 2.0f) continue;

                    raw_results.push_back({ best_class_id, max_conf, { cx - dw / 2.0f, cy - dh / 2.0f, dw, dh } });
                }
            }
        }

        if (raw_results.empty()) return final_results;

        std::sort(raw_results.begin(), raw_results.end(), [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
            });

        std::vector<bool> is_suppressed(raw_results.size(), false);
        for (size_t i = 0; i < raw_results.size(); ++i) {
            if (is_suppressed[i]) continue;

            final_results.push_back(raw_results[i]);
            if (final_results.size() >= max_det) break;

            for (size_t j = i + 1; j < raw_results.size(); ++j) {
                if (!is_suppressed[j] && raw_results[i].class_id == raw_results[j].class_id) {
                    float iou = CalculateIoU(raw_results[i], raw_results[j]);
                    if (iou > nms_threshold) {
                        if (iou > 0.7f && std::abs(raw_results[i].box.w - raw_results[j].box.w) / raw_results[i].box.w < 0.3f) {
                            raw_results[i].confidence = (raw_results[i].confidence + raw_results[j].confidence) / 2.0f;
                            raw_results[i].box.x = (raw_results[i].box.x + raw_results[j].box.x) / 2.0f;
                            raw_results[i].box.y = (raw_results[i].box.y + raw_results[j].box.y) / 2.0f;
                            raw_results[i].box.w = (raw_results[i].box.w + raw_results[j].box.w) / 2.0f;
                            raw_results[i].box.h = (raw_results[i].box.h + raw_results[j].box.h) / 2.0f;
                        }
                        is_suppressed[j] = true;
                    }
                }
            }
        }
    }
    catch (...) {}

    return final_results;
}
