#pragma once

#include <vector>

namespace AsyncFw {

class AbstractInstance {
public:
  class List : public std::vector<AbstractInstance *> {
    friend AbstractInstance;

  public:
    static void destroy();

  private:
    ~List();
  };
  static void destroyValues();

protected:
  virtual ~AbstractInstance() = default;
  virtual void destroyValue() = 0;
  void append(AbstractInstance *);
  void remove(AbstractInstance *);

private:
  inline static class List list;
};

template <typename T>
class Instance : public AbstractInstance {
  friend T;

public:
  template <typename CT, typename... Args>
  static CT *create(Args... args) {
    if (!i_->value) {
      i_->value = new CT(args...);
      i_->created();
      return static_cast<CT *>(i_->value);
    }
    return nullptr;
  }
  template <typename... Args>
  static T *create(Args... args) {
    return create<T>(args...);
  }
  static void set(T *p) { i_->value = p; }
  static T *get() { return i_->value; }

  Instance() : value(nullptr) {
    i_ = this;
    append(i_);
  }
  virtual ~Instance() override {
    destroyValue();
    remove(i_);
  }

protected:
  Instance(const Instance &) = delete;
  void destroyValue() override {
    if (value) delete value;
  }
  virtual void created() {}

private:
  T *value;
  inline static Instance<T> *i_;
};
}  // namespace AsyncFw
