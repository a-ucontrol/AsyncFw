#include <cstdlib>
#include <sys/wait.h>
#include "core/Thread.h"
#include "core/LogStream.h"
#include "Coroutine.h"
#include "SystemProcess.h"

#ifdef EXTEND_SYSTEMPROCESS_TRACE
  #define trace lsTrace
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

struct SystemProcess::Private {
  bool process();
  int out;
  int err;
  int in = -1;
  bool redirect_stdin = true;

  AbstractThread *thread_;
  State state_ = None;

  std::vector<std::string> args;
  int code_;
  std::string cmdline_;
  pid_t pid_;
  CoroutineTask *wait_task_ = nullptr;
  CoroutineTask waitTask();
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

  if (!private_->process()) {
    private_->state_ = Error;
    private_->code_ = -1;
    return false;
  }

  if (private_->redirect_stdin)
    private_->thread_->appendPollTask(STDIN_FILENO, AbstractThread::PollIn, [this](AbstractThread::PollEvents e) {
      if (e & AbstractThread::PollIn) {
        char buf[BUFSIZ];
        int r = read(STDIN_FILENO, buf, BUFSIZ - 1);
        if (r > 0) {
          buf[r] = 0;
          input(buf);
        }
        trace() << "in" << r << LogStream::Color::Green << buf;
      }
      if (e & ~AbstractThread::PollIn) {
        private_->thread_->removePollDescriptor(STDIN_FILENO);
        private_->redirect_stdin = false;
        lsWarning() << LogStream::Color::Red << "redirect stdin disabled";
      }
    });

  private_->thread_->appendPollTask(private_->out, AbstractThread::PollIn, [this](AbstractThread::PollEvents e) {
    if (e & AbstractThread::PollIn) {
      char buf[BUFSIZ];
      int r = read(private_->out, buf, BUFSIZ - 1);
      if (r > 0) {
        buf[r] = 0;
        output(buf, false);
      }
      trace() << "out" << r << LogStream::Color::DarkGreen << buf;
    }
    if (e & ~AbstractThread::PollIn) {
      private_->thread_->removePollDescriptor(private_->out);
      ::close(private_->out);
      private_->out = -1;
      lsTrace() << LogStream::Color::Red << "closed out";
      if (private_->err == -1) finality();
    }
  });

  private_->thread_->appendPollTask(private_->err, AbstractThread::PollIn, [this](AbstractThread::PollEvents e) {
    if (e & AbstractThread::PollIn) {
      char buf[BUFSIZ];
      int r = read(private_->err, buf, BUFSIZ - 1);
      if (r > 0) {
        buf[r] = 0;
        output(buf, true);
      }
      trace() << "err" << r << LogStream::Color::DarkRed << buf;
    }
    if (e & ~AbstractThread::PollIn) {
      private_->thread_->removePollDescriptor(private_->err);
      ::close(private_->err);
      private_->err = -1;
      lsTrace() << LogStream::Color::Red << "closed err";
      if (private_->out == -1) finality();
    }
  });

  lsTrace() << LogStream::Color::Green << private_->cmdline_;

  private_->state_ = Running;
  stateChanged(private_->state_);
  return true;
}

SystemProcess::State SystemProcess::state() { return private_->state_; }

pid_t SystemProcess::pid() { return private_->pid_; }

void SystemProcess::wait() {
  if (private_->state_ != Running) {
    lsWarning("process not running");
    return;
  }
  CoroutineTask ct = private_->waitTask();
  private_->wait_task_ = &ct;
  ct.wait();
  private_->wait_task_ = nullptr;
}

int SystemProcess::exitCode() { return private_->code_; }

bool SystemProcess::input(const std::string &str) const {
  if (private_->in < 0) return false;
  return write(private_->in, str.data(), str.size()) > 0;
}

