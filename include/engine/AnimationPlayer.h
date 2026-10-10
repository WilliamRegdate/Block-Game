#ifndef ANIMATION_PLAYER_H
#define ANIMATION_PLAYER_H

#include<unordered_map>
#include<string>
#include "Animation.h"

class AnimationPlayer
{
    public:
        bool isPlaying = false;

        // Registers a clip under `name` so Play(name) can find it later.
        // Doesn't take ownership - `clip` must outlive this player. This is
        // a minimal stand-in for actually loading clips until there's a
        // real asset/resource container to pull them from.
        void AddClip(const std::string& name, BakedAnimationClip* clip);

        // Plays the clip registered under `name`, if any. Does nothing
        // (and logs) if no such clip was ever added via AddClip.
        void Play(const std::string& name);

        // Stops playing. The current clip/frame are left as they were, so
        // GetCurrentClip() still returns the last-played clip until Play()
        // is called again - only isPlaying flips.
        void Stop();

        // Advances playback by `delta` seconds, handling looping/stopping
        // at the clip's duration. No-op if nothing is playing.
        void Update(double delta);

        // nullptr if nothing is currently playing.
        BakedAnimationClip* GetCurrentClip() const { return isPlaying ? current : nullptr; }

        uint32_t GetCurrentFrame(uint32_t frameCount, float duration) const;

        std::vector<BakedAnimationClip*> GetClips();

        float speed = 1.0f;
        bool looping = true;
        float progress = 0.0f;

    private:
        BakedAnimationClip* current = nullptr;
        std::unordered_map<std::string, BakedAnimationClip*> clips;
};


#endif