/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#include "kernel_sum_2d_internal.hpp"
#include "mx_function.hpp"
#include "../profiling.hpp"
#include <sstream>

using namespace std;

namespace casadi {

  KernelSum2DBase* KernelSum2DBase::create(const Function& f,
           const std::pair<int, int> & size,
           double r,
           int n, const Dict& opts)
    {
    // Read the type of parallelization
    Dict::const_iterator par_op = opts.find("parallelization");
    if (par_op==opts.end() || par_op->second == "serial") {
      return new KernelSum2DSerial(f, size, r, n);
    } else {
      if(par_op->second == "openmp") {
  #ifdef WITH_OPENMP
        return new KernelSum2DSerial(f, size, r, n);
  #else // WITH_OPENMP
        casadi_warning("CasADi was not compiled with OpenMP. "
                       "Falling back to serial mode.");
  #endif // WITH_OPENMP
      } else if (par_op->second == "opencl") {
        return new KernelSum2DOcl(f, size, r, n);
      }
      return new KernelSum2DSerial(f, size, r, n);
    }
  }

  KernelSum2DBase::KernelSum2DBase(const Function& f,
           const std::pair<int, int> & size,
           double r,
           int n)
    : f_(f), size_(size), r_(r), n_(n) {


    addOption("parallelization", OT_STRING, "serial",
              "Computational strategy for parallelization", "serial|openmp|opencl");

    casadi_assert(n>=1);

    casadi_assert_message(n==1, "Vectorized form of KernelSum2D not yet implemented.");

    casadi_assert(f.nIn()>=2);
    casadi_assert(f.inputSparsity(0)==Sparsity::dense(2, 1));
    casadi_assert(f.inputSparsity(1)==Sparsity::dense(1, 1));
    casadi_assert(f.inputSparsity(2)==Sparsity::dense(2, 1));

    // Give a name
    setOption("name", "unnamed_kernel_sum_2d");
  }

  KernelSum2DBase::~KernelSum2DBase() {
  }

  void KernelSum2DBase::init() {

    int num_in = f_.nIn(), num_out = f_.nOut();

    ibuf_.resize(num_in-1);
    obuf_.resize(num_out);

    input(0) = DMatrix::zeros(size_);
    input(1) = DMatrix::zeros(2, n_);

    for (int i=0;i<num_in-3;++i) {
      // Allocate space for input
      input(2+i) = DMatrix::zeros(f_.inputSparsity(i+3));
    }

    for (int i=0;i<num_out;++i) {
      // Allocate space for output
      output(i) = DMatrix::zeros(f_.outputSparsity(i));
    }

    // Call the initialization method of the base class
    FunctionInternal::init();

    step_out_.resize(num_out, 0);

    for (int i=0;i<num_out;++i) {
      step_out_[i] = f_.output(i).nnz();
    }

    // Allocate some space to evaluate each function to.
    nnz_out_ = 0;
    for (int i=0;i<num_out;++i) {
      nnz_out_+= step_out_[i];
    }

    alloc_w(f_.sz_w() + nnz_out_+3);
    alloc_iw(f_.sz_iw());
    alloc_arg(2*f_.sz_arg());
    alloc_res(2*f_.sz_res());

  }

  static bvec_t Orring(bvec_t x, bvec_t y) { return x | y; }



  void KernelSum2DBase::spFwd(const bvec_t** arg, bvec_t** res, int* iw, bvec_t* w) {
    // First input is non-differentiable

    int num_in = f_.nIn(), num_out = f_.nOut();

    // Clear the accumulators
    bvec_t** sum = res;
    for (int k=0;k<num_out;++k) {
      if (sum[k]!=0) std::fill(sum[k], sum[k]+step_out_[k], 0);
    }

    const bvec_t** arg1 = arg+f_.sz_arg();
    bvec_t** res1 = res+f_.sz_res();

    // Everything except the first argument can be passed as-is to f
    std::copy(arg+1, arg+num_in, arg1+2);

    // The first argument will be the pixel coordinates p_i
    bvec_t* coord = w+f_.sz_w()+nnz_out_;
    arg1[0] = coord;

    // The second argument will be the pixel value v_i
    bvec_t* value = w+f_.sz_w()+nnz_out_+2;
    arg1[1] = value;

    bvec_t* temp_res = w+f_.sz_w();
    // Clear the temp_res storage space
    if (temp_res!=0) std::fill(temp_res, temp_res+nnz_out_, 0);

    // Set the function outputs
    for (int j=0; j<num_out; ++j) {
      // Make the function outputs end up in temp_res
      res1[j] = (res[j]==0)? 0: temp_res;
      temp_res+= step_out_[j];
    }

    // Set the coordinate input
    coord[0] = 0;
    coord[1] = 0;

    // Set the pixel value input
    value[0] = 0;

    // Evaluate the function
    f_->spFwd(arg1, res1, iw, w);

    // Sum results from temporary storage to accumulator
    for (int k=0;k<num_out;++k) {
      if (res1[k] && sum[k])
        std::transform(res1[k], res1[k]+step_out_[k], sum[k], sum[k], &Orring);
    }
  }

