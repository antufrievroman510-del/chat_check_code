#include "WinHeaders.h"

#include <cmath>
#include <limits>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <chrono>

#include "AimbotTarget.h"

// ============================================================
// Konstruktory AimbotTarget
// ============================================================
AimbotTarget::AimbotTarget() : x(0), y(0), w(0), h(0), classId(-1), pivotX(0.0), pivotY(0.0) {}

AimbotTarget::AimbotTarget(int x_, int y_, int w_, int h_, int cls, double px, double py)
    : x(x_), y(y_), w(w_), h(h_), classId(cls), pivotX(px), pivotY(py) {
}

// ============================================================
// MultiTargetTracker - uluchshennaya logika iz source_logic
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
    // Derzhim zablokirovannuyu tsel' dol'she dlya zashchity ot kratkovremennykh okklyuziy
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

// ============================================================
// void MultiTargetTracker::update() - uluchshennaya logika trekinga
// s podderzhkoy Head/Body klassov i umnogo vybora tseli
// ============================================================
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

    // Sbrosyvaem flag observedThisFrame dlya vsekh trekov
    for (auto& t : tracks_) {
        t.observedThisFrame = false;
    }

    // Klassy: 0 = Body/Player, 1 = Head
    const int classPlayer = 0;
    const int classHead = 1;
    const float bodyOffset = 0.15f;  // Smeshchenie tochki pritselivaniya dlya tela (15% snverhu boksa)
    const float headOffset = 0.05f;  // Smeshchenie tochki pritselivaniya dlya golovy (5% snverhu boksa)

    // Formiruem kandidatov detektov s pravil'nymi pivot points
    std::vector<DetectionCandidate> candidates;
    candidates.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& box = boxes[i];
        int cls = (i < classes.size()) ? classes[i] : classPlayer;
        
        // Propuskaem golovu yesli disableHeadshot vklyuchen
        if (disableHeadshot && cls == classHead) continue;
        
        // Propuskaem neizvestnye klassy
        if (cls != classPlayer && cls != classHead) continue;
        
        DetectionCandidate cand;
        cand.box = box;
        cand.classId = cls;
        cand.pivotX = box.x + box.width * 0.5;
        // Vychislyaem pivotY v zavisimosti ot klassa (golova ili telo)
        cand.pivotY = box.y + box.height * ((cls == classHead) ? headOffset : bodyOffset);
        candidates.push_back(cand);
    }

    // Yesli headshot vklyuchen, obedinyaem golovu i telo odnogo igroka
    if (!disableHeadshot && !candidates.empty()) {
        std::vector<size_t> playerIdx;
        playerIdx.reserve(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].classId == classPlayer)
                playerIdx.push_back(i);
        }

        if (!playerIdx.empty()) {
            std::vector<char> dropHead(candidates.size(), 0);
            std::vector<char> playerHasHeadPivot(candidates.size(), 0);
            std::vector<double> playerHeadPivotX(candidates.size(), 0.0);
            std::vector<double> playerHeadPivotY(candidates.size(), 0.0);
            std::vector<double> playerHeadPivotDist(candidates.size(), 1e9); // ~max double

            // Dlya kazhdoy golovy ishchem blizhayshee telo
            for (size_t hi = 0; hi < candidates.size(); ++hi) {
                const auto& h = candidates[hi];
                if (h.classId != classHead) continue;

                const double headCx = h.box.x + h.box.width * 0.5;
                const double headCy = h.box.y + h.box.height * 0.5;

                size_t bestPlayer = static_cast<size_t>(-1);
                double bestDist = 1e9; // ~max double

                for (size_t pi : playerIdx) {
                    const auto& p = candidates[pi].box;
                    // Rasshirennaya oblast' poiska tela vokrug golovy
                    const double px1 = p.x - p.width * 0.15;
                    const double px2 = p.x + p.width * 1.15;
                    const double py1 = p.y - p.height * 0.20;
                    const double py2 = p.y + p.height * 0.65;

                    if (!(headCx >= px1 && headCx <= px2 && headCy >= py1 && headCy <= py2))
                        continue;

                    const double pCx = p.x + p.width * 0.5;
                    const double pCy = p.y + p.height * 0.5;
                    const double d = std::hypot(headCx - pCx, headCy - pCy);
                    if (d < bestDist) {
                        bestDist = d;
                        bestPlayer = pi;
                    }
                }

                if (bestPlayer != static_cast<size_t>(-1)) {
                    dropHead[hi] = 1;
                    if (!playerHasHeadPivot[bestPlayer] || bestDist < playerHeadPivotDist[bestPlayer]) {
                        playerHasHeadPivot[bestPlayer] = 1;
                        playerHeadPivotDist[bestPlayer] = bestDist;
                        playerHeadPivotX[bestPlayer] = h.box.x + h.box.width * 0.5;
                        playerHeadPivotY[bestPlayer] = h.box.y + h.box.height * headOffset;
                    }
                }
            }

            // Udalyaem golovy, no perenosim ikh pivot na tela
            std::vector<DetectionCandidate> filtered;
            filtered.reserve(candidates.size());

            for (size_t i = 0; i < candidates.size(); ++i) {
                if (dropHead[i]) continue;

                DetectionCandidate d = candidates[i];
                if (d.classId == classPlayer && playerHasHeadPivot[i]) {
                    d.pivotX = playerHeadPivotX[i];
                    d.pivotY = playerHeadPivotY[i];
                }
                filtered.push_back(d);
            }

            candidates.swap(filtered);
        }
    }

    // Matching trekov i detektov cherez IoU
    std::vector<int> detAssigned(candidates.size(), -1);
    std::vector<int> trackAssigned(tracks_.size(), -1);

    const float iouThreshold = 0.35f;
    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        if (tracks_[ti].missed > allowedMissedFrames(tracks_[ti])) continue;
        
        int bestDetIdx = -1;
        float bestIoU = iouThreshold;
        
        for (size_t di = 0; di < candidates.size(); ++di) {
            if (detAssigned[di] != -1) continue;
            
            float curIoU = iou(RectF(tracks_[ti].box.x, tracks_[ti].box.y, 
                                     tracks_[ti].box.width, tracks_[ti].box.height),
                               candidates[di].box);
            
            if (curIoU > bestIoU) {
                bestIoU = curIoU;
                bestDetIdx = static_cast<int>(di);
            }
        }
        
        if (bestDetIdx >= 0) {
            detAssigned[bestDetIdx] = static_cast<int>(ti);
            trackAssigned[ti] = bestDetIdx;
        }
    }

    // Obnovlyaem matched treki
    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        if (trackAssigned[ti] == -1) continue;
        
        TrackState& trk = tracks_[ti];
        const DetectionCandidate& det = candidates[trackAssigned[ti]];
        
        const float alpha = 0.75f;
        trk.box.x = trk.box.x * (1.0f - alpha) + det.box.x * alpha;
        trk.box.y = trk.box.y * (1.0f - alpha) + det.box.y * alpha;
        trk.box.width = trk.box.width * (1.0f - alpha) + det.box.width * alpha;
        trk.box.height = trk.box.height * (1.0f - alpha) + det.box.height * alpha;
        
        const double velAlpha = 0.5;
        const double newPivotX = det.pivotX;
        const double newPivotY = det.pivotY;
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

    // Sozdayom novyye treki dlya unmatched detektov
    for (size_t di = 0; di < candidates.size(); ++di) {
        if (detAssigned[di] == -1) {
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
    }

    // Udalyaem myortvyye treki
    pruneDeadTracks();

    // Proveryayem, ne poteryali li tekushchuyu zablokirovannuyu tsel'
    if (!keepCurrentLock && lockedTrackId_ >= 0) {
        int idx = findTrackIndexById(lockedTrackId_);
        if (idx < 0 || tracks_[idx].missed > allowedMissedFrames(tracks_[idx])) {
            lockedTrackId_ = -1;
        }
    }

    // Yesli net zablokirovannoy tseli, vybirayem luchshuyu
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
