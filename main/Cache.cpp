/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "Cache.h"

template<typename T>
void AsyncFw::Cache<T>::refresh() {
  {  //lock scope
    std::lock_guard<std::mutex> lock(mutex_);
    if (!expire_) return;
    expire_ = 0;
  }
  update();
}
