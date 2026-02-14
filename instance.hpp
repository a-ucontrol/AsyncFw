#pragma once

#include <vector>

namespace AsyncFw {

class AbstractInstance {
public:
  static void destroyValues();

protected:
  virtual void created() = 0;
  virtual void destroyValue() = 0;
  void append(AbstractInstance *);
  void remove(AbstractInstance *);

private:
  inline static struct List {
    ~List();
    std::vector<AbstractInstance *> instances;
  } list;
};

template <typename T>
class Instance : public AbstractInstance {
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
    append(i_);
  }
  Instance(const Instance &) = delete;
  virtual ~Instance() {
    destroyValue();
    remove(i_);
  }
  void destroyValue() override {
    if (p_) delete p_;
  }

private:
  T *p_;
  inline static Instance<T> *i_;
};
}  // namespace AsyncFw
