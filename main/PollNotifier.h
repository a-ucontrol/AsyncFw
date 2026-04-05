/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "../core/FunctionConnector.h"

namespace AsyncFw {
/*! \class PollNotifier PollNotifier.h <AsyncFw/PollNotifier> \brief The PollNotifier class
 Example: \snippet Stdin/main.cpp snippet */
class PollNotifier {
public:
  PollNotifier();
  /*! \brief Constructs a poll notifier. It assigns the file descriptor, and watches for events. \param fd file descriptor \param events watch events */
  PollNotifier(int, AbstractThread::PollEvents = AbstractThread::PollIn);
  ~PollNotifier();
  /*! \brief Assigns the file descriptor and watch events. \param fd file descriptor \param events watch events \return True if descriptor is set */
  bool setDescriptor(int, AbstractThread::PollEvents = AbstractThread::PollIn);
  /*! \brief Set watch events. \param events watch events \return True if events is set */
  bool setEvents(AbstractThread::PollEvents);
  /*! \brief Remove the file descriptor. */
  void removeDescriptor();
  /*! \brief Returns the file descriptor. */
  int descriptor() { return fd_; }
  /*! \brief Returns true if error occurred. */
  bool fail() { return fail_; }

  /*! \brief The PollNotifier::notify connector */
  FunctionConnectorProtected<PollNotifier>::Connector<AbstractThread::PollEvents> notify;

private:
  AbstractThread *thread_;
  int fd_ = -1;
  bool fail_ = false;
};
}  // namespace AsyncFw
