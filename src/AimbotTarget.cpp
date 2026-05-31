#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <cmath>
#include <limits>
#include <algorithm>
#include <numeric>
#include <mutex>

#include "AimbotTarget.h"

// ============================================================
// Конструкторы AimbotTarget (были объявлены, но не реализованы)
// ============================================================
AimbotTarget::AimbotTarget() : x(0), y(0), w(0), h(0), classId(-1), pivotX(0.0), pivotY(0.0) {}

AimbotTarget::AimbotTarget(int x_, int y_, int w_, int h_, int cls, double px, double py)
    : x(x_), y(y_), w(w_), h(h_), classId(cls), pivotX(px), pivotY(py) {
}

// ============================================================
// Остальной код MultiTargetTracker (без изменений)
// ============================================================

float MultiTargetTracker::iou(const RectF& a, const RectF& b) {
    const float x1 = (std::max)(a.x, b.x);
    const float y1 = (std::max)(a.y, b.y);
    const float x2 = (std::min)(a.x + a.width, b.x + b.width);
    const float y2 = (std::min)(a.y + a.height, b.y + b.height);
    const float w = (std::max)(0.0f, x2 - x1);
    const float h = (std::max)(0.0f, y2 - y1);
    const float inter = w * h;
    const float ua = a.width * a.height + b.width * b.height - inter;
    if (ua <= 1e-6f) return 0.0f;
    return inter / ua;
}

int MultiTargetTracker::findTrackIndexById(int id) const {
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

int MultiTargetTracker::allowedMissedFrames(const TrackState& t) const {
    const int lockedBonus = (t.id == lockedTrackId_) ? 8 : 0;
    return maxMissedFrames_ + lockedBonus;
}

void MultiTargetTracker::pruneDeadTracks() {
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(), [this](const TrackState& t) {
            return t.missed > allowedMissedFrames(t);
            }),
        tracks_.end());
}

