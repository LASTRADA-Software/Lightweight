// SPDX-License-Identifier: Apache-2.0

#include "SerialWorker.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

#include <QtCore/QDeadlineTimer>
#include <QtCore/QRunnable>

namespace DbtoolGui
{

namespace
{

    /// Runs a job and hands an exception that escapes it to the failure callback; a QRunnable must not throw.
    class SerialJob final: public QRunnable
    {
      public:
        SerialJob(std::function<void()> job, std::function<void(std::exception const&)> onFailure):
            _job { std::move(job) },
            _onFailure { std::move(onFailure) }
        {
            setAutoDelete(true);
        }

        void run() override
        {
            try
            {
                _job();
            }
            catch (std::exception const& e)
            {
                if (_onFailure)
                    _onFailure(e);
            }
            catch (...)
            {
                if (_onFailure)
                    _onFailure(std::runtime_error { "unknown exception" });
            }
        }

      private:
        std::function<void()> _job;
        std::function<void(std::exception const&)> _onFailure;
    };

} // namespace

SerialWorker::SerialWorker()
{
    _pool.setMaxThreadCount(1);
}

SerialWorker::~SerialWorker()
{
    _pool.waitForDone();
}

void SerialWorker::Enqueue(std::function<void()> job, std::function<void(std::exception const&)> onFailure)
{
    _pool.start(std::make_unique<SerialJob>(std::move(job), std::move(onFailure)).release());
}

bool SerialWorker::WaitForDone(std::chrono::milliseconds timeout)
{
    if (timeout == std::chrono::milliseconds::max())
        return _pool.waitForDone(QDeadlineTimer { QDeadlineTimer::Forever });
    return _pool.waitForDone(QDeadlineTimer { timeout });
}

} // namespace DbtoolGui
