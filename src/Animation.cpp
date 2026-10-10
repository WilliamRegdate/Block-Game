#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{

// Index k such that times[k] <= t < times[k+1]. Caller must have already
// handled t <= times.front() and t >= times.back() - this assumes t falls
// strictly within the track's range.
size_t FindKeyframe(const std::vector<float>& times, float t)
{
    size_t k = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin());
    return (k == 0) ? 0 : k - 1;
}

// Cubic Hermite basis, per glTF's cubic-spline interpolation formula -
// tangents are scaled by the segment duration (dt). Works for both vec3
// and vec4 (quaternion components treated as a vec4 - see SampleQuatTrack).
template<typename T>
T HermiteInterpolate(const T& v0, const T& outTangent0, const T& v1, const T& inTangent1, float s, float dt)
{
    const float s2 = s * s;
    const float s3 = s2 * s;

    const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
    const float h10 = s3 - 2.0f * s2 + s;
    const float h01 = -2.0f * s3 + 3.0f * s2;
    const float h11 = s3 - s2;

    return h00 * v0 + (dt * h10) * outTangent0 + h01 * v1 + (dt * h11) * inTangent1;
}

glm::vec3 SampleVec3Track(const Track<glm::vec3>& track, float t)
{
    const std::vector<float>& times = track.times;
    const size_t keyCount = times.size();

    if (t <= times.front())
        return track.mode == Interpolation::CubicSpline ? track.values[1] : track.values.front();
    if (t >= times.back())
        return track.mode == Interpolation::CubicSpline ? track.values[(keyCount - 1) * 3 + 1] : track.values.back();

    const size_t k = FindKeyframe(times, t);
    const float dt = times[k + 1] - times[k];
    const float s = (dt > 0.0f) ? (t - times[k]) / dt : 0.0f;

    switch (track.mode)
    {
        case Interpolation::Step:
            return track.values[k];

        case Interpolation::CubicSpline:
        {
            const glm::vec3& v0 = track.values[k * 3 + 1];
            const glm::vec3& b0 = track.values[k * 3 + 2]; // out-tangent of k
            const glm::vec3& v1 = track.values[(k + 1) * 3 + 1];
            const glm::vec3& a1 = track.values[(k + 1) * 3 + 0]; // in-tangent of k+1
            return HermiteInterpolate(v0, b0, v1, a1, s, dt);
        }

        case Interpolation::Linear:
        default:
            return glm::mix(track.values[k], track.values[k + 1], s);
    }
}

glm::quat SampleQuatTrack(const Track<glm::quat>& track, float t)
{
    const std::vector<float>& times = track.times;
    const size_t keyCount = times.size();

    if (t <= times.front())
        return track.mode == Interpolation::CubicSpline ? track.values[1] : track.values.front();
    if (t >= times.back())
        return track.mode == Interpolation::CubicSpline ? track.values[(keyCount - 1) * 3 + 1] : track.values.back();

    const size_t k = FindKeyframe(times, t);
    const float dt = times[k + 1] - times[k];
    const float s = (dt > 0.0f) ? (t - times[k]) / dt : 0.0f;

    switch (track.mode)
    {
        case Interpolation::Step:
            return track.values[k];

        case Interpolation::CubicSpline:
        {
            // Hermite-blended component-wise as a vec4, then renormalized -
            // the standard real-time approximation for cubic-spline
            // rotation tracks. A fully quaternion-correct spline (squad) is
            // a lot more machinery for a difference you're unlikely to see
            // outside unusually fast, sharply-curved rotation segments.
            const glm::quat& v0 = track.values[k * 3 + 1];
            const glm::quat& b0 = track.values[k * 3 + 2];
            const glm::quat& v1 = track.values[(k + 1) * 3 + 1];
            const glm::quat& a1 = track.values[(k + 1) * 3 + 0];

            glm::vec4 blended = HermiteInterpolate(
                glm::vec4(v0.x, v0.y, v0.z, v0.w), glm::vec4(b0.x, b0.y, b0.z, b0.w),
                glm::vec4(v1.x, v1.y, v1.z, v1.w), glm::vec4(a1.x, a1.y, a1.z, a1.w),
                s, dt);
            return glm::normalize(glm::quat(blended.w, blended.x, blended.y, blended.z));
        }

        case Interpolation::Linear:
        default:
            return glm::slerp(track.values[k], track.values[k + 1], s);
    }
}

// Steps 1-3 of clip playback for one instant: sample every bone's local
// T/R/S at time t (falling back to the bone's rest pose where a track is
// empty, or where the clip has no entry at all for a given bone index),
// walk the skeleton hierarchy to turn each local pose into a world-space
// matrix, then apply Bone::inverseBind to get the final skinning matrix.
void ComputeSkinningMatrices(const Skeleton& skeleton, const AnimationClip& clip, float t, std::vector<glm::mat4>& outSkinningMatrices)
{
    const size_t boneCount = skeleton.bones.size();
    std::vector<glm::mat4> worldMatrices(boneCount);

    static const BoneTracks kEmptyTracks; // reused for bones the clip has no entry for at all

    for (uint32_t boneIndex : skeleton.evalOrder)
    {
        const Bone& bone = skeleton.bones[boneIndex];
        const BoneTracks& tracks = (boneIndex < clip.bones.size()) ? clip.bones[boneIndex] : kEmptyTracks;

        const glm::vec3 translation = tracks.translation.Empty() ? bone.restTranslation : SampleVec3Track(tracks.translation, t);
        const glm::quat rotation    = tracks.rotation.Empty()    ? bone.restRotation    : SampleQuatTrack(tracks.rotation, t);
        const glm::vec3 scale       = tracks.scale.Empty()       ? bone.restScale       : SampleVec3Track(tracks.scale, t);

        const glm::mat4 local = glm::translate(glm::mat4(1.0f), translation)
                               * glm::mat4_cast(rotation)
                               * glm::scale(glm::mat4(1.0f), scale);

        worldMatrices[boneIndex] = (bone.parent < 0)
            ? local
            : worldMatrices[bone.parent] * local;
    }

    outSkinningMatrices.resize(boneCount);
    for (size_t i = 0; i < boneCount; i++)
        outSkinningMatrices[i] = worldMatrices[i] * skeleton.bones[i].inverseBind;
}

} // namespace


BakedAnimationClip BakeAnimationClip(const Skeleton& skeleton, const AnimationClip& clip, float sampleRate)
{
    BakedAnimationClip baked;
    baked.ID = clip.ID;
    baked.name = clip.name;
    baked.duration = clip.duration;

    if (sampleRate <= 0.0f || skeleton.bones.empty())
        return baked; // nothing sensible to bake - caller gets an empty clip back (frameCount stays 0)

    // +1 so a zero-duration clip still gets one sample at t=0, and so the
    // final sample always reaches exactly clip.duration rather than
    // stopping short of it.
    const size_t sampleCount = static_cast<size_t>(std::ceil(clip.duration * sampleRate)) + 1;
    const size_t boneCount = skeleton.bones.size();

    baked.frameCount = static_cast<uint32_t>(sampleCount);
    baked.boneCount  = static_cast<uint32_t>(boneCount);
    baked.poses.resize(sampleCount * boneCount);

    std::vector<glm::mat4> sampleMatrices;
    for (size_t s = 0; s < sampleCount; s++)
    {
        const float t = std::min(static_cast<float>(s) / sampleRate, clip.duration);
        ComputeSkinningMatrices(skeleton, clip, t, sampleMatrices);

        for (size_t b = 0; b < boneCount; b++)
            baked.poses[s * boneCount + b] = sampleMatrices[b];
    }

    return baked;
}