  KernelSum2DSerial::~KernelSum2DSerial() {
  }

  void KernelSum2DSerial::init() {
    // Call the initialization method of the base class
    KernelSum2DBase::init();

  }

  void KernelSum2DSerial::evalD(const double** arg, double** res,
                                int* iw, double* w) {
    int num_in = f_.nIn(), num_out = f_.nOut();

    double t0 = getRealTime();

    const double* V = arg[0];
    const double* X = arg[1];

    // Clear the accumulators
    double** sum = res;
    for (int k=0;k<num_out;++k) {
      if (sum[k]!=0) std::fill(sum[k], sum[k]+step_out_[k], 0);
    }

    const double** arg1 = arg+f_.sz_arg();
    double** res1 = res+f_.sz_res();

    // Everything except the first argument can be passed as-is to f
    std::copy(arg+1, arg+num_in, arg1+2);

    // The first argument will be the pixel coordinates p_i
    double* coord = w+f_.sz_w()+nnz_out_;
    arg1[0] = coord;

    // The second argument will be the pixel value v_i

    double* value = w+f_.sz_w()+nnz_out_+2;
    arg1[1] = value;

    double* temp_res = w+f_.sz_w();

    // Clear the temp_res storage space
    if (temp_res!=0) std::fill(temp_res, temp_res+nnz_out_, 0);

    // Set the function outputs
    for (int j=0; j<num_out; ++j) {
      // Make the function outputs end up in temp_res
      res1[j] = (res[j]==0)? 0: temp_res;
      temp_res+= step_out_[j];
    }

    //     ---> j,v
    //   |
    //   v  i,u
    int u = round(X[0]);
    int v = round(X[1]);
    int r = round(r_);

    for (int j = max(v-r, 0); j<= min(v+r, size_.second-1); ++j) {
      for (int i = max(u-r, 0); i<= min(u+r, size_.first-1); ++i) {
        // Set the coordinate input
        coord[0] = i;
        coord[1] = j;

        // Set the pixel value input
        value[0] = V[i+j*size_.first];

        // Evaluate the function
        f_->eval(arg1, res1, iw, w);

        // Sum results from temporary storage to accumulator
        for (int k=0;k<num_out;++k) {
          if (res1[k] && sum[k])
            std::transform(res1[k], res1[k]+step_out_[k], sum[k], sum[k], std::plus<double>());
        }

      }
    }

    std::cout << "serial kernelsum [ms]:" << (getRealTime()-t0)*1000 << std::endl;
  }

