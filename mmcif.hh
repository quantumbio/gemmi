// Copyright 2017 Global Phasing Ltd.
//
// Read mmcif (PDBx/mmCIF) file into a Structure from model.hh.

#ifndef GEMMI_MMCIF_HH_
#define GEMMI_MMCIF_HH_

#include <string>
#include <iostream> // temporary
#include "cif.hh"
#include "numb.hh"
#include "model.hh"

namespace gemmi {
namespace mol {

template<typename T>
T* find_or_add(std::vector<T>& vec, const std::string& name) {
  auto it = std::find_if(vec.begin(), vec.end(), [&name](const T& m) {
      return m.name == name;
  });
  if (it != vec.end())
    return &*it;
  vec.emplace_back(name);
  return &vec.back();
}

inline Residue* find_or_add_r(std::vector<Residue>& vec, int seq_id,
                              int auth_seq_id, const std::string& name) {
  auto it = std::find_if(vec.begin(), vec.end(), [&](const Residue& r) {
      return r.seq_id == seq_id && r.name == name &&
             (r.seq_id != Residue::UnknownId || r.auth_seq_id == auth_seq_id);
  });
  if (it != vec.end())
    return &*it;
  vec.emplace_back(seq_id, auth_seq_id, name);
  return &vec.back();
}

inline Structure structure_from_cif_block(const cif::Block& block) {
  Structure st;

  // unit cell and symmetry
  cif::TableView cell = block.find("_cell.",
                                   {"length_a", "length_b", "length_c",
                                   "angle_alpha", "angle_beta", "angle_gamma"});
  if (cell.ok()) {
    auto c = cell.one();
    st.cell.set(c.as_num(0), c.as_num(1), c.as_num(2),
                c.as_num(3), c.as_num(4), c.as_num(5));
  }
  st.sg_hm = block.find_string("_symmetry.space_group_name_H-M");

  auto add_info = [&](std::string tag) {
    cif::TableView t = block.find(tag);
    if (t.length() >= 1)
      st.info[tag] = t[0].as_str(0);
  };
  add_info("_entry.id");
  add_info("_cell.Z_PDB");
  add_info("_struct.title");
  add_info("_exptl.method");
  add_info("_database_PDB_rev.date_original");
  add_info("_struct_keywords.pdbx_keywords");
  add_info("_struct_keywords.text");

  cif::TableView ncs_oper = block.find("_struct_ncs_oper.",
             {"matrix[1][1]", "matrix[1][2]", "matrix[1][3]",
              "matrix[2][1]", "matrix[2][2]", "matrix[2][3]",
              "matrix[3][1]", "matrix[3][2]", "matrix[3][3]",
              "vector[1]", "vector[2]", "vector[3]", "code"});
  for (auto op : ncs_oper) {
    Matrix33 mat{op.as_num(0), op.as_num(1), op.as_num(2),
                 op.as_num(3), op.as_num(4), op.as_num(5),
                 op.as_num(6), op.as_num(7), op.as_num(8)};
    Position vec{op.as_num(9), op.as_num(10), op.as_num(11)};
    bool given = (op.as_str(12) == "given");
    st.ncs.push_back({given, mat, vec});
  }

  // sequence
  // TODO

  // atom list
  enum { kId=0, kSymbol, kAtomId, kAltId, kCompId, kAsymId, kSeqId, kInsCode,
         kX, kY, kZ, kOcc, kBiso, kCharge, kAuthSeqId, kAuthAsymId, kModelNum };
  cif::TableView atom_table = block.find("_atom_site.",
                                         {"id",
                                          "type_symbol",
                                          "label_atom_id",
                                          "label_alt_id",
                                          "label_comp_id",
                                          "label_asym_id",
                                          "label_seq_id",
                                          "pdbx_PDB_ins_code",
                                          "Cartn_x",
                                          "Cartn_y",
                                          "Cartn_z",
                                          "occupancy",
                                          "B_iso_or_equiv",
                                          "pdbx_formal_charge",
                                          "auth_seq_id",
                                          "auth_asym_id",
                                          "pdbx_PDB_model_num"});
  cif::TableView aniso_tab = block.find("_atom_site_anisotrop.",
                                        {"id", "U[1][1]", "U[2][2]", "U[3][3]",
                                         "U[1][2]", "U[1][3]", "U[2][3]"});
  auto aniso_iter = aniso_tab.begin();
  Model *model = nullptr;
  Chain *chain = nullptr;
  Residue *resi = nullptr;
  for (auto row : atom_table) {
    if (!model || row[kModelNum] != model->name) {
      model = find_or_add(st.models, row[kModelNum]);
      chain = nullptr;
    }
    if (!chain || row[kAsymId] != chain->name) {
      chain = find_or_add(model->chains, row[kAsymId]);
      chain->auth_name = row.as_str(kAuthAsymId);
      resi = nullptr;
    }
    int seq_id = cif::as_int(row[kSeqId], Residue::UnknownId);
    int auth_seq_id = cif::as_int(row[kAuthSeqId], Residue::UnknownId);
    if (!resi || seq_id != resi->seq_id || row[kCompId] != resi->name ||
        (seq_id == Residue::UnknownId && resi->auth_seq_id != auth_seq_id)) {
      resi = find_or_add_r(chain->residues, seq_id, auth_seq_id,
                           cif::as_string(row[kCompId]));
      // the insertion code happens to be always a single letter
      assert(row[kInsCode].size() == 1);
      resi->ins_code = cif::as_string(row[kInsCode])[0];
      resi->auth_seq_id = auth_seq_id;
    } else {
      assert(resi->auth_seq_id == auth_seq_id);
    }
    Atom atom;
    atom.name = cif::as_string(row[kAtomId]);
    atom.altloc = cif::as_string(row[kAltId])[0];
    atom.charge = cif::is_null(row[kCharge]) ? 0 : cif::as_int(row[kCharge]);
    atom.element = Element(cif::as_string(row[kSymbol]));
    atom.pos.x = cif::as_number(row[kX]);
    atom.pos.y = cif::as_number(row[kY]);
    atom.pos.z = cif::as_number(row[kZ]);
    atom.occ = cif::as_number(row[kOcc], 1.0);
    atom.b_iso = cif::as_number(row[kBiso], 50.0);

    if (aniso_tab.ok()) {
      bool found = false;
      // normally both tables are in the same order
      if (aniso_iter != aniso_tab.end() && aniso_iter.get(0) == row[kId]) {
        found = true;
      } else {
        for (auto r : aniso_tab)
          if (r[0] == row[kId]) {
            found = true;
            aniso_iter.cur = r.cur;
            break;
          }
      }
      if (found) {
        atom.u11 = cif::as_number(aniso_iter.get(1));
        atom.u22 = cif::as_number(aniso_iter.get(2));
        atom.u33 = cif::as_number(aniso_iter.get(3));
        atom.u12 = cif::as_number(aniso_iter.get(4));
        atom.u13 = cif::as_number(aniso_iter.get(5));
        atom.u23 = cif::as_number(aniso_iter.get(6));
        ++aniso_iter;
      }
    }
    resi->atoms.emplace_back(atom);
  }
  return st;
}

inline Structure read_atoms(const cif::Document& doc) {
  return structure_from_cif_block(doc.sole_block());
}

} // namespace mol
} // namespace gemmi
#endif
// vim:sw=2:ts=2:et
