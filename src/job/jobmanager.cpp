/*
 * This file is part of ATSDB.
 *
 * ATSDB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ATSDB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with ATSDB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <qtimer.h>
#include <QThreadPool>
#include <QCoreApplication>

#include "jobmanager.h"
#include "jobmanagerwidget.h"
#include "job.h"
#include "logger.h"
#include "stringconv.h"

using namespace Utils;

JobManager::JobManager()
    : Configurable ("JobManager", "JobManager0", 0, "threads.xml"), stop_requested_(false), stopped_(false), widget_(nullptr)
{
    logdbg  << "JobManager: constructor";
    QMutexLocker locker(&mutex_);

    registerParameter ("update_time", &update_time_, 10);

    logdbg  << "JobManager: constructor: end";
}

JobManager::~JobManager()
{
    logdbg  << "JobManager: destructor";
}

void JobManager::addJob (std::shared_ptr<Job> job)
{
    mutex_.lock();

    logdbg << "JobManager: addJob: " << job->name() << " num " << jobs_.size()+1;
    jobs_.push_back(job);
    mutex_.unlock();

    QThreadPool::globalInstance()->start(job.get());

    updateWidget();
}

void JobManager::addDBJob (std::shared_ptr<Job> job)
{
    mutex_.lock();

    queued_db_jobs_.push_back(job);

    mutex_.unlock();

    updateWidget();

    emit databaseBusy();
}


void JobManager::cancelJob (std::shared_ptr<Job> job)
{
    mutex_.lock();

    job->setObsolete();

    while (!job->done()) // wait for finish
    {
        loginf << "JobManager: cancelJob: waiting to finish";
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        msleep(100);
    }

    if (find(jobs_.begin(), jobs_.end(), job) != jobs_.end())
        jobs_.erase(find(jobs_.begin(), jobs_.end(), job));
    else
    {
        if (active_db_job_ == job)
            active_db_job_ = nullptr;
        else if (find(queued_db_jobs_.begin(), queued_db_jobs_.end(), job) != queued_db_jobs_.end())
            queued_db_jobs_.erase(find(queued_db_jobs_.begin(), queued_db_jobs_.end(), job));
        else
            logwrn << "JobManager: cancelJob: unknown job was to be cancelled";
    }

    mutex_.unlock();

    job->emitObsolete();

    updateWidget();
}

bool JobManager::noJobs ()
{
    QMutexLocker locker(&mutex_);

    return jobs_.size() == 0 && !active_db_job_ && queued_db_jobs_.size() == 0;
}

/**
 * Creates thread if possible.
 *
 * \exception std::runtime_error if thread already existed
 */
void JobManager::run()
{
    logdbg  << "JobManager: run: start";

    while (1)
    {
        mutex_.lock();

        bool changed=false;
        bool really_update_widget=false;

        while (jobs_.size() > 0)
        {
            // process normal jobs
            std::shared_ptr<Job> current = jobs_.front();
            assert (current);

            if (!current->obsolete() && !current->done())
                break;

            jobs_.pop_front();

            changed = true;
            logdbg << "JobManager: run: flushed job "+current->name();
            if(current->obsolete())
            {
                logdbg << "JobManager: run: flushing obsolete job";
                current->emitObsolete();
                continue;
            }

            logdbg << "JobManager: run: flushing done job";
            current->emitDone();
            logdbg << "JobManager: run: done job emitted "+current->name();

            really_update_widget = jobs_.size() == 0;
        }

        if (active_db_job_)
        {
            // see if active db job done or obsolete
            if(active_db_job_->obsolete())
            {
                logdbg << "JobManager: run: flushing db obsolete job";

                while (!active_db_job_->done()) // wait for finish
                {
                    logdbg << "JobManager: run: waiting to obsolete finish";
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    msleep(update_time_);
                }

                active_db_job_->emitObsolete();
                active_db_job_ = nullptr;
                changed = true;
            }
            else if (active_db_job_->done())
            {
                logdbg << "JobManager: run: flushing db done job";
                active_db_job_->emitDone();
                active_db_job_ = nullptr;
                changed = true;
                really_update_widget = true;
            }
        }

        while (!active_db_job_ && queued_db_jobs_.size() > 0)
        {
            // start a new one if needed
            std::shared_ptr<Job> current = queued_db_jobs_.front();
            queued_db_jobs_.pop_front();

            assert (current);
            assert (!current->done());

            if (current->obsolete())
            {
                current->emitObsolete();
                continue;
            }

            logdbg << "JobManager: run: starting dbjob " << current->name();
            active_db_job_ = current;

            QThreadPool::globalInstance()->start(active_db_job_.get());
            changed = true;
            break;
        }

        if (changed && !active_db_job_ && queued_db_jobs_.size() == 0)
            emit databaseIdle();

        mutex_.unlock();

        if (stop_requested_)
        {
            logdbg << "JobManager: run: stop requested";
            break;
        }

        if (changed)
            updateWidget(really_update_widget);

        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        msleep(update_time_);
    }
    stopped_=true;
    logdbg  << "JobManager: run: stopped";
}

void JobManager::shutdown ()
{
    loginf  << "JobManager: shutdown";
    mutex_.lock();

    if (widget_)
    {
        delete widget_;
        widget_ = nullptr;
    }

    if (active_db_job_)
        active_db_job_->setObsolete();

    for (auto job : queued_db_jobs_)
        job->setObsolete ();

    for (auto job : jobs_)
        job->setObsolete ();

//    while (jobs_.size() > 0 || active_db_job_ || queued_db_jobs_.size() > 0 )
//    {
//        mutex_.unlock();
//        msleep(update_time_);
//        mutex_.lock();
//    }

    mutex_.unlock();
    logdbg  << "JobManager: shutdown: setting stop requested";
    stop_requested_ = true;

    while (!stopped_)
    {
        logdbg  << "JobManager: shutdown: waiting";
        while (QCoreApplication::hasPendingEvents())
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::currentThread()->msleep(update_time_);
    }

    logdbg  << "JobManager: shutdown: done";
}

JobManagerWidget *JobManager::widget()
{
    if (!widget_)
    {
        widget_ = new JobManagerWidget (*this);
        last_update_time_ = boost::posix_time::microsec_clock::local_time();
    }

    assert (widget_);
    return widget_;
}

unsigned int JobManager::numJobs ()
{
    QMutexLocker locker(&mutex_);

    return jobs_.size();
}

unsigned int JobManager::numDBJobs ()
{
    QMutexLocker locker(&mutex_);

    return active_db_job_ ? queued_db_jobs_.size()+1 : queued_db_jobs_.size();
}

int JobManager::numThreads ()
{
    return QThreadPool::globalInstance()->activeThreadCount();
}

void JobManager::updateWidget (bool really)
{
    if (widget_)
    {
        boost::posix_time::ptime current_time = boost::posix_time::microsec_clock::local_time();
        boost::posix_time::time_duration diff = current_time - last_update_time_;

        if (diff.total_milliseconds() > 500 || really)
        {
            widget_->updateSlot();
            last_update_time_=current_time;
        }
    }
}
