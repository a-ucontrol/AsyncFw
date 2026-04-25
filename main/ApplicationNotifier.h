#pragma once

#include "../core/FunctionConnector.h"
#include "../core/AnyData.h"
#include "Instance.h"

namespace AsyncFw {
/*! \class ApplicationNotifier ApplicationNotifier.h <AsyncFw/ApplicationNotifier> \brief The ApplicationNotifier class.
  \brief Example: \snippet ApplicationNotifier/main.cpp snippet */
class ApplicationNotifier {
public:
  struct Value : public AnyData {
    Value(int type, const std::any &data = {}) : AnyData(data), type(type) {}
    int type;
  };
  static inline ApplicationNotifier *instance() { return instance_.value; }
  ApplicationNotifier();
  virtual ~ApplicationNotifier();

  FunctionConnector<const Value &> notify;

private:
  static AsyncFw::Instance<ApplicationNotifier> instance_;
};
}  // namespace AsyncFw
