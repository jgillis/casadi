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

#ifndef CALL_FX_HPP
#define CALL_FX_HPP

#include "multiple_output.hpp"
#include "../fx/fx.hpp"

namespace CasADi{

  /** 
      \author Joel Andersson 
      \date 2010-2013
  */
  class CallFX : public MultipleOutput{
  public:

    /** \brief  Constructor */
    explicit CallFX(const FX& fcn, std::vector<MX> arg);
    
    /** \brief  Destructor */
    virtual ~CallFX(){}
  
    /** \brief  Clone function */
    virtual CallFX* clone() const;

    /** \brief  Print a part of the expression */
    virtual void printPart(std::ostream &stream, int part) const;

    /** \brief Generate code for the operation */
    virtual void generateOperation(std::ostream &stream, const std::vector<std::string>& arg, const std::vector<std::string>& res, CodeGenerator& gen) const;

    /** \brief  Evaluate the function numerically */
    virtual void evaluateD(const DMatrixPtrV& input, DMatrixPtrV& output, const DMatrixPtrVV& fwdSeed, DMatrixPtrVV& fwdSens, const DMatrixPtrVV& adjSeed, DMatrixPtrVV& adjSens, std::vector<int>& itmp, std::vector<double>& rtmp);

    /** \brief  Evaluate the function symbolically (SX) */
    virtual void evaluateSX(const SXMatrixPtrV& input, SXMatrixPtrV& output, const SXMatrixPtrVV& fwdSeed, SXMatrixPtrVV& fwdSens, const SXMatrixPtrVV& adjSeed, SXMatrixPtrVV& adjSens, std::vector<int>& itmp, std::vector<SX>& rtmp);

    /** \brief  Evaluate the function symbolically (MX) */
    virtual void evaluateMX(const MXPtrV& input, MXPtrV& output, const MXPtrVV& fwdSeed, MXPtrVV& fwdSens, const MXPtrVV& adjSeed, MXPtrVV& adjSens, bool output_given);

    /** \brief  Propagate sparsity */
    virtual void propagateSparsity(DMatrixPtrV& input, DMatrixPtrV& output, std::vector<int>& itmp, std::vector<double>& rtmp, bool fwd);

    /** \brief  Get function reference */
    virtual FX& getFunction();

    /** \brief  Get function input */
    virtual int getFunctionInput() const{ return -1;}

    /** \brief  Get function output */
    virtual int getFunctionOutput() const{ return -1;}

    /** \brief  Deep copy data members */
    virtual void deepCopyMembers(std::map<SharedObjectNode*,SharedObject>& already_copied);

    /** \brief  Number of outputs */
    virtual int getNumOutputs() const;
        
    /** \brief  Get the sparsity of output oind */
    virtual const CRSSparsity& sparsity(int oind) const;

    /** \brief Get the operation */
    virtual int getOp() const{ return OP_CALL;}
    
    /// Get number of temporary variables needed
    virtual void nTmp(size_t& ni, size_t& nr);

    // Function to be evaluated
    FX fcn_;
  };

} // namespace CasADi

#endif // CALL_FX_HPP
