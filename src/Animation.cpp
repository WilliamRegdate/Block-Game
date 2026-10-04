#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <cassert>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{
    // First index where times[idx] >= t (times is kept ascending).
    size_t FindBracket(const std::vector<float>& times, float t)
    {
        return static_cast<size_t>(std::lower_bound(times.begin(), times.end(), t) - times.begin());
    }

    // Hermite basis weights for CubicSpline, shared by both sample
    // functions below.
    struct HermiteWeights
    {
        float h00, h10, h01, h11;
    };

    HermiteWeights Hermite(float u)
    {
        const float u2 = u * u;
        const float u3 = u2 * u;
        return {
            2.0f * u3 - 3.0f * u2 + 1.0f,
            u3 - 2.0f * u2 + u,
            -2.0f * u3 + 3.0f * u2,
            u3 - u2
        };
    }

    glm::vec3 SampleVec3Track(const Track<glm::vec3>& track, float t)
    {
        const size_t idx = FindBracket(track.times, t);
        const bool   isCubic = track.mode == Interpolation::CubicSpline;

        if (idx == 0)
            return isCubic ? track.values[1] : track.values[0];

        if (idx == track.times.size())
        {
            const size_t last = track.times.size() - 1;
            return isCubic ? track.values[3 * last + 1] : track.values[last];
        }

        const size_t i0 = idx - 1, i1 = idx;
        const float  segment = track.times[i1] - track.times[i0];
        const float  u = segment > 0.0f ? (t - track.times[i0]) / segment : 0.0f;

        switch (track.mode)
        {
            case Interpolation::Step:
                return track.values[i0];

            case Interpolation::CubicSpline:
            {
                const glm::vec3 p0 = track.values[3 * i0 + 1];
                const glm::vec3 m0 = track.values[3 * i0 + 2] * segment;   // out-tangent, scaled by segment length
                const glm::vec3 p1 = track.values[3 * i1 + 1];
                const glm::vec3 m1 = track.values[3 * i1 + 0] * segment;   // in-tangent, scaled by segment length

                const HermiteWeights w = Hermite(u);
                return w.h00 * p0 + w.h10 * m0 + w.h01 * p1 + w.h11 * m1;
            }

            case Interpolation::Linear:
            default:
                return glm::mix(track.values[i0], track.values[i1], u);
        }
    }

    glm::quat SampleQuatTrack(const Track<glm::quat>& track, float t)
    {
        const size_t idx = FindBracket(track.times, t);
        const bool   isCubic = track.mode == Interpolation::CubicSpline;

        if (idx == 0)
            return isCubic ? track.values[1] : track.values[0];

        if (idx == track.times.size())
        {
            const size_t last = track.times.size() - 1;
            return isCubic ? track.values[3 * last + 1] : track.values[last];
        }

        const size_t i0 = idx - 1, i1 = idx;
        const float  segment = track.times[i1] - track.times[i0];
        const float  u = segment > 0.0f ? (t - track.times[i0]) / segment : 0.0f;

        switch (track.mode)
        {
            case Interpolation::Step:
                return track.values[i0];

            case Interpolation::CubicSpline:
            {
                const glm::quat p0 = track.values[3 * i0 + 1];
                const glm::quat m0 = track.values[3 * i0 + 2] * segment;
                const glm::quat p1 = track.values[3 * i1 + 1];
                const glm::quat m1 = track.values[3 * i1 + 0] * segment;

                const HermiteWeights w = Hermite(u);

                // Blended as a raw 4-component vector (glm::quat supports
                // +/scalar* componentwise) - this does not preserve unit
                // length, so the result isn't a valid rotation until
                // normalized below.
                glm::quat result = w.h00 * p0 + w.h10 * m0 + w.h01 * p1 + w.h11 * m1;
                return glm::normalize(result);
            }

            case Interpolation::Linear:
            default:
                // slerp, not mix - a quaternion needs spherical
                // interpolation for constant angular velocity; component
                // lerp would speed up and slow down mid-rotation. glm's
                // slerp already takes the shortest path internally.
                return glm::slerp(track.values[i0], track.values[i1], u);
        }
    }
}


BakedAnimationClip BakeAnimationClip(const Skeleton& skeleton, const AnimationClip& clip, float sampleRate)
{
    assert(sampleRate > 0.0f);

    BakedAnimationClip baked;
    baked.name     = clip.name;
    baked.duration = clip.duration;

    const uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
    assert(skeleton.evalOrder.size() == boneCount);

    const float    dt         = 1.0f / sampleRate;
    const uint32_t frameCount = static_cast<uint32_t>(std::floor(clip.duration / dt)) + 1;

    baked.poses.resize(static_cast<size_t>(frameCount) * boneCount);

    std::vector<glm::mat4> globalTransforms(boneCount);

    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        const float sampleTime = std::min(frame * dt, clip.duration);
        
        for (uint32_t orderIndex = 0; orderIndex < boneCount; ++orderIndex)
        {
            const uint32_t boneIndex    = skeleton.evalOrder[orderIndex];
            const Bone&    skeletonBone = skeleton.bones[boneIndex];

            glm::vec3 translation = skeletonBone.restTranslation;
            glm::quat rotation    = skeletonBone.restRotation;
            glm::vec3 scale       = skeletonBone.restScale;

            if (boneIndex < clip.bones.size())
            {
                const BoneTracks& tracks = clip.bones[boneIndex];

                if (!tracks.translation.Empty())
                    translation = SampleVec3Track(tracks.translation, sampleTime);
                if (!tracks.rotation.Empty())
                    rotation = SampleQuatTrack(tracks.rotation, sampleTime);
                if (!tracks.scale.Empty())
                    scale = SampleVec3Track(tracks.scale, sampleTime);
            }

            const glm::mat4 localTransform =
                glm::translate(glm::mat4(1.0f), translation) *
                glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1.0f), scale);

            globalTransforms[boneIndex] = (skeletonBone.parent >= 0)
                ? globalTransforms[skeletonBone.parent] * localTransform   // safe: evalOrder guarantees the
                                                                             // parent was already processed
                                                                             // this frame
                : localTransform;

            baked.poses[static_cast<size_t>(frame) * boneCount + boneIndex] =
                globalTransforms[boneIndex] * skeletonBone.inverseBind;
        }
    }

    return baked;
}