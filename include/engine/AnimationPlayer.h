#ifndef ANIMATION_PLAYER_H
#define ANIMATIONPLAYER_H

#include<unordered_map>
#include "Animation.h"

class AnimationPlayer
{
    public:
        bool isPlaying = false;
        //plays the animation by name
        void Play(std::string name);
        //stops playing the animation
        void Stop();

        //steps the animation forward a frame
        void Update(double delta);

        float speed = 1.0f;
        bool looping = true;
    private:
        float progress = 0.0f; 
        BakedAnimationClip* current;
        std::unordered_map<std::string, BakedAnimationClip*> clips;

};


#endif