#pragma once

#include "../core/FunctionConnector.h"
#include "../core/AnyData.h"
#include "Instance.h"

namespace AsyncFw {
/*! \class ApplicationNotifier ApplicationNotifier.h <AsyncFw/ApplicationNotifier> \brief The ApplicationNotifier class.
  \brief Example: \snippet ApplicationNotifier/main.cpp snippet */
class ApplicationNotifier {
public:
  struct Data : public AnyData {
    Data(int type, const std::any &data = {}) : AnyData(data), type(type) {}
    int type;
  };
  static inline ApplicationNotifier *instance() { return instance_.value; }
  ApplicationNotifier();
  ~ApplicationNotifier();

  FunctionConnector<const Data &> notify;

private:
  static AsyncFw::Instance<ApplicationNotifier> instance_;
};
}  // namespace AsyncFw
