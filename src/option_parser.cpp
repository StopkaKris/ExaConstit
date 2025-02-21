
#include "option_parser.hpp"
#include "RAJA/RAJA.hpp"
#include "TOML_Reader/cpptoml.h"
#include "mfem.hpp"
#include "ECMech_cases.h"
#include "ECMech_evptnWrap.h"
#include "ECMech_const.h"
#include <iostream>
#include <fstream>


inline bool if_file_exists (const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}

using namespace std;
using namespace mfem;
namespace {
   typedef ecmech::evptn::matModel<ecmech::SlipGeom_BCC_A, ecmech::Kin_FCC_A, 
         ecmech::evptn::ThermoElastNCubic, ecmech::EosModelConst<false>>
         VoceBCCModel;
   typedef ecmech::evptn::matModel<ecmech::SlipGeom_BCC_A, ecmech::Kin_FCC_AH, 
            ecmech::evptn::ThermoElastNCubic, ecmech::EosModelConst<false>>
            VoceNLBCCModel;
}
// my_id corresponds to the processor id.
void ExaOptions::parse_options(int my_id)
{
   toml = cpptoml::parse_file(floc);

   // From the toml file it finds all the values related to state and mat'l
   // properties
   get_properties();
   // From the toml file it finds all the values related to the BCs
   get_bcs();
   // From the toml file it finds all the values related to the model
   get_model();
   // From the toml file it finds all the values related to the time
   get_time_steps();
   // From the toml file it finds all the values related to the visualizations
   get_visualizations();
   // From the toml file it finds all the values related to the Solvers
   get_solvers();
   // From the toml file it finds all the values related to the mesh
   get_mesh();
   // If the processor is set 0 then the options are printed out.
   if (my_id == 0) {
      print_options();
   }
}

// From the toml file it finds all the values related to state and mat'l
// properties
void ExaOptions::get_properties()
{
   double _temp_k = toml->get_qualified_as<double>("Properties.temperature").value_or(298.);

   if (_temp_k <= 0.0) {
      MFEM_ABORT("Properties.temperature is given in Kelvins and therefore can't be less than 0");
   }

   temp_k = _temp_k;

   // Material properties are obtained first
   auto prop_table = toml->get_table_qualified("Properties.Matl_Props");
   // Check to see if our table exists
   if (prop_table != nullptr) {
      std::string _props_file = prop_table->get_as<std::string>("floc").value_or("props.txt");
      props_file = _props_file;
      if (!if_file_exists(props_file))
      {
         MFEM_ABORT("Property file does not exist");
      }
      nProps = prop_table->get_as<int>("num_props").value_or(1);
   } 
   else {
      MFEM_ABORT("Properties.Matl_Props table was not provided in toml file");
   }

   // State variable properties are now obtained
   auto state_table = toml->get_table_qualified("Properties.State_Vars");
   // Check to see if our table exists
   if (state_table != nullptr) {
      numStateVars = state_table->get_as<int>("num_vars").value_or(1);
      std::string _state_file = state_table->get_as<std::string>("floc").value_or("state.txt");
      state_file = _state_file;
      if (!if_file_exists(state_file))
      {
         MFEM_ABORT("State file does not exist");
      }
   }
   else {
      MFEM_ABORT("Properties.State_Vars table was not provided in toml file");
   }

   // Grain related properties are now obtained
   auto grain_table = toml->get_table_qualified("Properties.Grain");
   // Check to see if our table exists
   if (grain_table != nullptr) {
      grain_statevar_offset = grain_table->get_as<int>("ori_state_var_loc").value_or(-1);
      grain_custom_stride = grain_table->get_as<int>("ori_stride").value_or(0);
      std::string _ori_type = grain_table->get_as<std::string>("ori_type").value_or("euler");
      ngrains = grain_table->get_as<int>("num_grains").value_or(0);
      std::string _ori_file = grain_table->get_as<std::string>("ori_floc").value_or("ori.txt");
      ori_file = _ori_file;
      std::string _grain_map = grain_table->get_as<std::string>("grain_floc").value_or("grain_map.txt");
      grain_map = _grain_map;

      // I still can't believe C++ doesn't allow strings to be used in switch statements...
      if ((_ori_type == "euler") || _ori_type == "Euler" || (_ori_type == "EULER")) {
         ori_type = OriType::EULER;
      }
      else if ((_ori_type == "quat") || (_ori_type == "Quat") || (_ori_type == "quaternion") || (_ori_type == "Quaternion")) {
         ori_type = OriType::QUAT;
      }
      else if ((_ori_type == "custom") || (_ori_type == "Custom") || (_ori_type == "CUSTOM")) {
         ori_type = OriType::CUSTOM;
      }
      else {
         MFEM_ABORT("Properties.Grain.ori_type was not provided a valid type.");
         ori_type = OriType::NOTYPE;
      }
   } // end of if statement for grain data
} // End of propert parsing

