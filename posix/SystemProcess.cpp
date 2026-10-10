/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <cstdlib>
#include <asm-generic/ioctls.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include "core/AbstractThread.h"
#include "core/LogStream.h"
#include "main/SystemProcess.h"

#ifdef EXTEND_SYSTEMPROCESS_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "core/extend_trace.hpp"

using namespace AsyncFw;

struct SystemProcess::Private {
  bool process();
  bool read_fd(int, std::string *);

  int in = -1;
  bool redirect_stdin;
  AbstractThread::Waiter waiter_;
  State state_ = None;

  int out;
  int err;

  AbstractThread *thread_;
  std::vector<std::string> args;
  int code_;
  std::string cmdline_;
  pid_t pid_;
};

SystemProcess::SystemProcess(bool redirect_stdin) : private_(*new Private) {
  private_.redirect_stdin = redirect_stdin;
  private_.thread_ = AbstractThread::current();
  lsTrace();
}

SystemProcess::~SystemProcess() {
  delete &private_;
  lsTrace();
}

bool SystemProcess::start(const std::string &_cmdline, const std::vector<std::string> &_args) {
  private_.cmdline_ = _cmdline;
  private_.args = _args;
  return start();
}

bool SystemProcess::start() {
  private_.state_ = None;
  private_.code_ = 0;

  if (!private_.process()) {
    private_.state_ = Error;
    private_.code_ = -1;
    return false;
  }

  private_.thread_->appendPollTask(private_.out, AbstractThread::PollIn, [this](AbstractThread::PollEvents e) {
    if (e & AbstractThread::PollIn) {
      std::string buf;
      if (private_.read_fd(private_.out, &buf)) output(buf, false);
      trace() << LogStream::Color::DarkRed << "pollin out" << buf.size();
    }
    if (e & ~AbstractThread::PollIn) {
      private_.thread_->removePollDescriptor(private_.out);
      ::close(private_.out);
      private_.out = -1;
      lsTrace() << LogStream::Color::Red << "closed out";
      if (private_.err == -1) finality();
    }
  });

  private_.thread_->appendPollTask(private_.err, AbstractThread::PollIn, [this](AbstractThread::PollEvents e) {
    if (e & AbstractThread::PollIn) {
      std::string buf;
      if (private_.read_fd(private_.err, &buf)) output(buf, true);
      trace() << LogStream::Color::DarkRed << "pollin err" << buf.size();
    }
    if (e & ~AbstractThread::PollIn) {
      private_.thread_->removePollDescriptor(private_.err);
      ::close(private_.err);
      private_.err = -1;
      lsTrace() << LogStream::Color::Red << "closed err";
      if (private_.out == -1) finality();
    }
  });

  lsTrace() << LogStream::Color::Green << private_.cmdline_;

  private_.state_ = Running;
  stateChanged(private_.state_);
  return true;
}

SystemProcess::State SystemProcess::state() { return private_.state_; }

pid_t SystemProcess::pid() { return private_.pid_; }

void SystemProcess::wait() {
  if (private_.state_ != Running) {
    lsWarning("process not running");
    return;
  }
  private_.waiter_.wait();
}

int SystemProcess::exitCode() { return private_.code_; }

int SystemProcess::input(const std::string &str) const {
  if (private_.in < 0) return -1;
  return write(private_.in, str.data(), str.size());
}

void SystemProcess::finality() {
  int r;

  if (private_.in >= 0) {
    ::close(private_.in);
    private_.in = -1;
  }
  lsTrace() << LogStream::Color::Red << "closed input";

  if (waitpid(private_.pid_, &r, 0) == private_.pid_) {
    private_.state_ = (WIFEXITED(r)) ? Finished : Crashed;
    private_.code_ = WEXITSTATUS(r);
  } else {
    private_.state_ = Crashed;
    private_.code_ = -1;
    lsError() << "error waitpid";
  }

  if (private_.waiter_.waiting()) private_.waiter_.complete();

  stateChanged(private_.state_);
  lsTrace() << LogStream::Color::Red << "End: " + private_.cmdline_ << r << (int)private_.state_;
}

