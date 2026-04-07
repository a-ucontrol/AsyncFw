/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <QProcess>
#include <QCoreApplication>
#include <QEventLoop>
#include "core/AbstractThread.h"
#include "core/LogStream.h"

#include "main/SystemProcess.h"

#ifdef EXTEND_SYSTEMPROCESS_TRACE
  #define trace lsTrace
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

struct SystemProcess::Private {
  bool redirect_stdin = true;
  QEventLoop loop;
  State state_ = None;
  AbstractThread *thread_;
  std::vector<std::string> args;
  int code_;
  std::string cmdline_;
  QProcess process_;
};

SystemProcess::SystemProcess(bool redirect_stdin) {
  private_ = new Private;
  private_->redirect_stdin = redirect_stdin;
  private_->thread_ = AbstractThread::currentThread();
  lsTrace();
}

SystemProcess::~SystemProcess() {
  delete private_;
  lsTrace();
}

bool SystemProcess::start(const std::string &_cmdline, const std::vector<std::string> &_args) {
  private_->cmdline_ = _cmdline;
  private_->args = _args;
  return start();
}

bool SystemProcess::start() {
  private_->state_ = None;
  private_->code_ = 0;

  private_->process_.setInputChannelMode(private_->redirect_stdin ? QProcess::ForwardedInputChannel : QProcess::ManagedInputChannel);

  QObject::connect(&private_->process_, &QProcess::readyReadStandardOutput, [this]() {
    std::string buf = private_->process_.readAllStandardOutput().toStdString();
    output(buf, false);
    trace() << "out" << buf.size() << LogStream::Color::DarkGreen << buf;
  });

  QObject::connect(&private_->process_, &QProcess::readyReadStandardError, [this]() {
    std::string buf = private_->process_.readAllStandardError().toStdString();
    output(buf, true);
  });

  QObject::connect(&private_->process_, &QProcess::finished, [this](int exitCode, QProcess::ExitStatus exitStatus) {
    private_->state_ = exitStatus == QProcess::NormalExit ? Finished : Crashed;
    private_->code_ = exitCode;
    finality();
  });

  lsTrace() << LogStream::Color::Green << private_->cmdline_;

  private_->state_ = Running;
  stateChanged(private_->state_);

  QStringList _args;
  for (const std::string &a : private_->args) _args += QString::fromStdString(a);
  private_->process_.setProgram(QString::fromStdString(private_->cmdline_));
  private_->process_.setArguments(_args);
  private_->process_.start();

  if (private_->process_.state() == QProcess::NotRunning) {
    private_->state_ = Error;
    private_->code_ = -1;
    return false;
  }

  return true;
}

SystemProcess::State SystemProcess::state() { return private_->state_; }

pid_t SystemProcess::pid() { return private_->process_.processId(); }

void SystemProcess::wait() {
  if (private_->state_ != Running) {
    lsWarning("process not running");
    return;
  }
  private_->loop.exec();
}

int SystemProcess::exitCode() { return private_->code_; }

bool SystemProcess::input(const std::string &str) const { return private_->process_.write(QByteArray::fromStdString(str)) > 0; }

void SystemProcess::finality() {
  if (private_->loop.isRunning()) { private_->loop.quit(); }
  private_->process_.disconnect();
  stateChanged(private_->state_);
  lsTrace() << LogStream::Color::Red << "End: " + private_->cmdline_ << private_->code_ << static_cast<int>(private_->state_);
}

bool SystemProcess::exec_(const std::string &cmd, const std::vector<std::string> &args, AbstractFunction<int, State, const std::string &, const std::string &> *f) {
  struct Data {
    SystemProcess process;
    std::string out;
    std::string err;
  };
  Data *_data = new Data;

  if (f) {
    _data->process.output(
        [_data](const std::string &msg, bool err) {
          if (!err) _data->out += msg;
          else { _data->err += msg; }
        },
        AbstractFunctionConnector::Connection::Direct);
  }
  _data->process.stateChanged(
      [f, _data](SystemProcess::State state) {
        if (state != SystemProcess::Running) {
          if (f) {
            (*f)(_data->process.exitCode(), state, _data->out, _data->err);
            delete f;
          }
          if (!_data->process.private_->thread_->invokeMethod([_data]() { delete _data; })) delete _data;
        }
      },
      AbstractFunctionConnector::Connection::Direct);
  if (!_data->process.private_->thread_->invokeMethod([cmd, args, f, _data]() {
        if (!_data->process.start(cmd, args)) {
          if (f) {
            (*f)(_data->process.exitCode(), _data->process.state(), _data->out, _data->err);
            delete f;
          }
          delete _data;
        }
      })) {
    if (f) {
      (*f)(-1, Error, _data->out, _data->err);
      delete f;
    }
    delete _data;
    return false;
  }
  return true;
}
