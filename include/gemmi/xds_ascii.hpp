// Copyright 2020 Global Phasing Ltd.
//
// Read XDS files: XDS_ASCII.HKL and INTEGRATE.HKL.

#ifndef GEMMI_XDS_ASCII_HPP_
#define GEMMI_XDS_ASCII_HPP_

#include "atof.hpp"      // for fast_from_chars
#include "atox.hpp"      // for is_space
#include "input.hpp"     // for copy_line_from_stream
#include "fileutil.hpp"  // for file_open
#include "unitcell.hpp"  // for UnitCell
#include "util.hpp"      // for starts_with

namespace gemmi {

// from Pointless docs: likely in-house source, in which case
// the unpolarised value is left unchanged (recognised wavelengths
// are CuKalpha 1.5418 +- 0.0019, Mo 0.7107 +- 0.0002, Cr 2.29 +- 0.01)
inline bool likely_in_house_source(double wavelength) {
  return std::fabs(wavelength - 1.5418) < 0.0019 ||
         std::fabs(wavelength - 0.7107) < 0.0002 ||
         std::fabs(wavelength - 2.29) < 0.01;
}

struct GEMMI_DLL XdsAscii {
  struct Refl {
    Miller hkl;
    int iset = 1;
    double iobs;
    double sigma;
    double xd;
    double yd;
    double zd;
    double rlp;
    double peak;
    double corr;  // is it always integer?
    double maxc;

    // ZD can be negative for a few reflections
    int frame() const { return (int) std::floor(zd + 1); }
  };
  struct Iset {
    int id;
    std::string input_file;
    double wavelength = 0.;
    double cell_constants[6] = {0., 0., 0., 0., 0., 0.};
    //statistics set by gather_iset_statistics()
    int frame_number_min = -1;
    int frame_number_max = -1;
    int frame_count = -1;
    int reflection_count = -1;

    Iset(int id_) : id(id_) {}
  };
  std::string source_path;
  int read_columns = 0;  // doesn't include ITEM_ISET from XSCALE
  int spacegroup_number = 0;
  double cell_constants[6] = {0., 0., 0., 0., 0., 0.};
  Mat33 cell_axes{0.};
  double wavelength = 0.;
  double incident_beam_dir[3] = {0., 0., 0.};
  double oscillation_range = 0.;
  double rotation_axis[3] = {0., 0., 0.};
  double starting_angle = 0.;
  double reflecting_range_esd = 0.;
  int starting_frame = 1;
  int nx = 0;  // detector size - number of pixels
  int ny = 0;
  double qx = 0.;  // pixel size in mm
  double qy = 0.;
  double orgx = 0.;
  double orgy = 0.;
  double detector_distance = 0.;
  std::string generated_by;
  std::string version_str;
  std::vector<Iset> isets;
  std::vector<Refl> data;

  Iset& find_or_add_iset(int id) {
    for (Iset& i : isets)
      if (i.id == id)
        return i;
    isets.emplace_back(id);
    return isets.back();
  }
  void read_stream(LineReaderBase&& reader, const std::string& source);

  template<typename T>
  void read_input(T&& input) {
    if (input.is_stdin()) {
      read_stream(LineReader<FileStream>{stdin}, "stdin");
    } else if (input.is_compressed()) {
      using LR = LineReader<decltype(input.get_uncompressing_stream())>;
      read_stream(LR{input.get_uncompressing_stream()}, input.path());
    } else {
      auto f = file_open(input.path().c_str(), "r");
      read_stream(LineReader<FileStream>{f.get()}, input.path());
    }
  }

  // set a few Iset properties in isets
  void gather_iset_statistics();

  double rot_angle(const Refl& refl) const {
    double z = refl.zd - starting_frame + 1;
    return starting_angle + oscillation_range * z;
  }

  static Vec3 get_normalized(const double (&arr)[3], const char* name) {
    Vec3 vec(arr[0], arr[1], arr[2]);
    double length = vec.length();
    if (length == 0)
      fail("unknown ", name);
    return vec / length;
  }

  // it's already normalized, but just in case normalize it again
  Vec3 get_rotation_axis() const {
    return get_normalized(rotation_axis, "rotation axis");
  }

  // I'm not sure if always |incident_beam_dir| == 1/wavelength
  Vec3 get_s0_direction() const {
    return get_normalized(incident_beam_dir, "incident beam direction");
  }

  bool has_cell_axes() const {
    for (int i = 0; i < 3; ++i)
      if (cell_axes[i][0] == 0 && cell_axes[i][1] == 0 && cell_axes[i][2] == 0)
        return false;
    return true;
  }

  /// Return transition matrix from "Cambridge" frame to XDS frame.
  /// x_xds = M x_cam
  Mat33 calculate_conversion_from_cambridge() const {
    // Cambridge z direction is along the principal rotation axis
    Vec3 z = get_rotation_axis();
    // Cambridge z direction is along beam
    Vec3 x = get_s0_direction();
    Vec3 y = z.cross(x).normalized();
    // beam and rotation axis may not be orthogonal
    x = y.cross(z).normalized();
    return Mat33::from_columns(x, y, z);
  }

  Mat33 get_orientation() const {
    if (!has_cell_axes())
      fail("unknown unit cell axes");
    Vec3 a = cell_axes.row_copy(0);
    Vec3 b = cell_axes.row_copy(1);
    Vec3 c = cell_axes.row_copy(2);
    Vec3 ar = b.cross(c).normalized();
    Vec3 br = c.cross(a);
    Vec3 cr = ar.cross(br).normalized();
    br = cr.cross(ar);
    return Mat33::from_columns(ar, br, cr);
  }

  /// \par p is degree of polarization from range (0,1), as used in XDS.
  void apply_polarization_correction(double p, Vec3 normal);

  /// \par overload is maximally allowed pixel value in a peak (MAXC).
  void eliminate_overloads(double overload) {
    vector_remove_if(data, [&](Refl& r) { return r.maxc > overload; });
  }

  /// \par batchmin lowest allowed batch number.
  void eliminate_batchmin(int batchmin) {
    double minz = batchmin - 1;
    vector_remove_if(data, [&](Refl& r) { return r.zd < minz; });
  }
};

inline XdsAscii read_xds_ascii_file(const std::string& path) {
  auto f = file_open(path.c_str(), "r");
  XdsAscii ret;
  ret.read_stream(LineReader<FileStream>{f.get()}, path);
  return ret;
}

} // namespace gemmi
#endif
