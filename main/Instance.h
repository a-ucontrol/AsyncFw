/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>

namespace AsyncFw {
/*! \class AbstractInstance Instance.h <AsyncFw/Instance> \brief The AbstractInstance class. */
class AbstractInstance {
public:
  /*! \brief Destroy values in all instances. */
  static void destroyValues();

protected:
  AbstractInstance(const std::string &name);
  virtual ~AbstractInstance() = 0;
  /*! \brief Destroy instance value. */
  virtual bool destroyValue() = 0;
  /*! \brief Runs when the instance value is created. */
  virtual void created();

private:
  struct Private;
  Private &private_;
};

/*! \class Instance Instance.h <AsyncFw/Instance> \brief The Instance class.
\brief Example: \snippet Instance/main.cpp snippet */
template <typename T>
class Instance : public AbstractInstance {
  friend T;

public:
  /*! \brief Create instance value. */
  template <typename CT, typename... Args>
  static CT *create(Args... args) {
    if (!i_->value) {
      CT *_v = new CT(args...);
      if (!i_->value) {
        i_->value = reinterpret_cast<T *>(_v);
        i_->created();
      }
#ifndef __clang_analyzer__
      return _v;
#endif
    }
    return nullptr;
  }
  template <typename... Args>
  /*! \brief Create instance value. */
  static T *create(Args... args) {
    return create<T>(args...);
  }
  /*! \brief Set instance value. */
  static void set(T *p) { i_->value = p; }
  /*! \brief Get instance value. */
  static T *get() { return i_->value; }

  Instance(const std::string &_name = {}) : AbstractInstance(_name) { i_ = this; }
  virtual ~Instance() override { Instance<T>::destroyValue(); }

protected:
  Instance(const Instance &) = delete;
  bool destroyValue() override {
    if (value) {
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
