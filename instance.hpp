#pragma once

namespace AsyncFw {
template <typename T>
class AbstractInstance {
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
  AbstractInstance() : p_(nullptr) {
    if (!i_) i_ = this;
  }
  AbstractInstance(const AbstractInstance &) = delete;
  virtual ~AbstractInstance() {
    if (p_) delete p_;
  }
  virtual void created() = 0;

private:
  T *p_;
  inline static AbstractInstance<T> *i_;
};
}  // namespace AsyncFw
