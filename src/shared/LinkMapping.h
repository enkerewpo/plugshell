// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "AudioLink.h"

namespace plugshell::link
{

/**
    The shared region, mapped into one process.

    Backed by a file rather than by shm_open, for two reasons that both come
    down to names: a POSIX shared memory name on macOS is limited to
    thirty-one characters, which is not enough to be unique per instance
    without being cryptic, and a name that outlives a crash is a leak that
    survives until the machine is restarted. A file in the temporary directory
    can be unlinked the moment both sides have it open, after which the
    mapping exists and the name does not.
*/
class Mapping
{
public:
    Mapping() = default;

    Mapping(Mapping&& other) noexcept { *this = std::move(other); }

    Mapping& operator=(Mapping&& other) noexcept
    {
        close();
        region = std::exchange(other.region, nullptr);
        fd = std::exchange(other.fd, -1);
        return *this;
    }

    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;

    ~Mapping() { close(); }

    /** Creates the region and immediately removes its name.

        The name is gone before this returns, so nothing can open the region
        by finding it, and nothing is left on disk if either process dies. The
        descriptor is what the child is given -- see descriptor() -- because
        an open file has no name it needs, and a name that has to survive
        until the child gets round to opening it is a race against a leak. */
    static Mapping create(const std::string& filePath, std::string& error)
    {
        Mapping m;

        m.fd = ::open(filePath.c_str(), O_CREAT | O_RDWR | O_EXCL, 0600);

        if (m.fd < 0)
        {
            error = "could not create " + filePath;
            return m;
        }

        ::unlink(filePath.c_str());

        if (::ftruncate(m.fd, (off_t) sizeof(Shared)) != 0)
        {
            error = "could not size the shared region";
            m.close();
            return m;
        }

        m.region = map(m.fd, error);

        if (m.region == nullptr)
            m.close();

        return m;
    }

    /** The child's side: a descriptor it inherited, which it now owns. */
    static Mapping fromDescriptor(int descriptor, std::string& error)
    {
        Mapping m;
        m.fd = descriptor;
        m.region = map(descriptor, error);

        if (m.region == nullptr)
            m.close();

        return m;
    }

    /** For handing to a child process. Still owned here. */
    int descriptor() const noexcept { return fd; }

    bool isValid() const noexcept { return region != nullptr; }
    Shared& operator*() const noexcept { return *region; }
    Shared* operator->() const noexcept { return region; }
    Shared* get() const noexcept { return region; }

private:
    static Shared* map(int descriptor, std::string& error)
    {
        void* p = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);

        if (p == MAP_FAILED)
        {
            error = "could not map the shared region";
            return nullptr;
        }

        return static_cast<Shared*>(p);
    }

    void close()
    {
        if (region != nullptr)
        {
            ::munmap(region, sizeof(Shared));
            region = nullptr;
        }

        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }

    Shared* region = nullptr;
    int fd = -1;
};

/**
    The pipe the host writes a byte to when a slot is ready.

    A pipe rather than a semaphore because of what happens when the host dies:
    the write end closes, the child's blocking read returns zero instead of
    waiting forever, and the child exits without anything having to notice it
    should. A semaphore left posted by a dead process is a child that never
    wakes and never stops.
*/
class Wakeup
{
public:
    Wakeup() = default;

    Wakeup(Wakeup&& other) noexcept { *this = std::move(other); }

    Wakeup& operator=(Wakeup&& other) noexcept
    {
        closeBoth();
        readFd = std::exchange(other.readFd, -1);
        writeFd = std::exchange(other.writeFd, -1);
        return *this;
    }

    Wakeup(const Wakeup&) = delete;
    Wakeup& operator=(const Wakeup&) = delete;

    ~Wakeup() { closeBoth(); }

    /** Writing to a pipe whose reader has gone raises SIGPIPE, and the default
        disposition of SIGPIPE is to end the process.

        For this application that default is precisely the failure being
        engineered against: the child dies, the host writes one more byte to
        wake it, and the host dies of the signal -- having gone to the trouble
        of putting the plugin in another process so that exactly this could not
        happen. Ignored, the write returns EPIPE instead, which is a fact the
        host can act on.

        Set here rather than left to each caller, because there is no correct
        program on either side of this link that wants the default. */
    static void ignoreBrokenPipes()
    {
        static const bool once = []
        {
            ::signal(SIGPIPE, SIG_IGN);
            return true;
        }();
        (void) once;
    }

    static Wakeup create()
    {
        ignoreBrokenPipes();

        Wakeup w;
        int fds[2]{-1, -1};

        if (::pipe(fds) == 0)
        {
            w.readFd = fds[0];
            w.writeFd = fds[1];
        }

        return w;
    }

    static Wakeup adoptReadEnd(int fd)
    {
        ignoreBrokenPipes();

        Wakeup w;
        w.readFd = fd;
        return w;
    }

    bool isValid() const noexcept { return readFd >= 0 || writeFd >= 0; }

    int readEnd() const noexcept { return readFd; }

    /** Called from the host's audio thread. One byte, non-blocking in
        practice: the pipe buffer is kilobytes and the child drains it every
        block, so this cannot stall -- and if it ever did, a dropped wake is a
        block the child answers late rather than an audio thread that waited. */
    void signal() const noexcept
    {
        const unsigned char one = 1;
        [[maybe_unused]] const auto ignored = ::write(writeFd, &one, 1);
    }

    /** Called from the child's worker. @return false when the host has gone. */
    bool wait() const noexcept
    {
        unsigned char buffer[64];
        const auto n = ::read(readFd, buffer, sizeof(buffer));
        return n > 0;
    }

    void closeWriteEnd()
    {
        if (writeFd >= 0)
        {
            ::close(writeFd);
            writeFd = -1;
        }
    }

    void closeReadEnd()
    {
        if (readFd >= 0)
        {
            ::close(readFd);
            readFd = -1;
        }
    }

private:
    void closeBoth()
    {
        closeReadEnd();
        closeWriteEnd();
    }

    int readFd = -1;
    int writeFd = -1;
};

} // namespace plugshell::link
