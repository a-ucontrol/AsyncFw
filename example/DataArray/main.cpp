/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/DataArray>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  //write
  std::string _string1_w = "string1";
  std::string _string2_w = "string2";
  AsyncFw::DataArray _da1_w = "_da1";
  AsyncFw::DataArray _da2_w = "_da2";
  AsyncFw::DataArrayList _list_w;
  _list_w.push_back("da list item1");
  _list_w.push_back("da list item2");
  _list_w.push_back("da list item3");
  int8_t _int8_w = 8;
  int64_t _int64_w = 64;

  AsyncFw::DataStream _ds_w;
  _ds_w << _string1_w << _string2_w << _da1_w << _da2_w << _list_w << _int8_w << _int64_w;
  AsyncFw::DataArray _da_w = AsyncFw::DataArray::compress(_ds_w.array());

  //read
  std::string _string1_r;
  std::string _string2_r;
  AsyncFw::DataArray _da1_r;
  AsyncFw::DataArray _da2_r;
  AsyncFw::DataArrayList _list_r;
  int8_t _int8_r;
  int64_t _int64_r;

  AsyncFw::DataArray _da_r = AsyncFw::DataArray::uncompress(_da_w);
  AsyncFw::DataStream _ds_r(_da_r);
  _ds_r >> _string1_r >> _string2_r >> _da1_r >> _da2_r >> _list_r >> _int8_r >> _int64_r;

  lsInfoCyan() << _string1_r << _string2_r << _da1_r << _da2_r << _list_r << _int8_r << _int64_r;
  for (const AsyncFw::DataArray &da : _list_r) { lsInfoCyan() << da; }

  return 0;
}
//! [snippet]
