#ifndef AIMBOTTARGET_H
#define AIMBOTTARGET_H

#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cfloat>

// Наши собственные структуры вместо OpenCV
struct RectF {
    float x, y, width, height;
    RectF() : x(0), y(0), width(0), height(0) {}
    RectF(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_) {}
};

struct PointF {
    float x, y;
    PointF() : x(0), y(0) {}
    PointF(float x_, float y_) : x(x_), y(y_) {}
};

class AimbotTarget
{
public:
    AimbotTarget();
    int x, y, w, h;
    int classId;
    double pivotX;
    double pivotY;
    AimbotTarget(int x_, int y_, int w_, int h_, int cls, double px = 0.0, double py = 0.0);
};

struct LockedTargetInfo
{
    int trackId = -1;
    bool observedThisFrame = false;
    int missedFrames = 0;
    AimbotTarget target;
};

struct TrackDebugInfo
{
    int trackId = -1;
    int classId = -1;
    RectF box;
    double pivotX = 0.0;
    double pivotY = 0.0;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    std::chrono::steady_clock::time_point lastUpdate{};
    bool observedThisFrame = false;
    int missedFrames = 0;
    bool isLocked = false;
};

class MultiTargetTracker
{
public:
    void reset();
    void update(
        const std::vector<RectF>& boxes,
        const std::vector<int>& classes,
        int screenWidth,
        int screenHeight,
        bool disableHeadshot,
        bool keepCurrentLock,
        std::chrono::steady_clock::time_point observationTime = {}
    );
    bool getLockedTarget(LockedTargetInfo& out) const;
    int getLockedTrackId() const { return lockedTrackId_; }
    std::vector<TrackDebugInfo> getDebugTracks() const;

private:
    struct TrackState
    {
        int id = -1;
        RectF box;
        PointF velocity = { 0.0f, 0.0f };
        int classId = -1;
        int hits = 0;
        int missed = 0;
        bool observedThisFrame = false;
        double pivotX = 0.0;
        double pivotY = 0.0;
        std::chrono::steady_clock::time_point lastUpdate;
    };

    struct DetectionCandidate
    {
        RectF box;
        int classId = -1;
        double pivotX = 0.0;
        double pivotY = 0.0;
    };

    static float iou(const RectF& a, const RectF& b);
    int findTrackIndexById(int id) const;
    int chooseBestTrack(int screenWidth, int screenHeight) const;
    int allowedMissedFrames(const TrackState& t) const;
    void pruneDeadTracks();

    std::vector<TrackState> tracks_;
    int nextId_ = 1;
    int lockedTrackId_ = -1;
    int maxMissedFrames_ = 6;
};

#endif // AIMBOTTARGET_H