// From the toml file it finds all the values related to the BCs
void ExaOptions::get_bcs()
{

   // Getting out arrays of values isn't always the simplest thing to do using
   // this TOML libary.
   changing_bcs = toml->get_qualified_as<bool>("BCs.changing_ess_bcs").value_or(false);
   if (!changing_bcs) {
      auto ess_ids = toml->get_qualified_array_of<int64_t>("BCs.essential_ids");
      std::vector<int> _essential_ids;
      for (const auto& val: *ess_ids) {
         _essential_ids.push_back(val);
      }

      if (_essential_ids.empty()) {
         MFEM_ABORT("BCs.essential_ids was not provided any values.");
      }
      map_ess_id[0] = std::vector<int>();
      map_ess_id[1] = _essential_ids;


      // Getting out arrays of values isn't always the simplest thing to do using
      // this TOML libary.
      auto ess_comps = toml->get_qualified_array_of<int64_t>("BCs.essential_comps");
      std::vector<int> _essential_comp;
      for (const auto& val: *ess_comps) {
         _essential_comp.push_back(val);
      }

      if (_essential_comp.empty()) {
         MFEM_ABORT("BCs.essential_comps was not provided any values.");
      }

      map_ess_comp[0] = std::vector<int>();
      map_ess_comp[1] = _essential_comp;

      // Getting out arrays of values isn't always the simplest thing to do using
      // this TOML libary.
      auto ess_vals = toml->get_qualified_array_of<double>("BCs.essential_vals");
      std::vector<double> _essential_vals;
      for (const auto& val: *ess_vals) {
         _essential_vals.push_back(val);
      }

      if (_essential_vals.empty()) {
         MFEM_ABORT("BCs.essential_vals was not provided any values.");
      }

      map_ess_vel[0] = std::vector<double>();
      map_ess_vel[1] = _essential_vals;
      updateStep.push_back(1);
   }
   else {
      auto upd_step = toml->get_qualified_array_of<int64_t>("BCs.update_steps");
      for (const auto& val: *upd_step) {
         updateStep.push_back(val);
      }

      if (updateStep.empty()) {
         MFEM_ABORT("BCs.update_steps was not provided any values.");
      }
      if (std::find(updateStep.begin(), updateStep.end(), 1) == updateStep.end()) {
         MFEM_ABORT("BCs.update_steps must contain 1 in the array");
      }

      int size = updateStep.size();
      auto nested_ess_ids = toml->get_qualified_array_of<cpptoml::array>("BCs.essential_ids");
      int ilength = 0;
      map_ess_id[0] = std::vector<int>();
      for (const auto &vec : *nested_ess_ids) {
         auto vals = (*vec).get_array_of<int64_t>();
         int key = updateStep.at(ilength);
         map_ess_id[key] = std::vector<int>();
         for (const auto &val : *vals) {
            map_ess_id[key].push_back(val);
         }
         if (map_ess_id[key].empty()) {
            MFEM_ABORT("BCs.essential_ids contains empty array.");
         }
         ilength += 1;
      }

      if (ilength != size) {
         MFEM_ABORT("BCs.essential_ids did not contain the same number of arrays as number of update steps");
      }

      auto nested_ess_comps = toml->get_qualified_array_of<cpptoml::array>("BCs.essential_comps");
      ilength = 0;
      map_ess_comp[0] = std::vector<int>();
      for (const auto &vec : *nested_ess_comps) {
         auto vals = (*vec).get_array_of<int64_t>();
         int key = updateStep.at(ilength);
         map_ess_comp[key] = std::vector<int>();
         for (const auto &val : *vals) {
            map_ess_comp[key].push_back(val);
         }
         if (map_ess_comp[key].empty()) {
            MFEM_ABORT("BCs.essential_comps contains empty array.");
         }
         ilength += 1;
      }

      if (ilength != size) {
         MFEM_ABORT("BCs.essential_comps did not contain the same number of arrays as number of update steps");
      }

      auto nested_ess_vals = toml->get_qualified_array_of<cpptoml::array>("BCs.essential_vals");
      ilength = 0;
      map_ess_vel[0] = std::vector<double>();
      for (const auto &vec : *nested_ess_vals) {
         auto vals = (*vec).get_array_of<double>();
         int key = updateStep.at(ilength);
         map_ess_vel[key] = std::vector<double>();
         for (const auto &val : *vals) {
            map_ess_vel[key].push_back(val);
         }
         if (map_ess_vel[key].empty()) {
            MFEM_ABORT("BCs.essential_vals contains empty array.");
         }
         ilength += 1;
      }

      if (ilength != size) {
         MFEM_ABORT("BCs.essential_vals did not contain the same number of arrays as number of update steps");
      }

   }
} // end of parsing BCs