  Function KernelSum2DBase
  ::getDerForward(const std::string& name, int nfwd, Dict& opts) {

    /* Write KernelSum2D in linear form:
    *
    *    S = F(V, X)  = sum_i  f ( P_i, v_i, X)
    *
    *  With a slight abuse of notation, we have:
    *
    *    S_dot = sum_i  f_forward ( P_i, v_i, X, X_dot)
    *
    *  The forward mode of KernelSum is another KernelSum.
    *  There is a bit of houskeeping in selecting the correct inputs/outputs.
    *
    */

    /* More exactly, the forward mode of the primitive is
    * fd( P_i, v_i, X, S, P_i_dot, v_i_dot, X_dot)
    *
    * we need to bring this in the form
    *
    *     f_forward ( P_i, v_i, X, X_dot)
    *
    */
    Function fd = f_.derForward(nfwd);

    std::vector<MX> f_inputs   = f_.symbolicInput(true);
    std::vector<MX> f_outputs  = f_.symbolicOutput();

    std::vector<MX> fd_inputs   = f_inputs;

    // Create nodes for the nominal output (S)
    std::vector<MX> f_call_out  = f_(f_inputs);

    fd_inputs.insert(fd_inputs.end(), f_call_out.begin(), f_call_out.end());
    for (int i=0;i<nfwd;++i) {
      // Pad with blanks: we don't consider P_i_dot and v_i_dot
      fd_inputs.push_back(MX());
      fd_inputs.push_back(MX());
      std::vector<MX> inputs   = f_.symbolicInput(true);
      fd_inputs.insert(fd_inputs.end(), inputs.begin()+2, inputs.end());
      f_inputs.insert(f_inputs.end(), inputs.begin()+2, inputs.end());
    }

    Function f_forward = MXFunction("f", f_inputs, fd(fd_inputs));

    Dict options = opts;
    // Propagate options (if not set already)
    if (options.find("parallelization")==options.end()) {
      options["parallelization"] = parallelization();
    }
    
    Function ret = KernelSum2D(name, f_forward, size_, r_, n_, options);

    /* Furthermore, we need to return something of signature
    *  der(V,X,S,V_dot,X_dot)
    *
    */
    std::vector<MX> der_inputs = symbolicInput();
    std::vector<MX> ret_inputs = der_inputs;

    std::vector<MX> outputs = symbolicOutput();
    der_inputs.insert(der_inputs.end(), outputs.begin(), outputs.end());

    for (int i=0;i<nfwd;++i) {
      // Construct dummy matrix for capturing the V_dot argument.
      der_inputs.push_back(MX::sym("x", Sparsity(size_) ));
      std::vector<MX> inputs = symbolicInput();
      der_inputs.insert(der_inputs.end(), inputs.begin()+1, inputs.end());
      ret_inputs.insert(ret_inputs.end(), inputs.begin()+1, inputs.end());
    }

    Function der = MXFunction("f", der_inputs, ret(ret_inputs), opts);

    return der;
  }

  Function KernelSum2DBase
  ::getDerReverse(const std::string& name, int nadj, Dict& opts) {
    /* Write KernelSum2D in linear form:
    *
    *    S = F(V, X)  = sum_i  f ( P_i, v_i, X)
    *
    *  With a slight abuse of notation, we have:
    *
    *    X_bar = sum_i  f_reverse ( P_i, v_i, X, S_bar)
    *
    *  The reverse mode of KernelSum is another KernelSum.
    *  There is a bit of houskeeping in selecting the correct inputs/outputs.
    *
    */

    int num_in = f_.nIn(), num_out = f_.nOut();

    /* More exactly, the reverse mode of the primitive is
    * fd( P_i, v_i, X, S, S_bar) -> P_i_bar, v_i_bar, X_bar
    *
    * we need to bring this in the form
    *
    *     f_reverse ( P_i, v_i, X, S_bar) -> X_bar
    *
    *
    *
    */
    Function fd = f_.derReverse(nadj);

    std::vector<MX> f_inputs   = f_.symbolicInput(true);
    std::vector<MX> f_outputs  = f_.symbolicOutput();

    std::vector<MX> fd_inputs   = f_inputs;
    // Create nodes for the nominal output (S)
    std::vector<MX> f_call_out  = f_(f_inputs);

    fd_inputs.insert(fd_inputs.end(), f_call_out.begin(), f_call_out.end());

    for (int i=0;i<nadj;++i) {
      std::vector<MX> outputs   = f_.symbolicOutput();
      fd_inputs.insert(fd_inputs.end(), outputs.begin(), outputs.end());
      f_inputs.insert(f_inputs.end(), outputs.begin(), outputs.end());
    }

    std::vector<MX> fd_outputs = fd(fd_inputs);

    // Drop the outputs we do not need: P_i_bar, v_i_bar
    f_outputs.clear();
    int offset = 2;
    for (int i=0;i<nadj;++i) {
      f_outputs.insert(f_outputs.end(), fd_outputs.begin()+offset,
        fd_outputs.begin()+offset+f_.nIn()-2);
      offset+= f_.nIn();
    }

    Function f_reverse = MXFunction("f", f_inputs, f_outputs);

    Dict options = opts;
    // Propagate options (if not set already)
    if (options.find("parallelization")==options.end()) {
      options["parallelization"] = parallelization();
    }
    
    Function kn = KernelSum2D(name, f_reverse, size_, r_, n_, options);

    /* Furthermore, we need to return something of signature
    *  der(V,X,S,S_bar) -> V_bar, X_bar
    *
    */

    std::vector<MX> ret_inputs = symbolicInput();
    std::vector<MX> kn_inputs = ret_inputs;
    for (int i=0;i<num_out;++i) {
      // Dummy symbols for the nominal outputs (S)
      MX output = MX::sym("x", Sparsity(f_.outputSparsity(i).shape()));
      ret_inputs.push_back(output);
    }
    for (int i=0;i<nadj;++i) {
      std::vector<MX> outputs = symbolicOutput();
      ret_inputs.insert(ret_inputs.end(), outputs.begin(), outputs.end());
      kn_inputs.insert(kn_inputs.end(), outputs.begin(), outputs.end());
    }

    std::vector<MX> kn_outputs = kn(kn_inputs);

    std::vector<MX> ret_outputs;
    offset = 0;
    for (int i=0;i<nadj;++i) {
      // Use sparse zero as V_bar output
      ret_outputs.push_back(MX::zeros(Sparsity(size_)));
      ret_outputs.insert(ret_outputs.end(), kn_outputs.begin()+offset,
        kn_outputs.begin()+offset+num_in-2);
      offset+= num_in-2;
    }

    Function ret = MXFunction(name, ret_inputs, ret_outputs, opts);

    return ret;
  }

