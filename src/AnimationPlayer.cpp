#include "AnimationPlayer.h"

void AnimationPlayer::Play(std::string name)
{
    BakedAnimationClip* clip = clips.at(name);
    progress = 0.0f;
}
void AnimationPlayer::Stop()
{
    isPlaying = false;
}
void AnimationPlayer::Update(double delta)
{
    if (progress > current->duration)
    {
        if (!looping)
        {
            Stop();
        }
        else
        {
            progress = 0.0f;
        }
    }
    progress += delta * speed;
}