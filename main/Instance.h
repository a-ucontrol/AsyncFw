/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Instance.h @brief The AbstractInstance and Instance classes. */

#include <string>
#include <typeinfo>

namespace AsyncFw {
/** @class AbstractInstance Instance.h <AsyncFw/Instance> @brief Abstract base class for global default service providers and managed instances. */
class AbstractInstance {
public:
  /** @brief Destroys stored values across all registered global instances. */
  static void destroyValues();

protected:
  /** @brief Constructs an abstract instance registry entry with a specific type and name. */
  AbstractInstance(const std::type_info &, const std::string &);

  /** @brief Pure virtual destructor requiring an explicit implementation body. */
  virtual ~AbstractInstance() = 0;

  /** @brief Destroys the underlying instance value. @return True if the value was alive and successfully destroyed. */
  virtual bool destroyValue() = 0;

  /** @brief Hook callback method executed immediately after the instance value is created. */
  virtual void created();

  /** @brief Searches the internal registry (populated by AbstractInstance constructors) for the entry whose registered std::type_info matches the given one. @param ti The std::type_info of the target class. @return Pointer to the matching AbstractInstance. */
  static AbstractInstance *findInstance(const std::type_info &);

private:
  struct Private;
  Private &private_;
};

/** @class Instance Instance.h <AsyncFw/Instance> @brief A template-based global registry wrapper for managing application-wide default objects (Global Default Instances).
@details Unlike a strict Singleton pattern, the underlying class (e.g., ThreadPool) remains a standard class that can be instantiated multiple times independently. This wrapper acts as a Service Locator, managing a single engine-wide global default instance.
@warning The static create() methods are **NOT thread-safe for concurrent instantiation**. All global default instances MUST be explicitly initialized (via create()) inside the primary execution thread (typically within main()) *BEFORE* any background worker threads or event loops are launched.
@par Example
@snippet Instance/main.cpp snippet */
template <typename T>
class Instance : public AbstractInstance {
  friend T;

public:
  /** @brief Registers a custom derived class instance as the engine-wide global default. @tparam CT The concrete target type to instantiate (must derive from T or be T itself). @param args Variadic arguments forwarded to the constructor of type CT. @return A pointer to the newly created object, or nullptr if an instance already exists. */
  template <typename CT, typename... Args>
  static CT *create(Args... args) {
    Instance<T> *_i = static_cast<Instance<T> *>(AbstractInstance::findInstance(typeid(T)));
    if (!_i->value) {
      CT *_v = new CT(args...);
      if (!_i->value) {
        _i->value = static_cast<T *>(_v);
        _i->created();
      }
#ifndef __clang_analyzer__
      return _v;
#endif
    }
    return nullptr;
  }

  /** @brief Creates the global default instance value of the exact type T. @param args Variadic arguments forwarded directly to the constructor of T. @return A pointer to the created instance object of type T, or nullptr if it already exists. */
  template <typename... Args>
  static T *create(Args... args) {
    return create<T>(args...);
  }

  /** @brief Manually sets or replaces the internal instance tracking pointer. @param p Pointer to the pre-allocated object of type T. */
  static void set(T *p) { static_cast<Instance<T> *>(AbstractInstance::findInstance(typeid(T)))->value = p; }

  /** @brief Retrieves the active engine-wide global default pointer for this instance type. @return A raw pointer to the managed object of type T, or nullptr if uninitialized. */
  static T *get() {
    static Instance<T> *_i = static_cast<Instance<T> *>(AbstractInstance::findInstance(typeid(T)));
    return _i->value;
  }

  /** @brief Overridden destructor that guarantees explicit deferred memory release for the managed resource. */
  virtual ~Instance() override { Instance<T>::destroyValue(); }

protected:
  /** @brief Initializes the instance tracker shell and updates the internal static reference context pointer. */
  Instance(const std::string &name = {}) : AbstractInstance(typeid(T), name) {}
  Instance(const Instance &) = delete;

  /** @brief Concretely frees the managed object allocation using standard C++ delete semantics. */
  bool destroyValue() override {
    if (value) {
      delete value;
      return true;
    }
    return false;
  }

private:
  T *value = nullptr;
};
}  // namespace AsyncFw