// From the toml file it finds all the values related to the model
void ExaOptions::get_model()
{
   std::string _mech_type = toml->get_qualified_as<std::string>("Model.mech_type").value_or("");

   // I still can't believe C++ doesn't allow strings to be used in switch statements...
   if ((_mech_type == "umat") || (_mech_type == "Umat") || (_mech_type == "UMAT") || (_mech_type == "UMat")) {
      mech_type = MechType::UMAT;
   }
   else if ((_mech_type == "exacmech") || (_mech_type == "Exacmech") || (_mech_type == "ExaCMech") || (_mech_type == "EXACMECH")) {
      mech_type = MechType::EXACMECH;
   }
   else {
      MFEM_ABORT("Model.mech_type was not provided a valid type.");
      mech_type = MechType::NOTYPE;
   }

   cp = toml->get_qualified_as<bool>("Model.cp").value_or(false);

   if (mech_type == MechType::EXACMECH) {
      if (!cp) {
         MFEM_ABORT("Model.cp needs to be set to true when using ExaCMech based models.");
      }

      if (ori_type != OriType::QUAT) {
         MFEM_ABORT("Properties.Grain.ori_type is not set to quaternion for use with an ExaCMech model.");
         xtal_type = XtalType::NOTYPE;
      }

      grain_statevar_offset = ecmech::evptn::iHistLbQ;

      auto exacmech_table = toml->get_table_qualified("Model.ExaCMech");

      std::string _xtal_type = exacmech_table->get_as<std::string>("xtal_type").value_or("");
      std::string _slip_type = exacmech_table->get_as<std::string>("slip_type").value_or("");

      if ((_xtal_type == "fcc") || (_xtal_type == "FCC")) {
         xtal_type = XtalType::FCC;
         int num_state_vars_check = ecmech::matModelEvptn_FCC_A::numHist + ecmech::ne + 1 - 4;
         if (numStateVars != num_state_vars_check) {
            MFEM_ABORT("Properties.State_Vars.num_vars needs " << num_state_vars_check << " values for a "
                       "face cubic material when using an ExaCMech model. Note: the number of values for a quaternion "
                       "are not included in this count.");
         }
      }
      else if ((_xtal_type == "bcc") || (_xtal_type == "BCC")) {
         xtal_type = XtalType::BCC;
         // We'll probably need to modify this whenever we add support for the other BCC variations in
         // here due to the change in number of slip systems.
         int num_state_vars_check = ecmech::matModelEvptn_BCC_A::numHist + ecmech::ne + 1 - 4;
         if (numStateVars != num_state_vars_check) {
            MFEM_ABORT("Properties.State_Vars.num_vars needs " << num_state_vars_check << " values for a "
                       "body center cubic material when using an ExaCMech model. Note: the number of values for a quaternion "
                       "are not included in this count.");
         }
      }
      else if ((_xtal_type == "hcp") || (_xtal_type == "HCP")) {
         xtal_type = XtalType::HCP;
         int num_state_vars_check = ecmech::matModelEvptn_HCP_A::numHist + ecmech::ne + 1 - 4;
         if (numStateVars != num_state_vars_check) {
            MFEM_ABORT("Properties.State_Vars.num_vars needs " << num_state_vars_check << " values for a "
                       "hexagonal material when using an ExaCMech model. Note: the number of values for a quaternion "
                       "are not included in this count.");
         }
      }
      else {
         MFEM_ABORT("Model.ExaCMech.xtal_type was not provided a valid type.");
         xtal_type = XtalType::NOTYPE;
      }

      if ((_slip_type == "mts") || (_slip_type == "MTS") || (_slip_type == "mtsdd") || (_slip_type == "MTSDD")) {
         slip_type = SlipType::MTSDD;
         if (xtal_type == XtalType::FCC) {
            if (nProps != ecmech::matModelEvptn_FCC_B::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << ecmech::matModelEvptn_FCC_B::nParams <<
                          " values for the MTSDD option and FCC option");
            }
         }
         else if (xtal_type == XtalType::BCC) {
            if (nProps != ecmech::matModelEvptn_BCC_A::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << ecmech::matModelEvptn_BCC_A::nParams <<
                          " values for the MTSDD option and BCC option");
            }
         }
         else if (xtal_type == XtalType::HCP) {
            if (nProps != ecmech::matModelEvptn_HCP_A::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << ecmech::matModelEvptn_HCP_A::nParams <<
                          " values for the MTSDD option and HCP option");
            }
         }
      }
      else if ((_slip_type == "powervoce") || (_slip_type == "PowerVoce") || (_slip_type == "POWERVOCE")) {
         slip_type = SlipType::POWERVOCE;
         if (xtal_type == XtalType::FCC) {
            if (nProps != ecmech::matModelEvptn_FCC_A::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << ecmech::matModelEvptn_FCC_A::nParams <<
                          " values for the PowerVoce option and FCC option");
            }
         }
         else if (xtal_type == XtalType::BCC) {
            if (nProps != VoceBCCModel::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << VoceBCCModel::nParams <<
                          " values for the PowerVoce option and BCC option");
            }
         }  
         else {
            MFEM_ABORT("Model.ExaCMech.slip_type can not be PowerVoce for HCP materials.")
         }
      }
      else if ((_slip_type == "powervocenl") || (_slip_type == "PowerVoceNL") || (_slip_type == "POWERVOCENL")) {
         slip_type = SlipType::POWERVOCENL;
         if (xtal_type == XtalType::FCC) {
            if (nProps != ecmech::matModelEvptn_FCC_AH::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << ecmech::matModelEvptn_FCC_AH::nParams <<
                          " values for the PowerVoceNL option and FCC option");
            }
         }
         else if (xtal_type == XtalType::BCC) {
            if (nProps != VoceNLBCCModel::nParams) {
               MFEM_ABORT("Properties.Matl_Props.num_props needs " << VoceNLBCCModel::nParams <<
                          " values for the PowerVoceNL option and BCC option");
            }
         }
         else {
            MFEM_ABORT("Model.ExaCMech.slip_type can not be PowerVoceNL for HCP materials.")
         }
      }
      else {
         MFEM_ABORT("Model.ExaCMech.slip_type was not provided a valid type.");
         slip_type = SlipType::NOTYPE;
      }
   }
} // end of model parsing

