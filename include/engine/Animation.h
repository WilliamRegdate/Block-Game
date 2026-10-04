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
    std::string             name;
    float                   duration = 0.0f;   // max of all track end times
    std::vector<BoneTracks> bones;             // indexed by bone index; empty tracks mean "use rest pose"
};
struct BakedAnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<glm::mat4x4> poses;
};

// Samples `clip` at a fixed rate and bakes every bone's final skinning
// matrix (model-space transform * inverse bind matrix) into `poses`,
// frame-major: poses[frame * boneCount + boneIndex]. One frame is produced
// at t=0, then every 1/sampleRate seconds up to and including clip.duration
// (the final sample is clamped exactly to clip.duration, not left to drift
// from float accumulation).
//
// Walks bones via Skeleton::evalOrder, not Skeleton::bones' own index
// order - bones follows glTF's skin.joints order, which isn't guaranteed
// to put parents before children, so evalOrder is what the hierarchy walk
// actually relies on.
BakedAnimationClip BakeAnimationClip(const Skeleton& skeleton, const AnimationClip& clip, float sampleRate);

#endif