bool SystemProcess::Private::process() {
  int _pipe_out[2];
  int _pipe_err[2];
  int _pipe_in[2];
  int _pipe_exec[2];

  //build _args before fork() (async-signal-safe)
  std::vector<const char *> _args;
  _args.reserve(args.size() + 2);
  _args.push_back(cmdline_.c_str());
  for (std::size_t i = 0; i != args.size(); ++i) _args.push_back(args[i].c_str());
  _args.push_back(nullptr);

  if (pipe(_pipe_out) == -1) return false;
  if (pipe(_pipe_err) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    return false;
  }

  if (!redirect_stdin && pipe(_pipe_in) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    ::close(_pipe_err[0]);
    ::close(_pipe_err[1]);
    return false;
  }

  if (!redirect_stdin) {
    int _flags = fcntl(_pipe_in[1], F_GETFL, 0);
    if (_flags < 0 || fcntl(_pipe_in[1], F_SETFL, _flags | O_NONBLOCK) < 0) {
      ::close(_pipe_out[0]);
      ::close(_pipe_out[1]);
      ::close(_pipe_err[0]);
      ::close(_pipe_err[1]);
      ::close(_pipe_in[0]);
      ::close(_pipe_in[1]);
      return false;
    }
  }

  if (pipe2(_pipe_exec, O_CLOEXEC) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    ::close(_pipe_err[0]);
    ::close(_pipe_err[1]);
    if (!redirect_stdin) {
      ::close(_pipe_in[0]);
      ::close(_pipe_in[1]);
    }
    return false;
  }

  if ((pid_ = fork()) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    ::close(_pipe_err[0]);
    ::close(_pipe_err[1]);
    if (!redirect_stdin) {
      ::close(_pipe_in[0]);
      ::close(_pipe_in[1]);
    }
    ::close(_pipe_exec[0]);
    ::close(_pipe_exec[1]);
    return false;
  }

  if (pid_ > 0) {
    if (::close(_pipe_out[1]) == -1) {
      ::close(_pipe_out[0]);
      ::close(_pipe_err[0]);
      ::close(_pipe_exec[0]);
      ::close(_pipe_err[1]);
      ::close(_pipe_exec[1]);
      if (!redirect_stdin) {
        ::close(_pipe_in[0]);
        ::close(_pipe_in[1]);
      }
      return false;
    }
    if (::close(_pipe_err[1]) == -1) {
      ::close(_pipe_out[0]);
      ::close(_pipe_err[0]);
      ::close(_pipe_exec[0]);
      ::close(_pipe_exec[1]);
      if (!redirect_stdin) {
        ::close(_pipe_in[0]);
        ::close(_pipe_in[1]);
      }
      return false;
    }
    if (::close(_pipe_exec[1]) == -1) {
      ::close(_pipe_out[0]);
      ::close(_pipe_err[0]);
      ::close(_pipe_exec[0]);
      if (!redirect_stdin) {
        ::close(_pipe_in[0]);
        ::close(_pipe_in[1]);
      }
      return false;
    }
    if (!redirect_stdin) {
      if (::close(_pipe_in[0]) == -1) {
        ::close(_pipe_out[0]);
        ::close(_pipe_err[0]);
        ::close(_pipe_exec[0]);
        ::close(_pipe_in[1]);
        return false;
      }
      in = _pipe_in[1];
    }
    out = _pipe_out[0];
    err = _pipe_err[0];
    int _errno = 0;
    ssize_t n = ::read(_pipe_exec[0], &_errno, sizeof(_errno));
    ::close(_pipe_exec[0]);
    if (n == static_cast<ssize_t>(sizeof(_errno))) {
      int dummy;
      waitpid(pid_, &dummy, 0);
      pid_ = -1;
      ::close(out);
      out = -1;
      ::close(err);
      err = -1;
      if (!redirect_stdin) {
        ::close(in);
        in = -1;
      }
      errno = _errno;
      return false;
    }
    return true;
  }

  if (::close(_pipe_out[0]) == -1 || dup2(_pipe_out[1], STDOUT_FILENO) == -1 || ::close(_pipe_out[1]) == -1) goto FAIL;
  if (::close(_pipe_err[0]) == -1 || dup2(_pipe_err[1], STDERR_FILENO) == -1 || ::close(_pipe_err[1]) == -1) goto FAIL;
  if (!redirect_stdin && (::close(_pipe_in[1]) == -1 || dup2(_pipe_in[0], STDIN_FILENO) == -1 || ::close(_pipe_in[0]) == -1)) goto FAIL;

  execv(cmdline_.c_str(), const_cast<char **>(_args.data()));

FAIL:
  int e = errno;
  (void)!::write(_pipe_exec[1], &e, sizeof(e));
  //_exit — system call that terminates the process immediately, without handlers
  _exit(127);
}

bool SystemProcess::Private::read_fd(int fd, std::string *buf) {
  int _s;
  if (ioctl(fd, FIONREAD, &_s) < 0) {
    lsError() << "get size" << errno;
    return false;
  }
  if (_s == 0) {
    lsError() << "null size";
    return false;
  }
  buf->resize(_s);
  int r = read(fd, buf->data(), buf->size());
  if (r != _s) {
    lsError() << "read" << r;
    return false;
  }
  return true;
}

bool SystemProcess::exec_(const std::string &cmd, const std::vector<std::string> &args, Invocable<void(int, State, const std::string &, const std::string &)>::Abstract *f) {
  struct Data {
    SystemProcess process;
    std::string out;
    std::string err;
  };
  Data *_data = new Data;

  if (f) {
    _data->process.output.connect<AbstractFunctionConnector::Connection::Direct>([_data](const std::string &msg, bool err) {
      if (!err) _data->out += msg;
      else { _data->err += msg; }
    });
  }
  _data->process.stateChanged.connect<AbstractFunctionConnector::Connection::Direct>([f, _data](SystemProcess::State state) {
    if (state != SystemProcess::Running) {
      if (f) {
        (*f)(_data->process.exitCode(), state, _data->out, _data->err);
        delete f;
      }
      if (!_data->process.private_.thread_->invoke([_data]() { delete _data; })) delete _data;
    }
  });
  if (!_data->process.private_.thread_->invoke([cmd, args, f, _data]() {
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
