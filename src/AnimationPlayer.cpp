#include "AnimationPlayer.h"

#include <algorithm>
#include <iostream>

void AnimationPlayer::AddClip(const std::string& name, BakedAnimationClip* clip)
{
    clips[name] = clip;
}

void AnimationPlayer::Play(const std::string& name)
{
    auto it = clips.find(name);
    if (it == clips.end())
    {
        std::cerr << "AnimationPlayer: no clip named \"" << name << "\" (call AddClip first)" << std::endl;
        return;
    }

    current = it->second;
    progress = 0.0f;
    isPlaying = true;
}

void AnimationPlayer::Stop()
{
    isPlaying = false;
}

void AnimationPlayer::Update(double delta)
{
    if (!isPlaying || current == nullptr)
        return;

    // Advance first, then check against duration - checking before
    // advancing (as this was originally written) tests the *previous*
    // frame's progress, so looping/stopping would trigger one Update()
    // call later than it should, after progress has already overshot
    // duration by up to one frame's worth of delta.
    progress += static_cast<float>(delta) * speed;

    if (progress > current->duration)
    {
        if (!looping)
        {
            Stop();
            progress = current->duration; // hold the last frame instead of leaving progress to drift past the end
        }
        else
        {
            // TODO: drops the remainder past duration, so very low frame
            // rates (large delta relative to duration) lose a little time
            // each loop. fmod(progress, current->duration) instead if that
            // drift ever becomes noticeable.
            progress = 0.0f;
        }
    }
}

uint32_t AnimationPlayer::GetCurrentFrame(uint32_t frameCount, float duration) const
{
    if (frameCount <= 1 || duration <= 0.0f)
        return 0;

    const float t = std::min(progress, duration);
    const uint32_t frame = static_cast<uint32_t>((t / duration) * static_cast<float>(frameCount - 1));
    return std::min(frame, frameCount - 1);
}

std::vector<BakedAnimationClip*> AnimationPlayer::GetClips()
{
    std::vector<BakedAnimationClip*> localClips;
    for (auto clip : clips)
    {
        localClips.push_back(clip.second);
    }
    return localClips;
}