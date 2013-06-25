/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
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

#include "set_sparse.hpp"
#include "mx_tools.hpp"
#include <vector>
#include <sstream>
#include "../stl_vector_tools.hpp"

using namespace std;

namespace CasADi{

  SetSparse::SetSparse(const MX& x, const CRSSparsity& sp){
    setDependencies(x);
    setSparsity(CRSSparsity(sp));
  }

  SetSparse* SetSparse::clone() const{
    return new SetSparse(*this);
  }

  void SetSparse::printPart(std::ostream &stream, int part) const{
    if(part==0){
      stream << "dense(";
    } else {
      stream << ")";
    }
  }

  template<typename T, typename MatV, typename MatVV> 
  void SetSparse::evaluateGen(const MatV& input, MatV& output, const MatVV& fwdSeed, MatVV& fwdSens, const MatVV& adjSeed, MatVV& adjSens){

    int nfwd = fwdSens.size();
    int nadj = adjSeed.size();

    // Propate values
    output[0]->set(*input[0]);
    
    // Propagate forward seeds
    for(int d=0; d<nfwd; ++d){
      fwdSens[d][0]->set(*fwdSeed[d][0]);
    }

    // Propagate adjoint seeds
    for(int d=0; d<nadj; ++d){      
      adjSens[d][0]->sparsity().add(adjSens[d][0]->ptr(),adjSeed[d][0]->ptr(),adjSeed[d][0]->sparsity());
      adjSeed[d][0]->setZero();
    }
  }

  void SetSparse::evaluateD(const DMatrixPtrV& input, DMatrixPtrV& output, const DMatrixPtrVV& fwdSeed, DMatrixPtrVV& fwdSens, const DMatrixPtrVV& adjSeed, DMatrixPtrVV& adjSens){
    evaluateGen<double,DMatrixPtrV,DMatrixPtrVV>(input,output,fwdSeed,fwdSens,adjSeed,adjSens);
  }

  void SetSparse::evaluateSX(const SXMatrixPtrV& input, SXMatrixPtrV& output, const SXMatrixPtrVV& fwdSeed, SXMatrixPtrVV& fwdSens, const SXMatrixPtrVV& adjSeed, SXMatrixPtrVV& adjSens){
    evaluateGen<SX,SXMatrixPtrV,SXMatrixPtrVV>(input,output,fwdSeed,fwdSens,adjSeed,adjSens);
  }

  void SetSparse::evaluateMX(const MXPtrV& input, MXPtrV& output, const MXPtrVV& fwdSeed, MXPtrVV& fwdSens, const MXPtrVV& adjSeed, MXPtrVV& adjSens, bool output_given){
    // Evaluate function
    if(!output_given){
      *output[0] = input[0]->setSparse(sparsity());
    }
  
    // Propagate forward seeds
    int nfwd = fwdSens.size();
    for(int d=0; d<nfwd; ++d){
      *fwdSens[d][0] = fwdSeed[d][0]->setSparse(sparsity(),true);
    }

    // Propagate adjoint seeds
    int nadj = adjSeed.size();
    for(int d=0; d<nadj; ++d){
      *adjSens[d][0] += adjSeed[d][0]->setSparse(dep().sparsity(),true);
      *adjSeed[d][0] = MX();
    }
  }

  void SetSparse::propagateSparsity(DMatrixPtrV& input, DMatrixPtrV& output, bool fwd){
    bvec_t *inputd = get_bvec_t(input[0]->data());
    bvec_t *outputd = get_bvec_t(output[0]->data());
    if(fwd){
      output[0]->sparsity().set(outputd,inputd,input[0]->sparsity());
    } else {
      input[0]->sparsity().bor(inputd,outputd,output[0]->sparsity());
      fill(outputd,outputd+output[0]->size(),bvec_t(0));
    }
  }

  void SetSparse::generateOperation(std::ostream &stream, const std::vector<std::string>& arg, const std::vector<std::string>& res, CodeGenerator& gen) const{
    // Codegen "copy sparse"
    gen.addAuxiliary(CodeGenerator::AUX_COPY_SPARSE);

    // Codegen the operation
    int sp_arg = gen.getSparsity(dep(0).sparsity());
    int sp_res = gen.addSparsity(sparsity());
    stream << "  casadi_copy_sparse(" << arg.front() << ",s" << sp_arg << "," << res.front() << ",s" << sp_res << ");" << std::endl;
  }


} // namespace CasADi

