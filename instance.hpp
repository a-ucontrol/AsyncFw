#pragma once

#include <vector>

namespace AsyncFw {

class AbstractInstance {
  friend class MainThread;

protected:
  virtual void created() = 0;
  virtual void destroyValue() = 0;
  inline static struct List {
    void destroyValues();
    ~List();
    void append(AbstractInstance *);
    std::vector<AbstractInstance *> instances;
  } list;
};

template <typename T>
class Instance : public AbstractInstance {
  friend class MainThread;

public:
  template <typename CT, typename... Args>
  static CT *create(Args... args) {
    if (!i_->p_) {
      i_->p_ = new CT(args...);
      i_->created();
      return static_cast<CT *>(i_->p_);
    }
    return nullptr;
  }
  template <typename... Args>
  static T *create(Args... args) {
    return create<T>(args...);
  }
  static void set(T *p) { i_->p_ = p; }
  static T *value() { return i_->p_; }
  static void clear() { i_->p_ = nullptr; }

protected:
  Instance() : p_(nullptr) {
    i_ = this;
    list.append(i_);
  }
  Instance(const Instance &) = delete;
  virtual ~Instance() { Instance::destroyValue(); }
  void destroyValue() override {
    if (p_) delete p_;
  }

private:
  T *p_;
  inline static Instance<T> *i_;
};
}  // namespace AsyncFw
