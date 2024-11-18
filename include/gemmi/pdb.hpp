// Copyright 2017 Global Phasing Ltd.
//
// Read the PDB file format and store it in Structure.
//
// Based on the format spec:
// https://www.wwpdb.org/documentation/file-format-content/format33/v3.3.html
// + support for two-character chain IDs (columns 21 and 22)
// + read segment ID (columns 73-76)
// + read hybrid-36 serial numbers (http://cci.lbl.gov/hybrid_36/)
// + hybrid-36 sequence id for sequences longer than 9999 (no such examples)
// + allow for longer REMARK lines (up to 120 characters)

#ifndef GEMMI_PDB_HPP_
#define GEMMI_PDB_HPP_

#include <cstdio>     // for stdin, size_t
#include <unordered_map>
#include "fileutil.hpp" // for path_basename, file_open
#include "input.hpp"    // for FileStream
#include "model.hpp"    // for Structure, ...

namespace gemmi {

/// Compare the first 4 letters of s, ignoring case, with uppercase record.
/// Both args must have at least 3+1 chars. ' ' and NUL are equivalent in s.
inline bool is_record_type4(const char* s, const char* record) {
  return ialpha4_id(s) == ialpha4_id(record);
}
/// for record "TER": "TER ", TER\n, TER\r, TER\t match, TERE, TER1 don't
inline bool is_record_type3(const char* s, const char* record) {
  return (ialpha4_id(s) & ~0xf) == ialpha4_id(record);
}

/// Returns operations corresponding to 1555, 2555, ... N555
GEMMI_DLL std::vector<Op> read_remark_290(const std::vector<std::string>& raw_remarks);

GEMMI_DLL Structure read_pdb_from_stream(LineReaderBase&& line_reader,
                                         const std::string& source,
                                         PdbReadOptions options);

inline Structure read_pdb_file(const std::string& path,
                               PdbReadOptions options=PdbReadOptions()) {
  auto f = file_open(path.c_str(), "rb");
  return read_pdb_from_stream(LineReader<FileStream>{f.get()}, path, options);
}

inline Structure read_pdb_from_memory(const char* data, size_t size,
                                      const std::string& name,
                                      PdbReadOptions options=PdbReadOptions()) {
  return read_pdb_from_stream(LineReader<MemoryStream>{data, size}, name, options);
}

inline Structure read_pdb_string(const std::string& str,
                                 const std::string& name,
                                 PdbReadOptions options=PdbReadOptions()) {
  return read_pdb_from_memory(str.c_str(), str.length(), name, options);
}

// A function for transparent reading of stdin and/or gzipped files.
template<typename T>
inline Structure read_pdb(T&& input, PdbReadOptions options=PdbReadOptions()) {
  if (input.is_stdin()) {
    return read_pdb_from_stream(LineReader<FileStream>{stdin}, "stdin", options);
  }
  if (input.is_compressed()) {
    using LR = LineReader<decltype(input.get_uncompressing_stream())>;
    return read_pdb_from_stream(LR{input.get_uncompressing_stream()}, input.path(), options);
  }
  return read_pdb_file(input.path(), options);
}

} // namespace gemmi
#endif
