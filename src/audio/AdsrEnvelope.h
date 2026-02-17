#pragma once

class AdsrEnvelope
{
public:
    enum State { Attack, Decay, Sustain, Release, Done };

    void noteOn (float attack, float decay, float sustain, float release)
    {
        attackTime  = attack;
        decayTime   = decay;
        sustainLvl  = sustain;
        releaseTime = release;
        state       = Attack;
        level       = 0.0f;
    }

    void noteOff()
    {
        if (state != Done)
            state = Release;
    }

    void forceRelease (float fastReleaseSec)
    {
        state = Release;
        releaseTime = fastReleaseSec;
    }

    float processSample (double sampleRate)
    {
        float rate;

        switch (state)
        {
            case Attack:
                rate = attackTime > 0.0001f ? 1.0f / (attackTime * (float) sampleRate) : 1.0f;
                level += rate;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    state = Decay;
                }
                break;

            case Decay:
                rate = decayTime > 0.0001f ? 1.0f / (decayTime * (float) sampleRate) : 1.0f;
                level -= rate;
                if (level <= sustainLvl)
                {
                    level = sustainLvl;
                    state = Sustain;
                }
                break;

            case Sustain:
                level = sustainLvl;
                break;

            case Release:
                rate = releaseTime > 0.0001f ? 1.0f / (releaseTime * (float) sampleRate) : 1.0f;
                level -= rate;
                if (level <= 0.0f)
                {
                    level = 0.0f;
                    state = Done;
                }
                break;

            case Done:
                level = 0.0f;
                break;
        }

        return level;
    }

    bool isDone() const { return state == Done; }
    State getState() const { return state; }
    float getLevel() const { return level; }

private:
    State state       = Done;
    float level       = 0.0f;
    float attackTime  = 0.0f;
    float decayTime   = 0.0f;
    float sustainLvl  = 1.0f;
    float releaseTime = 0.0f;
};
