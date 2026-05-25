/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>

namespace AsyncFw {
/*! \class AbstractInstance Instance.h <AsyncFw/Instance>
\brief Abstract base class for global singleton service providers and managed instances. */
class AbstractInstance {
public:
  /*! \brief Destroys stored values across all registered global instances. */
  static void destroyValues();

protected:
  /*! \brief Constructs an abstract instance registry entry with a specific identifier name. */
  AbstractInstance(const std::string &name);

  /*! \brief Pure virtual destructor requiring an explicit implementation body. */
  virtual ~AbstractInstance() = 0;

  /*! \brief Destroys the underlying instance value. \return True if the value was alive and successfully destroyed. */
  virtual bool destroyValue() = 0;

  /*! \brief Hook callback method executed immediately after the instance value is created. */
  virtual void created();

private:
  struct Private;
  Private &private_;
};

/*! \class Instance Instance.h <AsyncFw/Instance>
\brief A template-based global registry wrapper for managing application-wide unique objects (Singletons).
\brief Example: \snippet Instance/main.cpp snippet */
template <typename T>
class Instance : public AbstractInstance {
  friend T;

public:
  /*! \brief Creates a custom polymorphically derived object inside this instance container. \tparam CT The concrete target type to instantiate (must derive from or be type T). \param args Variadic arguments forwarded to the constructor of type CT. \return A pointer to the newly created object, or nullptr if an instance already exists. */
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

  /*! \brief Creates the singleton instance value of the exact type T. \param args Variadic arguments forwarded directly to the constructor of T. \return A pointer to the created instance object of type T, or nullptr if it already exists. */
  template <typename... Args>
  static T *create(Args... args) {
    return create<T>(args...);
  }

  /*! \brief Manually sets or replaces the internal instance tracking pointer. \param p Pointer to the pre-allocated object of type T. */
  static void set(T *p) { i_->value = p; }

  /*! \brief Retrieves the active global pointer for this instance type. \return A raw pointer to the managed object of type T, or nullptr if uninitialized. */
  static T *get() { return i_->value; }

  /*! \brief Initializes the instance tracker shell and updates the internal static reference context pointer. */
  Instance(const std::string &_name = {}) : AbstractInstance(_name) { i_ = this; }

  /*! \brief Overridden destructor that guarantees explicit deferred memory release for the managed resource. */
  virtual ~Instance() override { Instance<T>::destroyValue(); }

protected:
  Instance(const Instance &) = delete;

  /*! \brief Concretely frees the managed object allocation using standard C++ delete semantics. */
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
