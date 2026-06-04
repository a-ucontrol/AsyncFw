/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file File.h @brief Definition of the File class for file system operations. */

#include <ios>
#include <limits>

namespace AsyncFw {
class DataArray;
/** @class File File.h <AsyncFw/File> @brief Provides basic file I/O operations.
@details A wrapper over the standard std::fstream. The class object is non-copyable and non-movable. */
class File {
public:
  /** @brief Constructs a File object. @param fn Optional file path */
  File(const std::string & = {});
  /** @brief Destructor. Closes the file if open and frees internal resources. */
  ~File();
  /** @brief Opens a file with the specified name and access mode. @param fn The path or name of the file to open. @param m The file open mode flags (defaults to binary input). @return True if the file was opened successfully. */
  bool open(const std::string &, std::ios::openmode = std::ios::binary | std::ios::in);
  /** @brief Opens the file using the previously specified name. @param m The file open mode flags (defaults to binary input). @return True if the file was opened successfully. */
  bool open(std::ios::openmode = std::ios::binary | std::ios::in);
  /** @brief Checks if the file is currently open. @return true if the file stream is open, false otherwise. */
  bool isOpen();
  /** @brief Closes the file and flushes outstanding buffers to disk. */
  void close();
  /** @brief Forces internal output buffers to be written to the physical disk. */
  void flush();
  /** @brief Deletes file from disk. */
  void remove();
  /** @brief Returns the cached size of the file determined when it was opened. @return Size of file in bytes. */
  std::size_t size();
  /** @brief Checks if the file physically exists on disk. @return True if the file exists. */
  bool exists();
  /** @brief Reads data from the file into a DataArray container.
  @details If the parameter is set to @b std::numeric_limits<std::size_t>::max(), it reads all remaining data from the current position to the end of the file.
  @param s Number of bytes to read (defaults to @b std::numeric_limits<std::size_t>::max()). @return A filled DataArray object, or an empty one on failure. */
  DataArray read(std::size_t s = std::numeric_limits<std::size_t>::max());
  /** @brief Writes the entire content of a DataArray container to the file. @param da Container containing binary data. @return The number of bytes successfully written, or -1 on error. */
  std::streamsize write(const DataArray &);
  /** @brief Reads data from the file into a raw character buffer. @param b Pointer to the destination memory buffer. @param s Maximum number of bytes to read. @return The actual number of bytes read. */
  std::streamsize read(char *, std::streamsize);
  /** @brief Writes data from a raw buffer to the file. @param b Pointer to the data buffer. @param s Number of bytes to write. @return The number of bytes successfully written, or -1 on error. */
  std::streamsize write(const char *, std::streamsize);
  /** @brief Reads a single text line from the file up to a newline character. @return The extracted string. */
  std::string readLine();
  /** @brief Checks if the internal stream has its error flag (failbit) set. @return True if the last operation failed. */
  bool fail();
  /** @brief Returns the current read position in the file (stream get). @return The current byte offset for reading, or -1 on error. */
  std::streamsize tellg();
  /** @brief Returns the current write position in the file (stream put). @return The current byte offset for writing, or -1 on error. */
  std::streamsize tellp();
  /** @brief Provides direct access to the underlying std::fstream object.
  @warning If the file is opened in any write or modification mode (out or app), calling this method invalidates the internal file size cache (sets size() to @b std::numeric_limits<std::size_t>::max()). @n For read-only modes, the cache remains valid and untouched.
  @return Reference to the std::fstream instance. */
  //std::fstream &fstream();

private:
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
