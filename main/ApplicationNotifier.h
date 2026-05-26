/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*! \file ApplicationNotifier.h \brief The ApplicationNotifier class. */

#include "../core/FunctionConnector.h"
#include "../core/AnyData.h"
#include "Instance.h"

namespace AsyncFw {
/*! \class ApplicationNotifier ApplicationNotifier.h <AsyncFw/ApplicationNotifier> \brief The ApplicationNotifier class.
\brief Example: \snippet ApplicationNotifier/main.cpp snippet */
class ApplicationNotifier {
public:
  /*! \brief The Value struct. */
  struct Value : public AnyData {
    Value(int type, const std::any &data = {}) : AnyData(data), type(type) {}
    int type;
  };
  static inline ApplicationNotifier *instance() { return instance_.value; }
  ApplicationNotifier();
  virtual ~ApplicationNotifier();

  /*! \brief The ApplicationNotifier::notify connector */
  FunctionConnector<const Value &> notify;

private:
  static AsyncFw::Instance<ApplicationNotifier> instance_;
};
}  // namespace AsyncFw