// From the toml file it finds all the values related to the time
void ExaOptions::get_time_steps()
{
   // First look at the fixed time stuff
   auto fixed_table = toml->get_table_qualified("Time.Fixed");
   // check to see if our table exists
   if (fixed_table != nullptr) {
      dt_cust = false;
      dt = fixed_table->get_as<double>("dt").value_or(1.0);
      t_final = fixed_table->get_as<double>("t_final").value_or(1.0);
   }
   // Time to look at our custom time table stuff
   auto cust_table = toml->get_table_qualified("Time.Custom");
   // check to see if our table exists
   if (cust_table != nullptr) {
      dt_cust = true;
      nsteps = cust_table->get_as<int>("nsteps").value_or(1);
      std::string _dt_file = cust_table->get_as<std::string>("floc").value_or("custom_dt.txt");
      dt_file = _dt_file;
   }
} // end of time step parsing

// From the toml file it finds all the values related to the visualizations
void ExaOptions::get_visualizations()
{
   vis_steps = toml->get_qualified_as<int>("Visualizations.steps").value_or(1);
   visit = toml->get_qualified_as<bool>("Visualizations.visit").value_or(false);
   conduit = toml->get_qualified_as<bool>("Visualizations.conduit").value_or(false);
   paraview = toml->get_qualified_as<bool>("Visualizations.paraview").value_or(false);
   adios2 = toml->get_qualified_as<bool>("Visualizations.adios2").value_or(false);
   if (conduit || adios2) {
      if (conduit) {
        #ifndef MFEM_USE_CONDUIT
         MFEM_ABORT("MFEM was not built with conduit.")
        #endif
      }
      else {
         #ifndef MFEM_USE_ADIOS2
         MFEM_ABORT("MFEM was not built with ADIOS2");
         #endif
      }
   }
   std::string _basename = toml->get_qualified_as<std::string>("Visualizations.floc").value_or("results/exaconstit");
   basename = _basename;
   std::string _avg_stress_fname = toml->get_qualified_as<std::string>("Visualizations.avg_stress_fname").value_or("avg_stress.txt");
   avg_stress_fname = _avg_stress_fname;
   bool _additional_avgs = toml->get_qualified_as<bool>("Visualizations.additional_avgs").value_or(false);
   additional_avgs = _additional_avgs;
   std::string _avg_def_grad_fname = toml->get_qualified_as<std::string>("Visualizations.avg_def_grad_fname").value_or("avg_def_grad.txt");
   avg_def_grad_fname = _avg_def_grad_fname;
   std::string _avg_pl_work_fname = toml->get_qualified_as<std::string>("Visualizations.avg_pl_work_fname").value_or("avg_pl_work.txt");
   avg_pl_work_fname = _avg_pl_work_fname;
   std::string _avg_dp_tensor_fname = toml->get_qualified_as<std::string>("Visualizations.avg_dp_tensor_fname").value_or("avg_dp_tensor.txt");
   avg_dp_tensor_fname = _avg_dp_tensor_fname;
} // end of visualization parsing

