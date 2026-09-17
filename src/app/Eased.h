// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace plugshell
{

/**
    A value that catches up with its target instead of jumping to it.

    Interface movement is not decoration. A control that changes instantly
    gives no evidence that it was the thing you touched, and a panel that
    appears fully formed reads as a different screen rather than as this one
    showing more. A short movement carries that evidence, and the eye keeps
    its place.

    The bias is towards short and towards not being noticed. Anything long
    enough to be admired is long enough to be waited for, and this is a tool
    that will be used for hours.
*/
class Eased
{
public:
    explicit Eased(float startValue = 0.0f) : value(startValue), target(startValue) {}

    void setTarget(float t) { target = t; }
    void snapTo(float t) { value = target = t; }

    float get() const noexcept { return value; }
    float getTarget() const noexcept { return target; }
    bool isMoving() const noexcept { return std::abs(target - value) > 0.002f; }

    /** @param rate  the fraction of the remaining distance covered per frame.
                     At 60Hz, 0.25 settles in about six frames. */
    bool advance(float rate = 0.25f)
    {
        if (!isMoving())
        {
            if (value != target)
            {
                value = target;
                return true;
            }
            return false;
        }

        value += (target - value) * rate;
        return true;
    }

private:
    float value, target;
};

/** Drives a set of eased values, stopping once they have all settled so that
    nothing repaints while the interface is at rest. */
class Animator : private juce::Timer
{
public:
    explicit Animator(std::function<bool()> step) : advanceAll(std::move(step)) {}

    ~Animator() override { stopTimer(); }

    void nudge()
    {
        if (!isTimerRunning())
            startTimerHz(60);
    }

private:
    void timerCallback() override
    {
        if (!advanceAll())
            stopTimer();
    }

    std::function<bool()> advanceAll;
};

} // namespace plugshell
