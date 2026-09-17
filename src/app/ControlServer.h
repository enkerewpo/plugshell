// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace plugshell
{

/**
    A line-oriented JSON socket for driving the host from another program.

    Newline-delimited JSON over a loopback TCP socket, one request per line and
    one response per line. The choice is deliberately unfashionable: it can be
    driven from a shell with `nc` and from any language without a client
    library, which matters when the intended caller is a language model with a
    terminal rather than an application someone compiled against this.

    Requests are handled on the message thread, because everything they touch
    -- the plugin, its editor, the window -- belongs to it. The socket thread
    hands the work over and waits, with a timeout, so that a plugin which hangs
    produces a failed request rather than a stuck server.

    Loopback only, and no authentication, which is the right trade for a
    developer tool but means the port is as trusted as any local process. It is
    off unless asked for.
*/
class ControlServer : private juce::Thread
{
public:
    /** Called on the message thread. Returns the response object. */
    using Handler = std::function<juce::var(const juce::var& request)>;

    static constexpr int defaultPort = 8767;

    explicit ControlServer(Handler h) : juce::Thread("plugshell control"), handler(std::move(h)) {}

    ~ControlServer() override { stop(); }

    bool start(int port)
    {
        stop();

        listener = std::make_unique<juce::StreamingSocket>();
        if (!listener->createListener(port, "127.0.0.1"))
        {
            listener.reset();
            return false;
        }

        boundPort = port;
        startThread();
        return true;
    }

    void stop()
    {
        signalThreadShouldExit();

        if (listener != nullptr)
            listener->close();

        stopThread(2000);
        listener.reset();
        boundPort = 0;
    }

    int getPort() const { return boundPort; }

private:
    void run() override
    {
        while (!threadShouldExit() && listener != nullptr)
        {
            std::unique_ptr<juce::StreamingSocket> client(listener->waitForNextConnection());

            if (client == nullptr)
                continue;

            serve(*client);
        }
    }

    void serve(juce::StreamingSocket& client)
    {
        juce::String pending;

        while (!threadShouldExit() && client.isConnected())
        {
            // Waiting for readiness first is what separates "nothing has
            // arrived yet" from "the other end has gone". Both look like a
            // read of zero bytes, and treating the second as the first left
            // the loop holding a dead socket forever, so the first request
            // worked and every request after it was never accepted.
            const int ready = client.waitUntilReady(true, 200);

            if (ready < 0)
                return;

            if (ready == 0)
                continue;

            char buffer[4096];
            const int got = client.read(buffer, sizeof(buffer), false);

            if (got <= 0)
                return; // ready to read and nothing came: the peer closed

            pending += juce::String::fromUTF8(buffer, got);

            for (;;)
            {
                const int newline = pending.indexOfChar('\n');
                if (newline < 0)
                    break;

                const auto line = pending.substring(0, newline).trim();
                pending = pending.substring(newline + 1);

                if (line.isNotEmpty() && !writeAll(client, juce::JSON::toString(dispatch(line), true) + "\n"))
                    return;
            }
        }
    }

    /** A socket takes what it can and reports how much; a reply of any size
        therefore has to be written in a loop. Assuming one call suffices works
        until the first response large enough to matter, which for a plugin
        with a few thousand parameters is the first interesting one. */
    bool writeAll(juce::StreamingSocket& client, const juce::String& text)
    {
        const auto utf8 = text.toRawUTF8();
        const int total = (int) std::strlen(utf8);

        for (int sent = 0; sent < total;)
        {
            const int wrote = client.write(utf8 + sent, total - sent);

            if (wrote <= 0)
                return false;

            sent += wrote;
        }

        return true;
    }

    juce::var dispatch(const juce::String& line)
    {
        juce::var request;

        if (juce::JSON::parse(line, request).failed() || !request.isObject())
            return fail("request is not a JSON object");

        // Shared rather than captured by reference. The wait below can give
        // up, and when it does these locals would go out of scope underneath a
        // lambda that is still queued -- so the timeout path, the one that
        // only runs when something is already wrong, would corrupt memory.
        struct Call
        {
            juce::var request, response;
            juce::WaitableEvent done;
        };

        auto call = std::make_shared<Call>();
        call->request = request;

        // The handler runs where the plugin lives. Waiting with a timeout
        // rather than forever means a plugin that blocks the message thread
        // costs one request instead of the whole server.
        juce::MessageManager::callAsync(
            [this, call]
            {
                call->response = handler ? handler(call->request) : fail("no handler");
                call->done.signal();
            });

        if (!call->done.wait(15000))
            return fail("timed out waiting for the message thread; a plugin is probably blocking it");

        return call->response;
    }

    static juce::var fail(const juce::String& why)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("ok", false);
        o->setProperty("error", why);
        return juce::var(o);
    }

    Handler handler;
    std::unique_ptr<juce::StreamingSocket> listener;
    int boundPort = 0;
};

} // namespace plugshell