  void KernelSum2DSerial::generateDeclarations(CodeGenerator& g) const {
    f_->addDependency(g);
  }

  void KernelSum2DSerial::generateBody(CodeGenerator& g) const {
    g.addAuxiliary(CodeGenerator::AUX_COPY_N);
    g.addAuxiliary(CodeGenerator::AUX_FILL_N);
    g.addAuxiliary(CodeGenerator::AUX_AXPY);
    int num_in = f_.nIn(), num_out = f_.nOut();

    g.body << "  const real_t* V = arg[0];" << std::endl;
    g.body << "  const real_t* X = arg[1];" << std::endl;

    // Clear the accumulators
    g.body << "  real_t** sum = res;" << std::endl;
    for (int k=0;k<num_out;++k) {
      g.body << "  if (sum[" << k << "]!=0) fill_n(sum[" << k << "], "
        << step_out_[k] << ", 0);" << std::endl;
    }

    g.body << "  const real_t** arg1 = arg+" << f_.sz_arg() << ";" << std::endl;
    g.body << "  real_t** res1 = res+" << f_.sz_res() << ";" << std::endl;

    // Everything except the first argument can be passed as-is to f
    g.body << "  int ii;" << std::endl;
    g.body << "  for(ii=0;ii<" << num_in -1<< ";++ii) arg1[2+ii] = arg[1+ii];" << std::endl;

    // The first argument will be the pixel coordinates p_i
    g.body << "  real_t* coord = w+" << f_.sz_w()+nnz_out_ << ";" << std::endl;
    g.body << "  arg1[0] = coord;" << std::endl;

    // The second argument will be the pixel value v_i

    g.body << "  real_t* value = w+" << f_.sz_w()+nnz_out_+2 << ";" << std::endl;
    g.body << "  arg1[1] = value;" << std::endl;

    g.body << "  real_t* temp_res = w+" << f_.sz_w() << ";" << std::endl;

    // Clear the temp_res storage space
    g.body << "  if (temp_res!=0) fill_n(temp_res, " << nnz_out_ << ", 0);" << std::endl;

    // Set the function outputs
    int offset = 0;
    for (int j=0; j<num_out; ++j) {
      // Make the function outputs end up in temp_res
      g.body << "  res1[" << j << "] = (res[" << j << "]==0)? 0: temp_res + "
        << offset << ";" << std::endl;
      offset+= step_out_[j];
    }

    //     ---> j,v
    //   |
    //   v  i,u
    g.body << "  int u = round(X[0]);" << std::endl;
    g.body << "  int v = round(X[1]);" << std::endl;
    g.body << "  int jmin = v-" << round(r_) << "; jmin = jmin<0? 0 : jmin;" << std::endl;
    g.body << "  int imin = u-" << round(r_) << "; imin = imin<0? 0 : imin;" << std::endl;

    g.body << "  int jmax = v+" << round(r_) << ";" <<
      "jmax = jmax>" << size_.second-1 << "? " << size_.second-1 <<"  : jmax;" << std::endl;
    g.body << "  int imax = u+" << round(r_) << ";" <<
      "imax = imax>" << size_.first-1 << "? " << size_.first-1 <<"  : imax;" << std::endl;

    g.body << "  int i,j;" << std::endl;
    g.body << "  for (j = jmin;j<= jmax;++j) {" << std::endl;
    g.body << "    for (i = imin; i<= imax;++i) {" << std::endl;


    // Set the coordinate input
    g.body << "      coord[0] = i;" << std::endl;
    g.body << "      coord[1] = j;" << std::endl;

    // Set the pixel value input
    g.body << "      value[0] = V[i+j*" << size_.first << "];" << std::endl;

    // Evaluate the function
    g.body << "      " << g.call(f_, "arg1", "res1", "iw", "w") << ";" << endl;

    // Sum results from temporary storage to accumulator
    for (int k=0;k<num_out;++k) {
      g.body << "      if (res1[" << k << "] && sum[" << k << "])" << endl
             << "       axpy(" << step_out_[k] << ",1," <<
                          "res1["<< k << "],1,sum[" << k << "],1);" << endl;
    }
    g.body << "    }" << std::endl;
    g.body << "  }" << std::endl;

  }