// From the toml file it finds all the values related to the Solvers
void ExaOptions::get_solvers()
{
   std::string _assembly = toml->get_qualified_as<std::string>("Solvers.assembly").value_or("FULL");
   if ((_assembly == "FULL") || (_assembly == "full")) {
      assembly = Assembly::FULL;
   }
   else if ((_assembly == "PA") || (_assembly == "pa")) {
      assembly = Assembly::PA;
   }
   else if ((_assembly == "EA") || (_assembly == "ea")) {
      assembly = Assembly::EA;
   }
   else {
      MFEM_ABORT("Solvers.assembly was not provided a valid type.");
      assembly = Assembly::NOTYPE;
   }

   std::string _rtmodel = toml->get_qualified_as<std::string>("Solvers.rtmodel").value_or("CPU");
   if ((_rtmodel == "CPU") || (_rtmodel == "cpu")) {
      rtmodel = RTModel::CPU;
   }
   #if defined(RAJA_ENABLE_OPENMP)
   else if ((_rtmodel == "OPENMP") || (_rtmodel == "OpenMP")|| (_rtmodel == "openmp")) {
      rtmodel = RTModel::OPENMP;
   }
   #endif
   #if defined(RAJA_ENABLE_CUDA)
   else if ((_rtmodel == "CUDA") || (_rtmodel == "cuda")) {
      if (assembly == Assembly::FULL) {
         MFEM_ABORT("Solvers.rtmodel can't be CUDA if Solvers.rtmodel is FULL.");
      }
      rtmodel = RTModel::CUDA;
   }
   #endif
   else {
      MFEM_ABORT("Solvers.rtmodel was not provided a valid type.");
      rtmodel = RTModel::NOTYPE;
   }

   // Obtaining information related to the newton raphson solver
   auto nr_table = toml->get_table_qualified("Solvers.NR");
   if (nr_table != nullptr) {
      std::string _solver = nr_table->get_as<std::string>("nl_solver").value_or("NR");
      if ((_solver == "nr") || (_solver == "NR")) {
         nl_solver = NLSolver::NR;
      }
      else if ((_solver == "nrls") || (_solver == "NRLS")) {
         nl_solver = NLSolver::NRLS;
      }
      else {
         MFEM_ABORT("Solvers.NR.nl_solver was not provided a valid type.");
         nl_solver = NLSolver::NOTYPE;
      }
      newton_iter = nr_table->get_as<int>("iter").value_or(25);
      newton_rel_tol = nr_table->get_as<double>("rel_tol").value_or(1e-5);
      newton_abs_tol = nr_table->get_as<double>("abs_tol").value_or(1e-10);
   } // end of NR info

   std::string _integ_model = toml->get_qualified_as<std::string>("Solvers.integ_model").value_or("FULL");
   if ((_integ_model == "FULL") || (_integ_model == "full")) {
      integ_type = IntegrationType::FULL;
   }
   else if ((_integ_model == "BBAR") || (_integ_model == "bbar")) {
      integ_type = IntegrationType::BBAR;
      if (nl_solver == NLSolver::NR) {
         mfem::out << "BBar method performs better when paired with a NR solver with line search" << std::endl;
      }
   }

   // Now getting information about the Krylov solvers used to the linearized
   // system of equations of the nonlinear problem.
   auto iter_table = toml->get_table_qualified("Solvers.Krylov");
   if (iter_table != nullptr) {
      krylov_iter = iter_table->get_as<int>("iter").value_or(200);
      krylov_rel_tol = iter_table->get_as<double>("rel_tol").value_or(1e-10);
      krylov_abs_tol = iter_table->get_as<double>("abs_tol").value_or(1e-30);
      std::string _solver = iter_table->get_as<std::string>("solver").value_or("GMRES");
      if ((_solver == "GMRES") || (_solver == "gmres")) {
         solver = KrylovSolver::GMRES;
      }
      else if ((_solver == "PCG") || (_solver == "pcg")) {
         solver = KrylovSolver::PCG;
      }
      else if ((_solver == "MINRES") || (_solver == "minres")) {
         solver = KrylovSolver::MINRES;
      }
      else {
         MFEM_ABORT("Solvers.Krylov.solver was not provided a valid type.");
         solver = KrylovSolver::NOTYPE;
      }
   } // end of krylov solver info
} // end of solver parsing

