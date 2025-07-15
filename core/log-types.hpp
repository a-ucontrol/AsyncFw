#pragma once

#include <thread>
#include "DataArray.h"
#include "LogStream.h"

#ifndef _WIN32
  #if __has_include(<core/NetworkInterface.hpp>)
    #include <core/NetworkInterface.hpp>
    #define _NetworkInterface
  #endif
#endif

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const DataArray &v);
LogStream &operator<<(LogStream &log, const DataArrayList &v);

LogStream &operator<<(LogStream &log, const DataStream &v);
LogStream &operator<<(LogStream &log, const std::thread::id &v);

#ifdef _NetworkInterface
LogStream &operator<<(LogStream &log, const LRW::NetworkInterface::MacAddress &v);
LogStream &operator<<(LogStream &log, const LRW::NetworkInterface::Address &v);
LogStream &operator<<(LogStream &log, const LRW::NetworkInterface::Route &v);
#endif

LogStream &operator<<(LogStream &log, const std::wstring &v);
}  // namespace AsyncFw

#ifdef USE_QAPPLICATION

  #include <QDebug>
  #include <QJsonObject>
  #include <QJsonDocument>

  #undef qDebug
  #undef qInfo
  #undef qWarning
  #undef qCritical

  #define qDebug    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug
  #define qInfo     QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info
  #define qWarning  QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning
  #define qCritical QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).critical

namespace AsyncFw {
inline void qtMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
  QByteArray name(LogStream::sender(context.function).c_str());
  QByteArray note((context.line) ? QByteArray(context.file) + ":" + QByteArray::number(context.line) : "");
  switch (static_cast<int>(type)) {
    case QtDebugMsg: LogStream::completed({+LogStream::Debug | LogStream::DarkYellow, name.data(), ("(Qt) " + msg.toUtf8()).data(), note.data()}, LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND); break;
    case QtInfoMsg: LogStream::completed({+LogStream::Info | LogStream::DarkGreen, name.data(), ("(Qt) " + msg.toUtf8()).data(), note.data()}, LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND); break;
    case QtWarningMsg: LogStream::completed({+LogStream::Warning | LogStream::DarkBlue, name.data(), ("(Qt) " + msg.toUtf8()).data(), note.data()}, LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND); break;
    case QtCriticalMsg: LogStream::completed({+LogStream::Error | LogStream::DarkRed, name.data(), ("(Qt) " + msg.toUtf8()).data(), note.data()}, LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND); break;
    case QtFatalMsg:
      fprintf(stderr, "(Qt) Fatal error: %s\nprogramm exit\n", msg.toUtf8().data());
      fflush(stderr);
      break;
    default:
      fprintf(stderr, "(Qt) Warning: unknown log message type: %d\n", static_cast<int>(type));
      fflush(stderr);
      break;
  }
}

inline LogStream &operator<<(LogStream &log, const QByteArray &v) {
  if (v.isEmpty()) log << "\"\"";
  else { log << v.toStdString(); }
  return log;
}
inline LogStream &operator<<(LogStream &log, const QString &v) {
  if (v.isEmpty()) log << "\"\"";
  else { log << v.toStdString(); }
  return log;
}
inline LogStream &operator<<(LogStream &log, const QJsonObject &v) {
  if (v.isEmpty()) log << "{}";
  else { log << QJsonDocument(v).toJson().toStdString(); }
  return log;
}
}  // namespace AsyncFw
#endif