int MultiTargetTracker::chooseBestTrack(int screenWidth, int screenHeight) const {
    if (tracks_.empty()) return -1;
    const double cx = screenWidth * 0.5;
    const double cy = screenHeight * 0.5;
    int bestIdx = -1;
    double bestScore = (std::numeric_limits<double>::max)();
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const auto& t = tracks_[i];
        if (t.missed > allowedMissedFrames(t)) continue;
        const double dx = t.pivotX - cx;
        const double dy = t.pivotY - cy;
        const double dist = std::hypot(dx, dy);
        const double hitBonus = (std::min)(5, t.hits) * 4.0;
        const double missPenalty = t.missed * 50.0;
        const double score = dist + missPenalty - hitBonus;
        if (score < bestScore) {
            bestScore = score;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

void MultiTargetTracker::reset() {
    tracks_.clear();
    nextId_ = 1;
    lockedTrackId_ = -1;
}

void MultiTargetTracker::update(
    const std::vector<RectF>& boxes,
    const std::vector<int>& classes,
    int screenWidth,
    int screenHeight,
    bool disableHeadshot,
    bool keepCurrentLock,
    std::chrono::steady_clock::time_point observationTime) {
    
    if (observationTime == std::chrono::steady_clock::time_point{}) {
        observationTime = std::chrono::steady_clock::now();
    }

    for (auto& t : tracks_) {
        t.observedThisFrame = false;
        if (!keepCurrentLock && t.id == lockedTrackId_) {
            t.missed++;
        }
    }

    std::vector<DetectionCandidate> candidates;
    candidates.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& box = boxes[i];
        int cls = (i < classes.size()) ? classes[i] : 0;
        
        if (disableHeadshot && cls == 0) continue;
        
        DetectionCandidate cand;
        cand.box = box;
        cand.classId = cls;
        cand.pivotX = box.x + box.width * 0.5;
        cand.pivotY = box.y + box.height * 0.5;
        candidates.push_back(cand);
    }

    std::vector<std::pair<int, int>> matches;
    std::vector<bool> detUsed(candidates.size(), false);
    std::vector<bool> trkUsed(tracks_.size(), false);

    const float iouThreshold = 0.35f;
    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        if (tracks_[ti].missed > allowedMissedFrames(tracks_[ti])) continue;
        
        int bestDetIdx = -1;
        float bestIoU = iouThreshold;
        
        for (size_t di = 0; di < candidates.size(); ++di) {
            if (detUsed[di]) continue;
            
            float curIoU = iou(RectF(tracks_[ti].box.x, tracks_[ti].box.y, 
                                     tracks_[ti].box.width, tracks_[ti].box.height),
                               candidates[di].box);
            
            if (curIoU > bestIoU) {
                bestIoU = curIoU;
                bestDetIdx = static_cast<int>(di);
            }
        }
        
        if (bestDetIdx >= 0) {
            matches.push_back({static_cast<int>(ti), bestDetIdx});
            detUsed[bestDetIdx] = true;
            trkUsed[ti] = true;
        }
    }

    for (const auto& match : matches) {
        TrackState& trk = tracks_[match.first];
        const DetectionCandidate& det = candidates[match.second];
        
        const float alpha = 0.75f;
        trk.box.x = trk.box.x * (1.0f - alpha) + det.box.x * alpha;
        trk.box.y = trk.box.y * (1.0f - alpha) + det.box.y * alpha;
        trk.box.width = trk.box.width * (1.0f - alpha) + det.box.width * alpha;
        trk.box.height = trk.box.height * (1.0f - alpha) + det.box.height * alpha;
        
        const double velAlpha = 0.5;
        const double newPivotX = det.box.x + det.box.width * 0.5;
        const double newPivotY = det.box.y + det.box.height * 0.5;
        trk.velocity.x = trk.velocity.x * (1.0f - velAlpha) + (newPivotX - trk.pivotX) * velAlpha;
        trk.velocity.y = trk.velocity.y * (1.0f - velAlpha) + (newPivotY - trk.pivotY) * velAlpha;
        
        trk.pivotX = newPivotX;
        trk.pivotY = newPivotY;
        trk.classId = det.classId;
        trk.hits++;
        trk.missed = 0;
        trk.observedThisFrame = true;
        trk.lastUpdate = observationTime;
    }

    for (size_t di = 0; di < candidates.size(); ++di) {
        if (detUsed[di]) continue;
        
        TrackState newTrk;
        newTrk.id = nextId_++;
        newTrk.box = candidates[di].box;
        newTrk.classId = candidates[di].classId;
        newTrk.pivotX = candidates[di].pivotX;
        newTrk.pivotY = candidates[di].pivotY;
        newTrk.hits = 1;
        newTrk.missed = 0;
        newTrk.observedThisFrame = true;
        newTrk.lastUpdate = observationTime;
        tracks_.push_back(newTrk);
    }

    pruneDeadTracks();

    if (!keepCurrentLock && lockedTrackId_ >= 0) {
        int idx = findTrackIndexById(lockedTrackId_);
        if (idx < 0 || tracks_[idx].missed > allowedMissedFrames(tracks_[idx])) {
            lockedTrackId_ = -1;
        }
    }

    if (lockedTrackId_ < 0) {
        int bestIdx = chooseBestTrack(screenWidth, screenHeight);
        if (bestIdx >= 0) {
            lockedTrackId_ = tracks_[bestIdx].id;
        }
    }
}

bool MultiTargetTracker::getLockedTarget(LockedTargetInfo& out) const {
    const int idx = findTrackIndexById(lockedTrackId_);
    if (idx < 0) return false;
    const auto& t = tracks_[idx];
    if (t.missed > allowedMissedFrames(t)) return false;
    out.trackId = t.id;
    out.observedThisFrame = t.observedThisFrame;
    out.missedFrames = t.missed;
    out.target = AimbotTarget(
        static_cast<int>(std::lround(t.box.x)),
        static_cast<int>(std::lround(t.box.y)),
        static_cast<int>(std::lround(t.box.width)),
        static_cast<int>(std::lround(t.box.height)),
        t.classId,
        t.pivotX,
        t.pivotY
    );
    return true;
}

std::vector<TrackDebugInfo> MultiTargetTracker::getDebugTracks() const {
    std::vector<TrackDebugInfo> out;
    out.reserve(tracks_.size());
    for (const auto& t : tracks_) {
        if (t.missed > allowedMissedFrames(t)) continue;
        TrackDebugInfo d;
        d.trackId = t.id;
        d.classId = t.classId;
        d.box = RectF(t.box.x, t.box.y, t.box.width, t.box.height);
        d.pivotX = t.pivotX;
        d.pivotY = t.pivotY;
        d.velocityX = t.velocity.x;
        d.velocityY = t.velocity.y;
        d.lastUpdate = t.lastUpdate;
        d.observedThisFrame = t.observedThisFrame;
        d.missedFrames = t.missed;
        d.isLocked = (t.id == lockedTrackId_);
        out.push_back(d);
    }
    return out;
}