// From the toml file it finds all the values related to the mesh
void ExaOptions::get_mesh()
{
   // Refinement of the mesh and element order
   ser_ref_levels = toml->get_qualified_as<int>("Mesh.ref_ser").value_or(0);
   par_ref_levels = toml->get_qualified_as<int>("Mesh.ref_par").value_or(0);
   order = toml->get_qualified_as<int>("Mesh.p_refinement").value_or(1);
   // file location of the mesh
   std::string _mesh_file = toml->get_qualified_as<std::string>("Mesh.floc").value_or("../../data/cube-hex-ro.mesh");
   mesh_file = _mesh_file;
   // Type of mesh that we're reading/going to generate
   std::string mtype = toml->get_qualified_as<std::string>("Mesh.type").value_or("other");
   if ((mtype == "cubit") || (mtype == "Cubit") || (mtype == "CUBIT")) {
      mesh_type = MeshType::CUBIT;
   }
   else if ((mtype == "auto") || (mtype == "Auto") || (mtype == "AUTO")) {
      mesh_type = MeshType::AUTO;
      auto auto_table = toml->get_table_qualified("Mesh.Auto");

      // Basics to generate at least 1 element of length 1.
      // mx = auto_table->get_as<double>("length").value_or(1.0);
      auto mx = auto_table->get_qualified_array_of<double>("length");
      std::vector<double> _mxyz;
      for (const auto& val: *mx) {
         _mxyz.push_back(val);
      }

      if (_mxyz.size() != 3) {
         MFEM_ABORT("Mesh.Auto.length was not provided a valid array of size 3.");
      }
      mxyz[0] = _mxyz[0];
      mxyz[1] = _mxyz[1];
      mxyz[2] = _mxyz[2];

      // nx = auto_table->get_as<int>("ncuts").value_or(1);
      auto nx = auto_table->get_qualified_array_of<int64_t>("ncuts");
      std::vector<int> _nxyz;
      for (const auto& val: *nx) {
         _nxyz.push_back(val);
      }

      if (_nxyz.size() != 3) {
         MFEM_ABORT("Mesh.Auto.ncuts was not provided a valid array of size 3.");
      }
      nxyz[0] = _nxyz[0];
      nxyz[1] = _nxyz[1];
      nxyz[2] = _nxyz[2];
   }
   else if ((mtype == "other") || (mtype == "Other") || (mtype == "OTHER")) {
      mesh_type = MeshType::OTHER;
   }
   else {
      MFEM_ABORT("Mesh.type was not provided a valid type.");
      mesh_type = MeshType::NOTYPE;
   } // end of mesh type parsing

   if (mesh_type == MeshType::OTHER || mesh_type == MeshType::CUBIT) {
      if (!if_file_exists(mesh_file))
      {
         MFEM_ABORT("Mesh file does not exist");
      }
   }
} // End of mesh parsing