  inline string name(const Function& f) {
    if (f.isNull()) {
      return "NULL";
    } else {
      return f.getOption("name");
    }
  }
  
  KernelSum2DOcl::KernelSum2DOcl(const Function& f,
    const std::pair<int, int> & size,
    double r,
    int n) : KernelSum2DBase(f, size, r, n) {
      addOption("opencl_select", OT_INTEGERVECTOR, std::vector<int>(1, 0),
        "List with indices into OpenCL-compatible devices, to select which one to use. "
        "You may use 'verbose' option to see the list of devices.");


  }

  KernelSum2DOcl::~KernelSum2DOcl() {
  }

  void KernelSum2DSerial::print(ostream &stream) const {
    stream << "KernelSum2D(" << name(f_) << ", " << n_ << ")";
  }


  void KernelSum2DOcl::init() {
  
    int num_in = f_.nIn(), num_out = f_.nOut();
    // Call the initialization method of the base class
    KernelSum2DBase::init();

    s_ = round(r_)*2+1;
    ss_ = 2;

    sfrac_ = double(s_)/ss_;

    arg_length_ = 0;
    for (int i=2; i<num_in; ++i) {
      arg_length_+= f_.inputSparsity(i).nnz();
    }
    
    // Read in options
    opencl_select_ = getOption("opencl_select");

    alloc_w(f_.sz_w() + nnz_out_+3 + sizeof(float)*(s_*s_+arg_length_+f_.nnzOut()*s_*ss_)/sizeof(double));

  }
  
