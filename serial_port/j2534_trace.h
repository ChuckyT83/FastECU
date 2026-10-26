#ifndef FASTECU_J2534_TRACE_H
#define FASTECU_J2534_TRACE_H
#include <QDateTime>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// Optional JSONL sink. No driver/file I/O occurs under a transport-side lock.
// A short bounded queue critical section contains no file I/O. Queue overload
// drops records explicitly instead of applying file-writer backpressure.
class J2534Trace {
public:
    J2534Trace() = default;
    ~J2534Trace() { finish(); }
    J2534Trace(const J2534Trace &) = delete;
    J2534Trace &operator=(const J2534Trace &) = delete;
    bool enabled() const noexcept { return enabled_.load(); }
    quint64 dropped() const noexcept { return dropped_.load(); }
    bool failed() const noexcept { return failed_.load(); }
    bool start(const QString &path) noexcept {
        try {
            if (worker_.joinable() || stopping_) return false;
            if (path.isEmpty()) { failed_=true; return false; }
            const QFileInfo info(path);
            // Never overwrite an existing file, follow a symlink, or open a pipe.
            if (info.exists() || info.isSymLink()) { failed_=true; qWarning("J2534 trace requires a new regular file; tracing disabled."); return false; }
            enabled_=true;
            worker_=std::thread([this,path]{ consume(path); });
            QJsonObject header{{"format",1},{"software","FastECU"},{"qt_version",qVersion()},
                {"software_version",QCoreApplication::applicationVersion()},
                {"privacy_notice","Raw ECU traces may contain private calibration or identifiers. Review before sharing. Known ISO15765 security/VIN payloads are omitted."}};
            record("trace_start",header);
            return true; // actual destination errors are observable via failed()
        } catch (...) { enabled_=false; failed_=true; return false; }
    }
    void record(const char *operation, QJsonObject fields = {}) noexcept {
        if (!enabled()) return;
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.size()>=MaxRecords || stopping_) { ++dropped_; return; }
            fields["operation"]=QString::fromLatin1(operation);
            fields["sequence"]=QString::number(++sequence_);
            fields["timestamp_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            fields["dropped_before"]=QString::number(dropped());
            auto line=QJsonDocument(fields).toJson(QJsonDocument::Compact); line+='\n';
            if (line.size()>MaxRecordBytes) { ++dropped_; return; }
            queue_.push_back(std::move(line)); lock.unlock(); ready_.notify_one();
        } catch (...) { ++dropped_; }
    }
    // Called only after communication ends. Regular-file flush/join may take time.
    void finish() noexcept {
        try {
            enabled_=false;
            { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
            ready_.notify_one(); if(worker_.joinable()) worker_.join();
        } catch (...) { failed_=true; }
    }
private:
    enum { MaxRecords=128, MaxRecordBytes=32768, MaxFileBytes=16*1024*1024 };
    void consume(const QString &path) noexcept {
        try {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) ||
                !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)) {
                failed_=true; enabled_=false;
                qWarning("J2534 trace destination unavailable; tracing disabled."); return;
            }
            qint64 written=0;
            for (;;) {
                QByteArray line;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock,[this]{return stopping_ || !queue_.empty();});
                    if(queue_.empty() && stopping_) break;
                    line=std::move(queue_.front()); queue_.pop_front();
                }
                if(written+line.size()>MaxFileBytes) { ++dropped_; enabled_=false; failed_=true; qWarning("J2534 trace file limit reached; tracing disabled."); break; }
                if(file.write(line)!=line.size()) { failed_=true; enabled_=false; qWarning("J2534 trace write failed; tracing disabled."); break; }
                written+=line.size();
            }
            // A final summary makes drops detectable even when no later API occurs.
            const auto summary=QJsonDocument(QJsonObject{{"operation","trace_end"},
                {"dropped_total",QString::number(dropped())},{"failed",failed()}}).toJson(QJsonDocument::Compact)+'\n';
            if(written+summary.size()<=MaxFileBytes && file.write(summary)==summary.size()) {
                if(!file.flush()) failed_=true;
            } else failed_=true;
        } catch (...) { failed_=true; enabled_=false; }
    }
    std::atomic<bool> enabled_{false},failed_{false};
    std::atomic<quint64> sequence_{0},dropped_{0};
    std::mutex mutex_; std::condition_variable ready_; std::deque<QByteArray> queue_;
    bool stopping_=false;
    std::thread worker_;
};
#endif
