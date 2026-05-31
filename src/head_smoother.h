#pragma once
#include <deque>
#include <vector>
#include <algorithm>
#include <map>
#include <set>

class HeadSmoother {
public:
    void update(float& x, float& y, float& w, float& h, int track_id) {
        auto& data = smooth_data[track_id];
        data.x_history.push_back(x);
        data.y_history.push_back(y);
        data.w_history.push_back(w);
        data.h_history.push_back(h);

        if (data.x_history.size() > HISTORY_SIZE) {
            data.x_history.pop_front();
            data.y_history.pop_front();
            data.w_history.pop_front();
            data.h_history.pop_front();
        }

        if (data.x_history.size() == HISTORY_SIZE) {
            auto median = [](std::deque<float>& v) {
                std::vector<float> sorted(v.begin(), v.end());
                std::sort(sorted.begin(), sorted.end());
                return sorted[sorted.size() / 2];
                };
            x = median(data.x_history);
            y = median(data.y_history);
            w = median(data.w_history);
            h = median(data.h_history);
        }
    }

    void clearOldTracks(const std::set<int>& active_track_ids) {
        for (auto it = smooth_data.begin(); it != smooth_data.end(); ) {
            if (active_track_ids.find(it->first) == active_track_ids.end())
                it = smooth_data.erase(it);
            else
                ++it;
        }
    }

private:
    struct SmoothData {
        std::deque<float> x_history, y_history, w_history, h_history;
    };
    std::map<int, SmoothData> smooth_data;
    static constexpr int HISTORY_SIZE = 5;
};