FunctionConnectorProtected<SystemProcess>::Connector<int, SystemProcess::State, const std::string &, const std::string &> &SystemProcess::exec(const std::string &_cmdline, const std::vector<std::string> &_args) {
  FunctionConnectorProtected<SystemProcess>::Connector<int, SystemProcess::State, const std::string &, const std::string &> *fc = new FunctionConnectorProtected<SystemProcess>::Connector<int, SystemProcess::State, const std::string &, const std::string &>(AbstractFunctionConnector::Queued);

  SystemProcess *process = new SystemProcess();
  std::string *_out = new std::string();
  std::string *_err = new std::string();
  process->output(
      [_out, _err](const std::string &msg, bool err) {
        if (!err) *_out += msg;
        else { *_err += msg; }
      },
      AbstractFunctionConnector::Connection::Direct);
  process->stateChanged(
      [fc, process, _out, _err](SystemProcess::State state) {
        if (state != SystemProcess::Running) {
          (*fc)(process->exitCode(), state, *_out, *_err);
          process->private_->thread_->invokeMethod([fc, process]() {
            delete fc;
            delete process;
          });
          delete _out;
          delete _err;
        }
      },
      AbstractFunctionConnector::Connection::Direct);
#ifndef __clang_analyzer__
  process->private_->thread_->invokeMethod([_cmdline, _args, process, fc, _out, _err]() {
#endif
    if (!process->start(_cmdline, _args)) {
      (*fc)(process->exitCode(), process->state(), *_out, *_err);
      process->private_->thread_->invokeMethod([fc]() { delete fc; });
      delete process;
      delete _out;
      delete _err;
    }
  });
  return *fc;
}

void SystemProcess::finality() {
  int r;

  if (private_->redirect_stdin) private_->thread_->removePollDescriptor(STDIN_FILENO);
  ::close(private_->in);
  private_->in = -1;
  lsTrace() << LogStream::Color::Red << "closed input";

  if (waitpid(private_->pid_, &r, 0) == private_->pid_) {
    private_->state_ = (WIFEXITED(r)) ? Finished : Crashed;
    private_->code_ = WEXITSTATUS(r);
  } else {
    private_->state_ = Crashed;
    private_->code_ = -1;
    lsError() << "error waitpid";
  }

  if (private_->wait_task_) private_->wait_task_->resume();

  stateChanged(private_->state_);
  lsTrace() << LogStream::Color::Red << "End: " + private_->cmdline_ << r << (int)private_->state_;
}

CoroutineTask SystemProcess::Private::waitTask() { co_await CoroutineAwait(); }

bool SystemProcess::Private::process() {
  int _pipe_out[2];
  int _pipe_err[2];
  int _pipe_in[2];

  if (pipe(_pipe_out) == -1) return false;
  if (pipe(_pipe_err) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    return false;
  }
  if (pipe(_pipe_in) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    ::close(_pipe_err[0]);
    ::close(_pipe_err[1]);
    return false;
  }
  if ((pid_ = fork()) == -1) {
    ::close(_pipe_out[0]);
    ::close(_pipe_out[1]);
    ::close(_pipe_err[0]);
    ::close(_pipe_err[1]);
    ::close(_pipe_in[0]);
    ::close(_pipe_in[1]);
    return false;
  }

  if (pid_ > 0) {
    if (::close(_pipe_out[1]) == -1) {
      ::close(_pipe_err[1]);
      ::close(_pipe_in[0]);
      return false;
    }
    if (::close(_pipe_err[1]) == -1) {
      ::close(_pipe_in[0]);
      return false;
    }
    if (::close(_pipe_in[0]) == -1) return false;
    out = _pipe_out[0];
    err = _pipe_err[0];
    in = _pipe_in[1];
    return true;
  }

  std::vector<const char *> _args;

  if (::close(_pipe_out[0]) == -1 || dup2(_pipe_out[1], STDOUT_FILENO) == -1 || ::close(_pipe_out[1]) == -1) goto FAIL;
  if (::close(_pipe_err[0]) == -1 || dup2(_pipe_err[1], STDERR_FILENO) == -1 || ::close(_pipe_err[1]) == -1) goto FAIL;
  if (::close(_pipe_in[1]) == -1 || dup2(_pipe_in[0], STDIN_FILENO) == -1 || ::close(_pipe_in[0]) == -1) goto FAIL;

  _args.push_back(cmdline_.c_str());
  for (std::size_t i = 0; i != args.size(); ++i) _args.push_back(args[i].c_str());
  _args.push_back(nullptr);
  execv(cmdline_.c_str(), const_cast<char **>(_args.data()));

FAIL:
  std::terminate();
}