  std::string KernelSum2DOcl::kernelCode() const {
    std::stringstream code;

    Dict opts;
    opts["opencl"] = true;
    opts["meta"] = false;

    CodeGenerator cg(opts);
    cg.add(f_,"F");

    //code << "#pragma OPENCL EXTENSION cl_khr_fp64: enable" << std::endl;
    code << "#define d float" << std::endl;
    code << "#define real_t float" << std::endl;
    code << "#define CASADI_PREFIX(ID) test_c_ ## ID" << std::endl;

    code << cg.generate() << std::endl;

    code << "__kernel void mykernel(" << std::endl;
    code << "   __global float* im_in," << std::endl;     
    code << "   __global float* sum_out," << std::endl; 
    code << "   __global float* args," << std::endl;              
    code << "   int i_offset," << std::endl;  
    code << "   int j_offset) {" << std::endl; 

    code << "  float args_local[" << arg_length_ << "];" << std::endl;
    code << "  float p[2];" << std::endl;
    code << "  float value;" << std::endl;
    code << "  int jj = get_global_id(0);" << std::endl;
    code << "  int j = jj / " << ss_ << ";" << std::endl;
    code << "  int jk = jj % " << ss_ << ";" << std::endl;
    code << "  for (int k=0;k<"<< arg_length_ << ";++k) { args_local[k] = args[k]; }" << std::endl;

    code << "  int iw[" << f_.sz_iw() << "];" << std::endl;
    code << "  float w[" << f_.sz_w() << "];" << std::endl;
    code << "  float res_local[" << f_.nnzOut() << "];" << std::endl;
    code << "  float sum[" << f_.nnzOut() << "];" << std::endl;
    code << "  const d* arg[" << f_.sz_arg() << "];" << std::endl;
    code << "  d* res[" << f_.sz_res() << "];" << std::endl;
    code << "  arg[0] = p;arg[1]=&value;";

    int offset= 0;
    for (int i=2;i<f_.nIn();++i) {
      code << "  arg[" << i << "] = args_local+" << offset << ";" << std::endl;
      offset+= f_.inputSparsity(i).nnz();
    }

    offset= 0;
    for (int i=0;i<f_.nOut();++i) {
      code << "  res[" << i << "] = res_local+" << offset << ";"  << std::endl;
      offset+= f_.outputSparsity(i).nnz();
    }
    code << "  p[1] = j_offset + j;" << std::endl;
    code << "  for (int k=0;k<" << f_.nnzOut() << ";++k) { sum[k]= 0; }" << std::endl;
    std::stringstream ss;
    ss << std::scientific << sfrac_;
    code << "  int upper = (int) ceil((jk+1)*" << ss.str() << " );" << std::endl;
    code << "  int lower = (int) ceil(jk*" << ss.str() << ");" << std::endl;
    code << "  for (int i=lower;i<upper;++i) {" << std::endl;  
    code << "    value = im_in[j*" << s_ <<" + i];" << std::endl;
    code << "    p[0] = i_offset + i;" << std::endl;
    code << "    F(arg,res,iw,w); " << std::endl;
    code << "    for (int k=0;k<" << f_.nnzOut() << ";++k) { sum[k]+= res_local[k]; }" << std::endl;
    code << "  }" << std::endl;
    code << "  for (int k=0;k<"<< f_.nnzOut() << ";++k) { sum_out[k+jj*" << f_.nnzOut() << "] = sum[k]; }" << std::endl;
    code << "}   " << std::endl;     
    
    std::cout << code.str() << std::endl;
    
       
    return code.str();
    
  }

  void KernelSum2DOcl::evalD(const double** arg, double** res, int* iw, double* w) {
    
  }

  void KernelSum2DOcl::generateDeclarations(CodeGenerator& g) const {
    f_->addDependency(g);
  }

