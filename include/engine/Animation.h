#ifndef ANIMATION_H
#define ANIMATION_H

#include "Skeleton.h"

enum class Interpolation { Linear, Step, CubicSpline };

template<typename T>
struct Track
{
    std::vector<float> times;     // keyframe times in seconds, ascending
    std::vector<T>     values;    // one per key (Linear/Step); three per key for CubicSpline
    Interpolation      mode = Interpolation::Linear;

    bool Empty() const { return times.empty(); }
};

struct BoneTracks
{
    Track<glm::vec3> translation;
    Track<glm::quat> rotation;
    Track<glm::vec3> scale;
};

struct AnimationClip
{
    uint32_t    ID = 0; 
    std::string             name;
    float                   duration = 0.0f;   // max of all track end times
    std::vector<BoneTracks> bones;             // indexed by bone index; empty tracks mean "use rest pose"
};

struct BakedAnimationClip
{
    uint32_t    ID = 0; 
    std::string name;
    float       duration = 0.0f;
    uint32_t    frameCount = 0; // poses.size() == frameCount * boneCount
    uint32_t    boneCount = 0;
    std::vector<glm::mat4x4> poses;
};

BakedAnimationClip BakeAnimationClip(const Skeleton& skeleton, const AnimationClip& clip, float sampleRate);

#endif