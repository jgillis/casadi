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

#ifndef LAPACK_LU_DENSE_HPP
#define LAPACK_LU_DENSE_HPP

#include "symbolic/fx/linear_solver_internal.hpp"

namespace CasADi{
  
  /** \brief  Forward declaration of internal class

      @copydoc LinearSolver_doc
  */
  class LapackLUDenseInternal;

  /** \brief  LU LinearSolver with Lapack Interface
   * @copydoc LinearSolver_doc
   *
   * This class solves the linear system A.x=b by making an LU factorization of A: \n
   * A = L.U, with L lower and U upper triangular
   * 
   * LapackLUDense is an CasADi::FX mapping from 2 inputs [ A (matrix),b (vector)] to one output [x (vector)].
   *
   * The usual procedure to use LapackLUDense is: \n
   *  -# init()
   *  -# set the first input (A)
   *  -# prepare()
   *  -# set the second input (b)
   *  -# solve()
   *  -# Repeat steps 4 and 5 to work with other b vectors.
   *
   * The method evaluate() combines the prepare() and solve() step and is therefore more expensive if A is invariant.
   *
   */
  class LapackLUDense : public LinearSolver{
  public:

    /// Default (empty) constructor
    LapackLUDense();
  
    /// Create a linear solver given a sparsity pattern
    LapackLUDense(const CRSSparsity& sparsity, int nrhs=1);
    
    /// Access functions of the node
    LapackLUDenseInternal* operator->();
    const LapackLUDenseInternal* operator->() const;
  
    /// Static creator function
#ifdef SWIG
    %callback("%s_cb");
#endif
    static LinearSolver creator(const CRSSparsity& sp, int nrhs){ return LapackLUDense(sp,nrhs);}
#ifdef SWIG
    %nocallback;
#endif

  };

#ifndef SWIG

  /// LU-Factorize dense matrix (lapack)
  extern "C" void dgetrf_(int *m, int *n, double *a, int *lda, int *ipiv, int *info);

  /// Solve a system of equation using an LU-factorized matrix (lapack)
  extern "C" void dgetrs_(char* trans, int *n, int *nrhs, double *a, int *lda, int *ipiv, double *b, int *ldb, int *info);

  /// Calculate row and column scalings
  extern "C" void dgeequ_(int *m, int *n, double *a, int *lda, double *r, double *c, double *rowcnd, double *colcnd, double *amax, int *info);

  /// Equilibriate the system
  extern "C" void dlaqge_(int *m, int *n, double *a, int *lda, double *r, double *c, double *rowcnd, double *colcnd, double *amax, char *equed );

  /// Internal class
  class LapackLUDenseInternal : public LinearSolverInternal{
  public:
    // Create a linear solver given a sparsity pattern and a number of right hand sides
    LapackLUDenseInternal(const CRSSparsity& sparsity, int nrhs);

    // Clone
    virtual LapackLUDenseInternal* clone() const;
    
    // Destructor
    virtual ~LapackLUDenseInternal();
    
    // Initialize the solver
    virtual void init();

    // Prepare the solution of the linear system
    virtual void prepare();
    
    // Solve the system of equations
    virtual void solve(double* x, int nrhs, bool transpose);

  protected:

    // Scale rows
    void rowScaling(double* x, int nrhs);
    
    // Scale columns
    void columnScaling(double* x, int nrhs);
    
    // Matrix
    std::vector<double> mat_;
    
    // Pivoting elements
    std::vector<int> ipiv_;

    // Row and column scaling
    std::vector<double> r_, c_;
    
    // Type of scaling during the last equilibration
    char equed_;
    
    // Equilibriate?
    bool equilibriate_;

    // Allow the equilibration to fail
    bool allow_equilibration_failure_;
        
    // Dimensions
    int nrow_, ncol_;
    
  };

#endif // SWIG

} // namespace CasADi

#endif //LAPACK_LU_DENSE_HPP