  void KernelSum2DOcl::generateBody(CodeGenerator& g) const {
  
    g.addAuxiliary(CodeGenerator::AUX_MINMAX);
    g.addAuxiliary(CodeGenerator::AUX_ASSERT);
    
    g.addInclude("CL/cl.h");

    
    int& ind = g.added_dependencies_[this];

    g.declarations << "static cl_kernel kernel" << ind << "_ = 0;" << std::endl;
    g.declarations << "static cl_command_queue commands" << ind << "_ = 0;" << std::endl;
    g.declarations << "static cl_context context" << ind << "_ = 0;" << std::endl;
    g.declarations << "static cl_mem d_im" << ind << "_ = 0;" << std::endl;
    g.declarations << "static cl_mem d_sum" << ind << "_ = 0;" << std::endl;
    g.declarations << "static cl_mem d_args" << ind << "_ = 0;" << std::endl;
    // NB: we copy the host pointers here too
    g.declarations << "#define check_cl_error(a) assert_action(a == CL_SUCCESS, printf(\"OpenCL exit code '%d'\\n\",a))" << std::endl;
 
    g.setup << "  {" << std::endl;
    g.setup << "    cl_uint numPlatforms;" << std::endl;
    g.setup << "    int err = clGetPlatformIDs(0, NULL, &numPlatforms);" << std::endl;
    g.setup << "    cl_platform_id Platform[numPlatforms];" << std::endl;
    g.setup << "    err = clGetPlatformIDs(numPlatforms, Platform, NULL);" << std::endl;

    //casadi_assert_message(err == CL_SUCCESS, "CL error during getting platforms: " << err);

    g.setup << "    cl_device_id mydevices[" << opencl_select_.size() << "];"  << std::endl;

    // Secure a GPU
    g.setup << "    int i,j;" << std::endl;
    g.setup << "    for (i = 0; i < numPlatforms; i++) {" << std::endl;
    g.setup << "      cl_uint n = 0;" << std::endl;
    g.setup << "      err = clGetDeviceIDs(Platform[i], CL_DEVICE_TYPE_ALL, 0, NULL, &n);" << std::endl;
    g.setup << "      check_cl_error(err);" << std::endl;
    g.setup << "      cl_device_id device_id[n];" << std::endl;
    g.setup << "      err = clGetDeviceIDs(Platform[i], CL_DEVICE_TYPE_ALL, n, device_id, NULL);" << std::endl;
    g.setup << "      check_cl_error(err);" << std::endl;
    g.setup << "      mydevices[0] = device_id[0];" << std::endl;
    g.setup << "      for (j=0;j<n;++j) {" << std::endl;
    g.setup << "        cl_char device_name[1024] = {0};" << std::endl;
    g.setup << "        err = clGetDeviceInfo( device_id[j],CL_DEVICE_NAME, sizeof(device_name), &device_name, NULL);" << std::endl;
    g.setup << "        check_cl_error(err);" << std::endl;
    g.setup << "        printf(\"Detected device: %s\\n\",device_name);" << std::endl;
    g.setup << "      }" << std::endl;
    g.setup << "    }" << std::endl;


    // Create a compute context 
    g.setup << "    context" << ind << "_ = clCreateContext(0, 1, mydevices, NULL, NULL, &err);" << std::endl;
    //casadi_assert_message(err == CL_SUCCESS, "CL error getting device name: " << err);

    // Create a command queue
    g.setup << "    commands" << ind << "_ = clCreateCommandQueue(context" << ind << "_, mydevices[0], 0, &err);" << std::endl;

    g.setup << "    const char *KernelSource = " << g.multiline_string(kernelCode()+"\n") << ";" << std::endl;

    // Create the compute program from the source buffer
    g.setup << "    cl_program program = clCreateProgramWithSource(context" << ind << "_, 1, (const char **) & KernelSource, NULL, &err);" << std::endl;


    // Build the program  
    g.setup << "    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);" << std::endl;
    g.setup << "    if (err != CL_SUCCESS) {" << std::endl;
    g.setup << "      size_t len;" << std::endl;
    g.setup << "      char buffer[200048];" << std::endl;
    g.setup << "      clGetProgramBuildInfo(program, mydevices[0], CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);" << std::endl;
    g.setup << "      printf(\"%s\\n\", buffer);" << std::endl;
    g.setup << "      check_cl_error(err);" << std::endl;
    g.setup << "    }" << std::endl;


    // Create the compute kernel from the program 
    g.setup << "    kernel" << ind << "_ = clCreateKernel(program, \"mykernel\", &err);" << std::endl;
    g.setup << "    check_cl_error(err);" << std::endl;
    g.setup << "  }" << std::endl;
    
    g.body << "  int i,j,k;" << std::endl;

    g.body << "  int i_offset;" << std::endl;
    g.body << "  int j_offset;" << std::endl;
    g.body << "  size_t global = " << s_*ss_ << ";" << std::endl;

    for (int i=0;i<f_.nOut();++i) {
      g.body << "    if (res[" << i << "]) {" << std::endl;
      g.body << "      for (k=0;k<" << f_.outputSparsity(i).nnz() << ";++k) {" << std::endl;
      g.body << "        res[" << i << "][k] = 0;" << std::endl;
      g.body << "      }" << std::endl;
      g.body << "    }" << std::endl;
    }
                                
    g.body << "  const real_t* V = arg[0];" << std::endl;
    g.body << "  const real_t* X = arg[1];" << std::endl;

    g.body << "  int u = round(X[0]);" << std::endl;
    g.body << "  int v = round(X[1]);" << std::endl;
    g.body << "  int r = round(" << r_ << ");" << std::endl;
    
    g.body << "  float *h_args_ = (float*) (w+" << f_.sz_w() + nnz_out_+3 << ");" << std::endl;
    g.body << "  float *h_im_ = (float*) (w+" << f_.sz_w() + nnz_out_+3 <<  "+sizeof(float)*("<< arg_length_ << ")/sizeof(double));" << std::endl;
    g.body << "  float *h_sum_ = (float*) (w+" << f_.sz_w() + nnz_out_+3 <<  "+sizeof(float)*("<< arg_length_ + s_*s_ << ")/sizeof(double));" << std::endl;
    
    g.body << "  for (i =0;i<" << s_*s_ << ";++i) h_im_[i] = 0;" << std::endl;

    g.body << "  j_offset = v-r;" << std::endl;
    g.body << "  i_offset = u-r;" << std::endl;

    g.body << "  for (j = MAX(v-r, 0); j<= MIN(v+r, " << size_.second-1 << "); ++j) {" << std::endl;
    g.body << "    for (i = MAX(u-r, 0); i<= MIN(u+r, " <<  size_.first-1 << "); ++i) {" << std::endl;

    // Set the pixel value input
    g.body << "      h_im_[(i-i_offset)+(j-j_offset)*" << s_ << "] = V[i+j*" << size_.first << "];" << std::endl;
    g.body << "    }" << std::endl;
    g.body << "  }" << std::endl;

    g.body << "  int kk = 0;" << std::endl;
    for (int i=2;i<f_.nIn();++i) {
      g.body << "  for (k=0;k<" << f_.inputSparsity(i).nnz() << ";++k) {" << std::endl;
      g.body << "    h_args_[kk] = arg[" << i-1 << "][k];" << std::endl;
      g.body << "    kk++;" << std::endl;
      g.body << "  }" << std::endl;
    }

    g.body << "  int err;" << std::endl;
    // NB: we copy the host pointers here too
    g.body << "  d_args" << ind << "_  = clCreateBuffer(context" << ind << "_,  CL_MEM_READ_ONLY, sizeof(float)*" <<  arg_length_ << ", NULL, &err);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;
    g.body << "  d_im" << ind << "_    = clCreateBuffer(context" << ind << "_,  CL_MEM_READ_ONLY, sizeof(float)*" << s_*s_ << ", NULL, &err);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;
    g.body << "  d_sum" << ind << "_   = clCreateBuffer(context" << ind << "_,  CL_MEM_WRITE_ONLY, sizeof(float)*" << f_.nnzOut()*s_*ss_ << ", NULL, &err);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;

    g.body << "  err = clEnqueueWriteBuffer(commands" << ind << "_, d_args" << ind << "_, CL_TRUE, 0, sizeof(float) * " << arg_length_ << ",h_args_, 0, NULL, NULL);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;
    g.body << "  err = clEnqueueWriteBuffer(commands" << ind << "_, d_im" << ind << "_, CL_TRUE, 0, sizeof(float) * " << s_*s_ << ",h_im_, 0, NULL, NULL);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;

    g.body << "  err   = clSetKernelArg(kernel" << ind << "_, 0, sizeof(cl_mem), &d_im" << ind << "_);" << std::endl;
    g.body << "  err  |= clSetKernelArg(kernel" << ind << "_, 1, sizeof(cl_mem), &d_sum" << ind << "_);" << std::endl;
    g.body << "  err  |= clSetKernelArg(kernel" << ind << "_, 2, sizeof(cl_mem), &d_args" << ind << "_);" << std::endl;
    g.body << "  err  |= clSetKernelArg(kernel" << ind << "_, 3, sizeof(int), &i_offset);" << std::endl;
    g.body << "  err  |= clSetKernelArg(kernel" << ind << "_, 4, sizeof(int), &j_offset);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;

    g.body << "  err = clEnqueueNDRangeKernel(commands" << ind << "_, kernel" << ind << "_, 1, NULL, &global, NULL, 0, NULL, NULL);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;
    g.body << "  err = clFinish(commands" << ind << "_);" << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;
    g.body << "  err = clEnqueueReadBuffer( commands" << ind << "_, d_sum" << ind << "_, CL_TRUE, 0, " << sizeof(float) * (f_.nnzOut()*s_*ss_) << ", h_sum_, 0, NULL, NULL ); " << std::endl;
    g.body << "  check_cl_error(err);" << std::endl;
    double tout = getRealTime();
    g.body << "  kk = 0;" << std::endl;
    g.body << "  for (j=0;j<" << s_*ss_ << ";++j) {" << std::endl;
      for (int i=0;i<f_.nOut();++i) {
        g.body << "    if (res[" << i << "]) {" <<std::endl;
        g.body << "      for (k=0;k<" << f_.outputSparsity(i).nnz() << ";++k) {" <<std::endl;
        g.body << "        res[" << i << "][k] += h_sum_[kk];" <<std::endl;
        g.body << "        kk++;" <<std::endl;
        g.body << "      }" <<std::endl;
        g.body << "    } else {" <<std::endl;
        g.body << "      kk += " << f_.outputSparsity(i).nnz() << ";"<< std::endl;
        g.body << "  }" <<std::endl;
      }
   g.body << "  }" <<std::endl;
  }

} // namespace casadi
