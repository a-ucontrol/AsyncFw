/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>

namespace AsyncFw {
/*! \class AbstractInstance Instance.h <AsyncFw/Instance> \brief The AbstractInstance class. */
class AbstractInstance {
public:
  static void destroyValues();

protected:
  AbstractInstance(const std::string &name);
  virtual ~AbstractInstance() = 0;
  virtual bool destroyValue() = 0;
  virtual void created();
  virtual void destroing();

private:
  struct Private;
  Private &private_;
};

/*! \class Instance Instance.h <AsyncFw/Instance> \brief The Instance class. */
template <typename T>
class Instance : public AbstractInstance {
  friend T;

public:
  template <typename CT, typename... Args>
  static CT *create(Args... args) {
    if (!i_->value) {
      CT *_v = new CT(args...);
      if (!i_->value) {
        i_->value = reinterpret_cast<T *>(_v);
        i_->created();
      }
      return _v;
    }
    return nullptr;
  }
  template <typename... Args>
  static T *create(Args... args) {
    return create<T>(args...);
  }
  static void set(T *p) { i_->value = p; }
  static T *get() { return i_->value; }

  Instance(const std::string &_name = {}) : AbstractInstance(_name) { i_ = this; }
  virtual ~Instance() override { destroyValue(); }

protected:
  Instance(const Instance &) = delete;
  bool destroyValue() override {
    if (value) {
      destroing();
      delete value;
      return true;
    }
    return false;
  }

private:
  T *value = nullptr;
  static inline Instance<T> *i_;
};
}  // namespace AsyncFw
