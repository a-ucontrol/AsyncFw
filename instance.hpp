#pragma once

#include <vector>
#include <string>

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

protected:
  virtual ~AbstractInstance() = default;
  virtual void destroyValue() = 0;
  void append(AbstractInstance *);
  void remove(AbstractInstance *);

  std::string name;

private:
  static class List list;
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

  Instance(const std::string &_name = {}) : value(nullptr) {
    name = _name;
    append(i_ = this);
  }
  virtual ~Instance() override {
    if (value) delete value;
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