void ExaOptions::print_options()
{
   std::cout << "Mesh file location: " << mesh_file << "\n";
   std::cout << "Mesh type: ";
   if (mesh_type == MeshType::OTHER) {
      std::cout << "other";
   }
   else if (mesh_type == MeshType::CUBIT) {
      std::cout << "cubit";
   }
   else {
      std::cout << "auto";
   }
   std::cout << "\n";

   std::cout << "Edge dimensions (mx, my, mz): " << mxyz[0] << " " << mxyz[1] << " " << mxyz[2] << "\n";
   std::cout << "Number of cells on an edge (nx, ny, nz): " << nxyz[0] << " " << nxyz[1] << " " << nxyz[2] << "\n";

   std::cout << "Serial Refinement level: " << ser_ref_levels << "\n";
   std::cout << "Parallel Refinement level: " << par_ref_levels << "\n";
   std::cout << "P-refinement level: " << order << "\n";

   std::cout << std::boolalpha;
   std::cout << "Custom dt flag (dt_cust): " << dt_cust << "\n";

   if (dt_cust) {
      std::cout << "Number of time steps (nsteps): " << nsteps << "\n";
      std::cout << "Custom time file loc (dt_file): " << dt_file << "\n";
   }
   else {
      std::cout << "Constant time stepping on \n";
      std::cout << "Final time (t_final): " << t_final << "\n";
      std::cout << "Time step (dt): " << dt << "\n";
   }

   std::cout << "Visit flag: " << visit << "\n";
   std::cout << "Conduit flag: " << conduit << "\n";
   std::cout << "Paraview flag: " << paraview << "\n";
   std::cout << "ADIOS2 flag: " << adios2 << "\n";
   std::cout << "Visualization steps: " << vis_steps << "\n";
   std::cout << "Visualization directory: " << basename << "\n";

   std::cout << "Average stress filename: " << avg_stress_fname << std::endl;
   if (additional_avgs)
   {
      std::cout << "Additional averages being computed" << std::endl;
      std::cout << "Average deformation gradient filename: " << avg_def_grad_fname << std::endl;
      std::cout << "Average plastic work filename: " << avg_pl_work_fname << std::endl;
      std::cout << "Average plastic strain rate tensor filename: " << avg_dp_tensor_fname << std::endl;
   }
   else
   {
      std::cout << "No additional averages being computed" << std::endl;
   }
   std::cout << "Average stress filename: " << avg_stress_fname << std::endl;

   if (nl_solver == NLSolver::NR) {
      std::cout << "Nonlinear Solver is Newton Raphson \n";
   }
   else if (nl_solver == NLSolver::NRLS) {
      std::cout << "Nonlinear Solver is Newton Raphson with a line search\n";
   }

   std::cout << "Newton Raphson rel. tol.: " << newton_rel_tol << "\n";
   std::cout << "Newton Raphson abs. tol.: " << newton_abs_tol << "\n";
   std::cout << "Newton Raphson # of iter.: " << newton_iter << "\n";
   std::cout << "Newton Raphson grad debug: " << grad_debug << "\n";

   if (integ_type == IntegrationType::FULL) {
      std::cout << "Integration Type: Full \n";
   }
   else if (integ_type == IntegrationType::BBAR) {
      std::cout << "Integration Type: BBar \n";
   }

   std::cout << "Krylov solver: ";
   if (solver == KrylovSolver::GMRES) {
      std::cout << "GMRES";
   }
   else if (solver == KrylovSolver::PCG) {
      std::cout << "PCG";
   }
   else {
      std::cout << "MINRES";
   }
   std::cout << "\n";

   std::cout << "Krylov solver rel. tol.: " << krylov_rel_tol << "\n";
   std::cout << "Krylov solver abs. tol.: " << krylov_abs_tol << "\n";
   std::cout << "Krylov solver # of iter.: " << krylov_iter << "\n";

   std::cout << "Matrix Assembly is: ";
   if (assembly == Assembly::FULL) {
      std::cout << "Full Assembly\n";
   }
   else if (assembly == Assembly::PA) {
      std::cout << "Partial Assembly\n";
   }
   else {
      std::cout << "Element Assembly\n";
   }

   std::cout << "Runtime model is: ";
   if (rtmodel == RTModel::CPU) {
      std::cout << "CPU\n";
   }
   else if (rtmodel == RTModel::CUDA) {
      std::cout << "CUDA\n";
   }
   else if (rtmodel == RTModel::OPENMP) {
      std::cout << "OpenMP\n";
   }

   std::cout << "Mechanical model library being used ";

   if (mech_type == MechType::UMAT) {
      std::cout << "UMAT\n";
   }
   else if (mech_type == MechType::EXACMECH) {
      std::cout << "ExaCMech\n";
      std::cout << "Crystal symmetry group is ";
      if (xtal_type == XtalType::FCC) {
         std::cout << "FCC\n";
      }
      else if (xtal_type == XtalType::BCC) {
         std::cout << "BCC\n";
      }
      else if (xtal_type == XtalType::HCP) {
         std::cout << "HCP\n";
      }

      std::cout << "Slip system and hardening model being used is ";

      if (slip_type == SlipType::MTSDD) {
         std::cout << "MTS slip like kinetics with dislocation density based hardening\n";
      }
      else if (slip_type == SlipType::POWERVOCE) {
         std::cout << "Power law slip kinetics with a linear Voce hardening law\n";
      }
      else if (slip_type == SlipType::POWERVOCENL) {
         std::cout << "Power law slip kinetics with a nonlinear Voce hardening law\n";
      }
   }

   std::cout << "Xtal Plasticity being used: " << cp << "\n";

   std::cout << "Orientation file location: " << ori_file << "\n";
   std::cout << "Grain map file location: " << grain_map << "\n";
   std::cout << "Number of grains: " << ngrains << "\n";

   std::cout << "Orientation type: ";
   if (ori_type == OriType::EULER) {
      std::cout << "euler";
   }
   else if (ori_type == OriType::QUAT) {
      std::cout << "quaternion";
   }
   else {
      std::cout << "custom";
   }
   std::cout << "\n";

   std::cout << "Custom stride to read grain map file: " << grain_custom_stride << "\n";
   std::cout << "Orientation offset in state variable file: " << grain_statevar_offset << "\n";

   std::cout << "Number of properties: " << nProps << "\n";
   std::cout << "Property file location: " << props_file << "\n";

   std::cout << "Number of state variables: " << numStateVars << "\n";
   std::cout << "State variable file location: " << state_file << "\n";

   for (const auto key: updateStep)
   {
      std::cout << "Starting on step " << key << " essential BCs values are:" << std::endl;
      std::cout << "Essential ids are set as: ";
      for (const auto & val: map_ess_id.at(key)) {
         std::cout << val << " ";
      }
      std::cout << std::endl << "Essential components are set as: ";
      for (const auto & val: map_ess_comp.at(key)) {
         std::cout << val << " ";
      }
      std::cout << std::endl << "Essential boundary values are set as: ";
      for (const auto & val: map_ess_vel.at(key)) {
         std::cout << val << " ";
      }
      std::cout << std::endl;
   }
} // End of printing out options
